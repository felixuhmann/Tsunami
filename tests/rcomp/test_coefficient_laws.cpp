// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoefficientLaws.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace tsunami::rcomp;
static void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
int main() {
    // One time constant of samples decays by the reference constant 0.367879441.
    const double m = timeConstantMemory(48000, 10.0);
    check(near(std::pow(m, 480.0), 0.367879441, 1e-12), "memory decays to 1/e per time constant");
    check(near(timeConstantMemory(96000, 10.0), std::pow(0.367879441, 1.0 / 960.0), 1e-15),
          "memory scales with sample rate");
    check(timeConstantMemory(48000, 0.0) == 0.0, "zero time gives zero memory");
    check(timeConstantMemory(0, 10.0) == 0.0, "invalid rate gives zero memory");
    // Nonzero flag slot multiplies the time by 5 before the law.
    check(slowedTimeMilliseconds(16.0, false) == 16.0, "flag clear keeps time");
    check(slowedTimeMilliseconds(16.0, true) == 80.0, "flag set multiplies time by 5");
    // Ceiling: (1 / (dbToGain(gain) * 16)) * 0.9999996423721313.
    check(near(ceilingForGainDb(0.0), 0.0625 * 0.9999996423721313, 1e-15), "unity ceiling");
    check(near(ceilingForGainDb(6.0), 0.9999996423721313 / (16.0 * std::pow(10.0, 0.3)), 1e-15),
          "ceiling follows physical gain");
    check(ceilingForGainDb(-30.0) > ceilingForGainDb(0.0), "lower gain raises ceiling");
    // Static gain computer, characterized from genuine stepped-sine renders
    // (thresholds -12/-20/-30 dB): q = T/(m*S + T) with T = 10^(threshold/20),
    // S = 1, and the kernel shape term (coefficient 0.5, kernel adds 1) = 1.5.
    const StaticCurve c = staticCurveForThresholdDb(-20.0);
    check(near(c.thresholdScale, 0.1, 1e-12) && c.slope == 1.0 && c.shape == 1.5,
          "static curve coefficients");
    // At m = threshold, q = 0.5 and the target is 0.5 + 1.5*0.25*0.5 = 0.6875.
    // Soft gain: history gain = follower*scale + 2*offset with scale + 2*offset = 1/16
    // and floor b = 2*offset*16.
    const SoftGain g = softGainForFloor(0.25);
    check(near(g.scale, 0.75 / 16.0, 1e-15) && near(2.0 * g.offset, 0.25 / 16.0, 1e-15),
          "soft gain from floor");
    check(near(g.scale + 2.0 * g.offset, 0.0625, 1e-15), "soft gain unity is 1/16");
    // Attack feeds the memory law directly (b_a05/b_a16/b_a100 renders).
    check(attackMemory(48000, 16.0) == timeConstantMemory(48000, 16.0), "attack memory law");
    // Ratio -> soft-gain floor, measured (threshold-independent) at displayed
    // ratios 1.50/2.00/3.01/4.01/7.99; exact at the measured points.
    check(near(ratioFloor(2.0), 0.148735, 2e-5) && near(ratioFloor(4.01), 0.029841, 2e-5)
          && near(ratioFloor(7.99), 0.006655, 2e-5), "measured ratio floors");
    check(ratioFloor(3.5) < ratioFloor(3.01) && ratioFloor(3.5) > ratioFloor(4.01),
          "floor interpolation monotone");
    std::cout << "rcomp coefficient laws ok\n";
}
