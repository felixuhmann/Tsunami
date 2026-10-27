// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GainHistory.h"
#include "HistoryGeometry.h"
#include <cmath>
#include <cstdint>
#include <utility>

namespace tsunami::rcomp {

// Mono wet path from computed gain to makeup-scaled delayed audio: rate-selected
// history geometry, gain history and the reference kernel's makeup multiply.
// Prepare/reset reproduce the reference startup state (unity computed gain).
// Still partial: the detector/gain computer, soft-gain law and ARC are NOT included.
class WetPath {
public:
    // Off-audio-thread. Invalid rates leave the previous configuration intact.
    bool prepare(int sampleRateHz) {
        HistoryGeometry g{};
        if (!historyGeometryForRate(sampleRateHz, g)) return false;
        GainHistory history;
        if (!history.prepare(g.firstSpan, g.middleSpan, g.shift, g.delay)) return false;
        MakeupSplit m{};
        if (!splitMakeup(gainDb_, g, m)) return false;
        geometry_ = g;
        history_ = std::move(history);
        makeup_ = scaleOf(m);
        reset();
        return true;
    }

    // Physical Gain in dB folded with the geometry normalization; atomic on failure.
    bool setMakeup(float gainDb) noexcept {
        MakeupSplit m{};
        if (geometry_.delay == 0 || !splitMakeup(gainDb, geometry_, m)) return false;
        gainDb_ = gainDb;
        makeup_ = scaleOf(m);
        return true;
    }

    // Reference startup (0x180029540 ring, 0x180028450 states, same Q31 writer):
    // past gain slots hold 0.0625 and both recursion states 0.0625*inverseNorm,
    // i.e. a history that has always held unity computed gain.
    void reset() noexcept {
        history_.prefill(toQ31(kHistoryUnity),
                         toQ31(0.0625 * geometry_.inverseNormalization));
    }

    // computedGain is the unit-domain gain (1 = no reduction). The history stores
    // gain at 1/16 scale, the level of the reference prefill and meter reference.
    float tick(float computedGain, float audio) noexcept {
        return tickHistory(computedGain * kHistoryUnity, audio);
    }

    // historyGain is already in the kernel history domain (unity = 0.0625), as
    // produced by the soft-gain/ceiling stage.
    float tickHistory(float historyGain, float audio) noexcept {
        return tickHistoryAligned(historyGain, audio).wet;
    }

    // Wet output plus the same H-delayed audio, which the kernel's output mix
    // uses as its aligned dry input.
    struct Aligned { float wet; float dry; };
    Aligned tickHistoryAligned(float historyGain, float audio) noexcept {
        const GainHistory::Frame f = history_.tick(historyGain, audio);
        return {(f.gain * makeup_) * f.delayed, f.delayed};
    }

    float makeupScale() const noexcept { return makeup_; }
    const HistoryGeometry& geometry() const noexcept { return geometry_; }
    unsigned latency() const noexcept { return geometry_.delay; }

    static constexpr float kHistoryUnity = 0.0625f;

private:
    // Both startup values are exact multiples of 2^-31 for every rate band.
    static std::uint32_t toQ31(double value) noexcept {
        return static_cast<std::uint32_t>(std::llround(std::ldexp(value, 31)));
    }

    // The kernel forms the float mantissa coefficient times 2^(exponent+4).
    static float scaleOf(const MakeupSplit& m) noexcept {
        return static_cast<float>(m.mantissa)
            * static_cast<float>(1 << m.shiftCompensation);
    }

    HistoryGeometry geometry_{};
    GainHistory history_;
    float gainDb_ = 0.0f;
    float makeup_ = 0.0f;
};

} // namespace tsunami::rcomp
