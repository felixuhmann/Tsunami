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

} // namespace tsunami::rcomp
