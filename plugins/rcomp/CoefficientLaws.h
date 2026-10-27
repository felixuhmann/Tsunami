// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

namespace tsunami::rcomp {

// Coefficient laws recovered from the reference coefficient writers (PE RComp.dll
// sha256 4ecaafa3..., private windows-coefficients exports). Only the laws are
// recovered here. Which physical control feeds the time slots of 0x18002c8b0,
// 0x18002d110 and 0x18002d250 is NOT resolved (parameter-block slots +0x7eb0,
// +0x54b0, +0x3cb0 have no recovered control identity), so these functions are
// deliberately named by law, not by Attack/Release.

// Shared one-pole memory law of 0x18002c8b0/0x18002d110/0x18002d250:
// pow(0.367879441, 1 / (sampleRate * milliseconds * 0.001)).
// Non-positive inputs return 0 (no memory); the reference does not guard these.
inline double timeConstantMemory(int sampleRate, double milliseconds) noexcept {
    if (sampleRate <= 0 || !(milliseconds > 0.0) || !std::isfinite(milliseconds)) return 0.0;
    return std::pow(0.367879441,
                    1.0 / (static_cast<double>(sampleRate) * milliseconds * 0.001));
}

// 0x18002c8b0: the time slot is multiplied by 5 when the flag slot (+0x66b0,
// identity unresolved) is nonzero, before the memory law.
inline double slowedTimeMilliseconds(double milliseconds, bool flagSet) noexcept {
    return flagSet ? milliseconds * 5.0 : milliseconds;
}

// 0x18002c790 (coefficient 4): peak ceiling from the physical Gain slot (+0xcb0,
// the same slot 0x18002cef0 uses for output makeup):
// (1 / (dbToGain(gain) * 16)) * 0.9999996423721313.
// dbToGain is taken as 10^(dB/20), as in the existing makeup port.
inline double ceilingForGainDb(double gainDb) noexcept {
    return (1.0 / (std::pow(10.0, gainDb / 20.0) * 16.0)) * 0.9999996423721313;
}

// Attack -> attack memory (the kernel's gain-decrease memory). Characterized from
// genuine tone-burst renders (Threshold -30, Ratio 8, Attack 0.5/16/100 ms,
// Manual release): measured decay time constants 0.62/15.5/100.0 ms, i.e. the
// Attack value enters the shared memory law directly (no x5 flag).
inline double attackMemory(int sampleRate, double attackMs) noexcept {
    return timeConstantMemory(sampleRate, attackMs);
}

// Static gain computer of the kernel: q = T/(m*S + T),
// target = min(1, q + shape*q^2*(1-q)), shape = coefficient + 1.
// Characterized from genuine stepped-sine renders at Threshold -12/-20/-30 dB
// (Ratio 2/3/8, Smooth, Manual): T/S = 10^(threshold/20) exactly, shape 1.5.
// Only the ratio T/S is observable; S = 1 is a normalization choice.
struct StaticCurve { double thresholdScale; double slope; double shape; };
inline StaticCurve staticCurveForThresholdDb(double thresholdDb) noexcept {
    return {std::pow(10.0, thresholdDb / 20.0), 1.0, 1.5};
}

// Kernel soft gain (history domain): follower*scale + 2*offset, with
// scale + 2*offset = 1/16 (unity). The renders show a level-independent gain
// floor b: gain = (1-b)*follower + b, so scale = (1-b)/16, 2*offset = b/16.
struct SoftGain { double scale; double offset; };
inline SoftGain softGainForFloor(double floor) noexcept {
    return {(1.0 - floor) / 16.0, floor / 32.0};
}

// Ratio -> soft-gain floor b. MEASURED, not a recovered formula: the floor was
// extracted from genuine stepped-sine renders and is independent of Threshold
// (identical b at -12/-20/-30 dB for Ratio 2 and 8). Exact at the measured
// displayed ratios; log-log interpolation between them and b(1) = 1 below 1.5
// are PROVISIONAL. Ratios above 7.99 clamp (unmeasured).
inline double ratioFloor(double ratio) noexcept {
    static constexpr double r[] = {1.0, 1.5, 2.0, 3.01, 4.01, 7.99};
    static constexpr double b[] = {1.0, 0.313986, 0.148735, 0.056305, 0.029841, 0.006655};
    if (!(ratio > r[0])) return 1.0;
    if (ratio >= r[5]) return b[5];
    unsigned i = 1;
    while (ratio > r[i]) ++i;
    const double t = std::log(ratio / r[i - 1]) / std::log(r[i] / r[i - 1]);
    return std::exp(std::log(b[i - 1]) + t * (std::log(b[i]) - std::log(b[i - 1])));
}

} // namespace tsunami::rcomp
