// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GainStage.h"
#include "WetPath.h"

namespace tsunami::rcomp {

// Mono wet chain from input audio, in the reference kernel's per-sample order:
// detector/gain law/followers on x[n] -> soft gain and ceiling minimum ->
// history (same sample) -> makeup -> H-delayed x. Coefficient-domain only: the
// physical-control mapping and the ARC-adaptive gain-increase memory are NOT
// included (gainIncrease is a fixed caller coefficient). Not a Processor.
class WetChain {
public:
    // Off-audio-thread. Atomic on failure. Makeup and coefficients accepted
    // before or between prepares are kept (project API lifecycle policy).
    bool prepare(int sampleRateHz) {
        WetPath wet;
        if (!wet.setMakeup(gainDb_) || !wet.prepare(sampleRateHz)) return false;
        wet_ = std::move(wet);
        reset();
        return true;
    }

    bool configure(const GainStage::Coefficients& coefficients) noexcept {
        return stage_.configure(coefficients);
    }

    bool setMakeup(float gainDb) noexcept {
        if (!wet_.setMakeup(gainDb)) return false;
        gainDb_ = gainDb;
        return true;
    }

    // Reference startup: history prefill (WetPath) and the ceiling follower at
    // history-domain unity; gain follower at 1 (0x180028450).
    void reset() noexcept {
        wet_.reset();
        stage_.reset(WetPath::kHistoryUnity);
    }

    float tick(float audio) noexcept {
        return tickAligned(audio).wet;
    }

    WetPath::Aligned tickAligned(float audio) noexcept {
        return wet_.tickHistoryAligned(stage_.tick(audio), audio);
    }

    unsigned latency() const noexcept { return wet_.latency(); }
    const WetPath& wetPath() const noexcept { return wet_; }

private:
    GainStage stage_;
    WetPath wet_;
    float gainDb_ = 0.0f;
};

} // namespace tsunami::rcomp
