// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// CHARACTERIZED, NOT RECOVERED. Static gain-reduction curve (dB) versus
// detector peak level (dB, input peak + recovered Peak Reduction drive),
// fitted by tests/cla2a/fit_static_curve.py to genuine stepped 1 kHz tone
// renders (runtime/evidence/cla2a 20261027T161007, 20261027T161031, 20261027T161215;
// baseline 20261027T161139). The default evaluation renders were not
// used. Comp is constrained by data over detector about -34..+28 dB, Limit over
// -34..+8 dB; points outside are linear extrapolation. The 26-point count
// matches the reference kernel; the reference breakpoints are unknown.
// ProgrammeLevelDb: 1 kHz level offset between recovered makeup and the
// measured uncompressed gain (unrecovered programme-path filtering).
#include <array>
namespace tsunami::cla2a::Characterized {
constexpr double DetectorDb0 = -16.0, DetectorStepDb = 2.0;
constexpr double ProgrammeLevelDb = -0.349;
constexpr std::array<double, 26> CompGrDb{0.000, 0.000, 0.000, 0.000, 0.000, -0.571, -1.487, -2.958, -4.587, -5.944, -7.593, -9.266, -10.571, -12.315, -14.090, -14.721, -16.951, -18.654, -19.259, -21.721, -23.375, -23.783, -26.323, -26.666, -28.108, -29.550};
constexpr std::array<double, 26> LimitGrDb{0.000, 0.000, 0.000, 0.000, 0.000, -0.556, -1.581, -3.261, -4.410, -6.634, -8.545, -9.672, -12.446, -13.474, -15.374, -17.275, -19.176, -21.077, -22.978, -24.879, -26.780, -28.681, -30.582, -32.483, -34.383, -36.284};
}
