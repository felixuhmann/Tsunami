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
    std::cout << "rcomp coefficient laws ok\n";
}
