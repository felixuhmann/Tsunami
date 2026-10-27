// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GainHistory.h"
#include "HistoryGeometry.h"
#include <utility>

namespace tsunami::rcomp {

// Mono wet path from computed gain to makeup-scaled delayed audio: rate-selected
// history geometry, gain history and the reference kernel's makeup multiply.
// Still partial: the detector/gain computer, soft-gain law, ARC and the
// nonzero startup prefill are NOT included (zero reset, as in GainHistory).
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

    void reset() noexcept { history_.reset(); }

    // computedGain is the unit-domain gain (1 = no reduction). The history stores
    // gain at 1/16 scale, the level of the reference prefill and meter reference.
    float tick(float computedGain, float audio) noexcept {
        const GainHistory::Frame f = history_.tick(computedGain * kHistoryUnity, audio);
        return (f.gain * makeup_) * f.delayed;
    }

    float makeupScale() const noexcept { return makeup_; }
    const HistoryGeometry& geometry() const noexcept { return geometry_; }
    unsigned latency() const noexcept { return geometry_.delay; }

private:
    static constexpr float kHistoryUnity = 0.0625f;

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
