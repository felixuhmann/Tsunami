// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Knob -> decibel control laws RECOVERED from the reference Windows build
// (CLA-2A.dll 17.1.42, sha256 30443d7e...). Source: existing decompile
// export analysis/cla2a/recovery/windows/functions plus the breakpoint
// values read from that DLL's .rdata / literal operands; no new Ghidra run.
//  * gainMakeupDb: FUN_18002fa10 reads control 38 (physical 0..100), divides
//    by 10, clamps to [0,10] and interpolates an 11-point dB table
//    (.rdata 0x18004b560..0x18004b5b0); result -> dbToGain -> one coefficient.
//  * peakReductionDriveDb: FUN_180030d80 reads control 74 (physical 0..100),
//    scales to [0,4] and interpolates a 5-point dB table (stack literals:
//    -50, -33, 15.6, 31, 39); result -> dbToGain -> one coefficient.
//  * autoMakeupDb: FUN_18002f3a0/FUN_18002fa10 key a compensation table on
//    control 74 (.rdata x 0x18004b3f0.., y 0x18004b420..) and add its change
//    to the control-38 dB value when the auto-makeup switch (0xcf) is on.
// Control 38 = Gain and 74 = Peak Reduction is INFERRED from that auto-makeup
// coupling (compensation keyed on 74 is added to 38). Where the two resulting
// coefficients act inside the kernel (output gain / sidechain input gain) is
// a structural assumption: the coefficient-index -> cCoefs offset map was not
// recovered. Only the breakpoint numbers are reproduced here.
#include <algorithm>
#include <array>
namespace tsunami::cla2a {
namespace detail {
template <std::size_t N>
double uniformTable(double position, const std::array<double, N>& y) noexcept {
    if (position <= 0.0) return y.front();
    if (position >= double(N - 1)) return y.back();
    const std::size_t i = std::min(static_cast<std::size_t>(position), N - 2);
    return y[i] + (y[i + 1] - y[i]) * (position - double(i));
}
}
inline double gainMakeupDb(double knob) noexcept {
    static constexpr std::array<double, 11> db{-51.0, -34.6, -17.7, -2.2, 8.8, 17.2,
                                               25.0, 30.0, 34.0, 35.5, 39.0};
    return detail::uniformTable(std::clamp(knob, 0.0, 100.0) / 10.0, db);
}
inline double peakReductionDriveDb(double knob) noexcept {
    static constexpr std::array<double, 5> db{-50.0, -33.0, 15.6, 31.0, 39.0};
    return detail::uniformTable(std::clamp(knob, 0.0, 100.0) / 25.0, db);
}
inline double autoMakeupDb(double peakReduction) noexcept {
    static constexpr std::array<double, 6> x{0.0, 45.12, 49.08, 54.96, 60.12, 100.0};
    static constexpr std::array<double, 6> y{0.0, 0.7, 4.2, 5.0, 7.6, 18.1};
    std::size_t i = 1;
    while (i < x.size() - 1 && x[i] < peakReduction) ++i;
    const double t = (peakReduction - x[i - 1]) / (x[i] - x[i - 1]);
    return (1.0 - t) * y[i - 1] + t * y[i];
}
}
