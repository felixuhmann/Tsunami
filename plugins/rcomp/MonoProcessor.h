// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CoefficientLaws.h"
#include "OutputStage.h"
#include "WetChain.h"

namespace tsunami::rcomp {

// Connected mono signal path in the reference kernel's per-sample order:
// WetChain (detector -> soft gain/ceiling -> history -> makeup) and the static
// physical output mix, whose dry input is the same H-delayed audio. Physical
// Gain enters through the wet-path makeup (0x18002cef0); Trim/Mix through
// the output coefficients (0x18002e0d0). Gain ALSO sets the wet peak ceiling
// (coefficient 4, 0x18002c790), recomputed here on every Gain change. PARTIAL: wet coefficients are in the
// GainStage coefficient domain; the Threshold/Ratio/Attack/Release/ARC mapping
// is NOT recovered, there is no automation smoothing, stereo or mode handling.
class MonoProcessor {
public:
    // Off-audio-thread. Atomic on failure.
    bool prepare(int sampleRateHz) {
        if (!chain_.prepare(sampleRateHz)) return false;
        prepared_ = true;
        return true;
    }

    // The ceiling field is overridden by the Gain-derived ceiling law.
    bool configureWet(const GainStage::Coefficients& coefficients) noexcept {
        GainStage::Coefficients candidate = coefficients;
        candidate.ceiling = static_cast<float>(ceilingForGainDb(gainDb_));
        if (!chain_.configure(candidate)) return false;
        wet_ = candidate;
        return true;
    }

    const GainStage::Coefficients& wetCoefficients() const noexcept { return wet_; }

    // Physical Gain dB [-30,30], Trim dB [-18,18], Mix % [0,100]. Atomic on failure.
    // Valid before prepare(); accepted controls persist across prepare/reset.
    bool setOutput(float gainDb, float trimDb, float mixPercent) noexcept {
        OutputStage candidate = output_;
        if (!candidate.setGainTrim(0.0f, trimDb) || !candidate.setMix(mixPercent)) return false;
        if (!std::isfinite(gainDb) || gainDb < -30.0f || gainDb > 30.0f) return false;
        GainStage::Coefficients wet = wet_;
        wet.ceiling = static_cast<float>(ceilingForGainDb(gainDb));
        if (!chain_.configure(wet)) return false;
        if (!chain_.setMakeup(gainDb)) { chain_.configure(wet_); return false; }
        wet_ = wet;
        gainDb_ = gainDb;
        output_ = candidate;
        return true;
    }

    void reset() noexcept { chain_.reset(); }

    // In-place allowed. Allocation-, lock- and I/O-free.
    void process(const float* input, float* output, unsigned frames) noexcept {
        if (!prepared_) {
            for (unsigned i = 0; i < frames; ++i) output[i] = 0.0f;
            return;
        }
        for (unsigned i = 0; i < frames; ++i) {
            const WetPath::Aligned a = chain_.tickAligned(input[i]);
            output[i] = output_.tick(a.dry, a.wet);
        }
    }

    unsigned latency() const noexcept { return chain_.latency(); }

private:
    WetChain chain_;
    OutputStage output_;
    GainStage::Coefficients wet_{};
    float gainDb_ = 0.0f;
    bool prepared_ = false;
};

} // namespace tsunami::rcomp
