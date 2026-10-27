// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace tsunami::rcomp {

// Restricted internal gain-history/audio slice, NOT a physical-control Processor.
// Spans/shift are supplied kernel coefficients; no millisecond mapping is implied.
// Restricted reduction: original shared-ring length R=2*H+2*A+B, H a power of
// two, A=firstSpan, B=middleSpan, H=audioDelay. Detector/ARC stages are excluded.
class GainHistory {
public:
    struct Frame { float gain; float audio; float delayed; };

    // Off-audio-thread allocation. Zero history is a stage-level initialization.
    bool prepare(unsigned firstSpan, unsigned middleSpan, unsigned shift,
                 unsigned audioDelay) {
        // This bounded allocation limit is replacement policy, not a recovered
        // maximum control range. Validate before changing any live state.
        if (firstSpan == 0 || firstSpan > 65536 || middleSpan > 65536
            || 2u * firstSpan + middleSpan > 65536 || shift > 31
            || audioDelay == 0 || audioDelay > 65536
            || (audioDelay & (audioDelay - 1u)) != 0) return false;
        firstSpan_ = firstSpan;
        middleSpan_ = middleSpan;
        shift_ = shift;
        history_.assign(2 * firstSpan + middleSpan + 1, 0);
        audio_.assign(audioDelay, 0.0f);
        position_ = audioPosition_ = 0;
        previous_ = older_ = 0;
        return true;
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0u);
        std::fill(audio_.begin(), audio_.end(), 0.0f);
        position_ = audioPosition_ = 0;
        previous_ = older_ = 0;
    }

    // Startup state with a constant past: every gain slot holds the raw Q31 word
    // and both recursion states hold accumulator; the audio delay stays zero.
    void prefill(std::uint32_t word, std::uint32_t accumulator) noexcept {
        reset();
        std::fill(history_.begin(), history_.end(), word);
        previous_ = older_ = accumulator;
    }

    // Single owner; call prepare first. Finite audio is required. Gain conversion
    // explicitly reproduces x86 CVTTSS2SI, including its integer-indefinite result.
    Frame tick(float gain, float audio) noexcept {
        const float scaled = gain * 2147483648.0f;
        const auto word = (scaled >= -2147483648.0f && scaled < 2147483648.0f)
            ? static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled))
            : 0x80000000u;
        history_[position_] = word;
        const auto tap = [this](unsigned delay) {
            const auto index = (position_ + history_.size() - delay) % history_.size();
            return arithmeticShift(history_[index], shift_);
        };
        // Explicit modulo-2^32 arithmetic reproduces the kernel's integer ALU,
        // without signed-overflow UB or implementation-defined signed shifts.
        const std::uint32_t next = tap(0) + tap(2 * firstSpan_ + middleSpan_)
            - tap(firstSpan_) - tap(firstSpan_ + middleSpan_)
            - older_ + 2u * previous_;
        older_ = previous_;
        previous_ = next;
        position_ = (position_ + 1) % history_.size();
        std::int32_t signedNext;
        std::memcpy(&signedNext, &next, sizeof(next));
        const float smoothed = static_cast<float>(
            static_cast<double>(signedNext) * 4.656612873077e-10);
        const float delayed = audio_[audioPosition_];
        audio_[audioPosition_] = audio;
        audioPosition_ = (audioPosition_ + 1) % audio_.size();
        return {smoothed, smoothed * delayed, delayed};
    }

private:
    static std::uint32_t arithmeticShift(std::uint32_t word, unsigned shift) noexcept {
        if (shift == 0) return word;
        const auto sign = (word & 0x80000000u) ? (~std::uint32_t{0} << (32 - shift)) : 0u;
        return (word >> shift) | sign;
    }
    unsigned firstSpan_ = 0, middleSpan_ = 0, shift_ = 0;
    std::vector<std::uint32_t> history_;
    std::vector<float> audio_;
    std::size_t position_ = 0, audioPosition_ = 0;
    std::uint32_t previous_ = 0, older_ = 0;
};

} // namespace tsunami::rcomp
