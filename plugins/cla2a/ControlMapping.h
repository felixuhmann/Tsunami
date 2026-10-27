// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
namespace tsunami::cla2a {
// Physical knob markings; not amplitudes or decibels. Independently written
// formula verified against the genuine host enumeration: it reproduces all
// 929 / 931 distinct display values of a 1000-step normalized sweep and the
// reported defaults (see tests/cla2a/test_mapping.cpp). Not a DSP curve.
inline double gainToNormalized(double knob) noexcept {
    knob = std::clamp(knob, 0.0, 100.0);
    const bool upper = knob > 35.0;
    const double distance = std::abs(knob - 35.0);
    const double fraction = std::log1p(distance / 4.0) / std::log(upper ? 17.25 : 9.75);
    return 0.35 + (upper ? 0.65 : -0.35) * fraction;
}
inline double normalizedToGain(double normalized) noexcept {
    normalized = std::clamp(normalized, 0.0, 1.0);
    const bool upper = normalized > 0.35;
    const double fraction = std::abs(normalized-0.35)/(upper ? 0.65 : 0.35);
    const double distance = 4.0*std::expm1(std::log(upper ? 17.25 : 9.75)*fraction);
    return 35.0 + (upper ? distance : -distance);
}
inline double peakReductionToNormalized(double knob) noexcept {
    knob = std::clamp(knob, 0.0, 100.0);
    const bool upper = knob > 40.0;
    const double fraction = std::log1p(std::abs(knob-40.0)/4.0)/std::log(upper ? 16.0 : 11.0);
    return 0.4+(upper ? 0.6 : -0.4)*fraction;
}
inline double normalizedToPeakReduction(double normalized) noexcept {
    normalized = std::clamp(normalized, 0.0, 1.0);
    const bool upper = normalized > 0.4;
    const double fraction = std::abs(normalized-0.4)/(upper ? 0.6 : 0.4);
    const double distance = 4.0*std::expm1(std::log(upper ? 16.0 : 11.0)*fraction);
    return 40.0+(upper ? distance : -distance);
}
}
