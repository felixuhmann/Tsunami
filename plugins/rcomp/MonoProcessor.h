// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OutputStage.h"
#include "WetChain.h"

namespace tsunami::rcomp {

// Connected mono signal path in the reference kernel's per-sample order:
// WetChain (detector -> soft gain/ceiling -> history -> makeup) and the static
// physical output mix, whose dry input is the same H-delayed audio. Physical
// Gain enters only through the wet-path makeup (0x18002cef0); Trim/Mix through
// the output coefficients (0x18002e0d0). PARTIAL: wet coefficients are in the
// GainStage coefficient domain; the Threshold/Ratio/Attack/Release/ARC mapping
// is NOT recovered, there is no automation smoothing, stereo or mode handling.
class MonoProcessor {
public:
    // Off-audio-thread. Atomic on failure.
    bool prepare(int sampleRateHz) { return chain_.prepare(sampleRateHz); }

    bool configureWet(const GainStage::Coefficients& coefficients) noexcept {
        return chain_.configure(coefficients);
    }

    // Physical Gain dB [-30,30], Trim dB [-18,18], Mix % [0,100]. Atomic on failure.
    bool setOutput(float gainDb, float trimDb, float mixPercent) noexcept {
        OutputStage candidate = output_;
        if (!candidate.setGainTrim(0.0f, trimDb) || !candidate.setMix(mixPercent)) return false;
        if (!std::isfinite(gainDb) || gainDb < -30.0f || gainDb > 30.0f
            || !chain_.setMakeup(gainDb)) return false;
        output_ = candidate;
        return true;
    }

    void reset() noexcept { chain_.reset(); }

    // In-place allowed. Allocation-, lock- and I/O-free.
    void process(const float* input, float* output, unsigned frames) noexcept {
        for (unsigned i = 0; i < frames; ++i) {
            const WetPath::Aligned a = chain_.tickAligned(input[i]);
            output[i] = output_.tick(a.dry, a.wet);
        }
    }

    unsigned latency() const noexcept { return chain_.latency(); }

private:
    WetChain chain_;
    OutputStage output_;
};

} // namespace tsunami::rcomp
