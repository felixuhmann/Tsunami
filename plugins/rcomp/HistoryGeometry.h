// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

namespace tsunami::rcomp {

// Sample-rate dependent history geometry, startup prefill layout and makeup
// normalization of the reference wrapper (RComp 17.1.42.50, mono). Partial
// state API only: it does NOT connect GainHistory, detector or ARC stages, and
// the storage format of the prefill writer is unresolved.
struct HistoryGeometry {
    unsigned delay = 0;            // H: 64, 128 or 256
    unsigned delayLog2 = 0;
    unsigned firstSpan = 0;        // A: first rectangle length
    unsigned secondRectangle = 0;  // H - A + 1
    unsigned middleSpan = 0;       // GainHistory middleSpan = second - A
    unsigned shift = 0;            // ceil(log2(A * second))
    double normalization = 0.0;    // 2^shift / (A * second)
    double inverseNormalization = 0.0;

    // Shared history length (outputs + 2) * H + 1; mono gives 2H+2A+B = 3H+1.
    unsigned sharedLength(unsigned outputs) const noexcept {
        return (outputs + 2) * delay + 1;
    }
};

// The reference stores the rate as an integer and selects bands at 64000 and
// 128000 Hz. Nonpositive rates are rejected (replacement policy) without mutation.
inline bool historyGeometryForRate(int sampleRateHz, HistoryGeometry& out) noexcept {
    if (sampleRateHz <= 0) return false;
    HistoryGeometry g;
    if (sampleRateHz >= 128000) {
        g.delay = 256;
        g.firstSpan = static_cast<unsigned>(std::floor(108.532));
    } else if (sampleRateHz >= 64000) {
        g.delay = 128;
        g.firstSpan = static_cast<unsigned>(std::floor(54.516));
    } else {
        g.delay = 64;
        g.firstSpan = 27;
    }
    g.secondRectangle = g.delay - g.firstSpan + 1;
    g.middleSpan = g.secondRectangle - g.firstSpan;
    for (unsigned h = g.delay; h > 1; h >>= 1) ++g.delayLog2;
    const unsigned product = g.firstSpan * g.secondRectangle;
    // Single-precision log as in the reference, then a double ceiling.
    const float logProduct = std::log(static_cast<float>(product));
    g.shift = static_cast<unsigned>(std::ceil(static_cast<double>(logProduct) / std::log(2.0)));
    const double power = std::ldexp(1.0, static_cast<int>(g.shift));
    g.normalization = power / static_cast<double>(product);
    g.inverseNormalization = static_cast<double>(product) / power;
    out = g;
    return true;
}

// Startup contents of the shared history: [0,H) zero, [H,2H+1) constant 0.0625,
// zero thereafter. The constant is a coefficient-domain value whose stored
// fixed/float representation is not yet resolved.
struct PrefillLayout {
    unsigned zeroHead;
    unsigned constantStart;
    unsigned constantCount;
    double constant;
};

inline PrefillLayout prefillLayout(const HistoryGeometry& g) noexcept {
    return {g.delay, g.delay, g.delay + 1, 0.0625};
}

// Physical makeup Gain folded with the history normalization and split into a
// mantissa <= 1 and power-of-two exponent; historyScale is the paired
// normalization coefficient. dB->linear assumes 20*log10 amplitude convention.
struct MakeupSplit {
    double scaled = 0.0;          // gain * normalization
    double scaledTimes16 = 0.0;
    double mantissa = 0.0;
    int exponent = 0;             // mantissa * 2^exponent == scaled
    int shiftCompensation = 0;    // exponent + 4
    double historyScale = 0.0;    // inverseNormalization * 0.0625
};

inline bool splitMakeup(float gainDb, const HistoryGeometry& g, MakeupSplit& out) noexcept {
    if (!std::isfinite(gainDb) || gainDb < -30.0f || gainDb > 30.0f || g.delay == 0) return false;
    MakeupSplit m;
    m.scaled = std::pow(10.0, static_cast<double>(gainDb) / 20.0) * g.normalization;
    m.scaledTimes16 = m.scaled * 16.0;
    m.mantissa = m.scaled;
    while (m.mantissa > 1.0) {
        m.mantissa *= 0.5;
        ++m.exponent;
    }
    if (m.exponent == 0) {
        m.mantissa *= 0.5;
        m.exponent = 1;
    }
    m.shiftCompensation = m.exponent + 4;
    m.historyScale = g.inverseNormalization * 0.0625;
    out = m;
    return true;
}

} // namespace tsunami::rcomp
