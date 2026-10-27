// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the release-table builder/lookup (reference builder 0x18002d7d0, kernel lookup 0x101100).
// Expected values are computed independently from the recovered formulas in
// docs/evidence/rcomp/r2t4-kernel-findings.json.
#include "ReleaseTables.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace tsunami::rcomp;
static void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
static bool near(double a, double b) { return std::abs(a - b) <= 1e-12 * std::max(1.0, std::abs(b)); }
int main() {
    const double fs = 48000.0, R = 100.0;
    ReleaseTableSettings s{fs, R, ReleaseCurve::C10, false, false};
    ReleaseTables t = buildReleaseTables(s);
    // A_j = 0.5^(d/(j+1)), d = c^2*0.01 (c=10 -> d=1).
    check(near(t.a(0), 0.5), "A[0] c=10");
    check(near(t.a(255), std::pow(0.5, 1.0 / 256.0)), "A[255] c=10");
    check(near(t.a(9), std::pow(0.5, 0.1)), "A[9] c=10");
    // B_j with x_j=j: time = max(0.002,(1-(j/256)^4)^4*R) ms.
    auto bExpected = [&](int j, double x, double c) {
        const double d = c * c * 0.01;
        const double aj = std::pow(0.5, d / (j + 1));
        const double u = x / 256.0;
        const double w = std::pow(1.0 - u * u * u * u, 4.0) * R;
        return std::exp(-1.0 / (std::max(0.002, w) * fs * 0.001)) / aj;
    };
    for (int j : {0, 1, 64, 128, 200, 255})
        check(near(t.b(j), bExpected(j, j, 10.0)), "B[j] forward");
    for (int j = 1; j < 256; ++j) {
        check(t.a(j) > t.a(j - 1), "A strictly increasing");
        check(t.b(j) * t.a(j) <= t.b(j - 1) * t.a(j - 1) * (1.0 + 1e-14), "A*B (pole) non-increasing forward");
    }
    // Curve switch c=3 -> d=0.09.
    ReleaseTables t3 = buildReleaseTables({fs, R, ReleaseCurve::C3, false, false});
    check(near(t3.a(0), std::pow(0.5, 0.09)), "A[0] c=3");
    check(near(t3.b(7), bExpected(7, 7, 3.0)), "B[7] c=3");
    // Reversed index switch: x_j = 256-j.
    ReleaseTables tr = buildReleaseTables({fs, R, ReleaseCurve::C10, true, false});
    for (int j : {0, 1, 128, 255})
        check(near(tr.b(j), bExpected(j, 256 - j, 10.0)), "B[j] reversed");
    check(near(tr.b(0) * tr.a(0), std::exp(-1.0 / (0.002 * fs * 0.001))), "reversed j=0 hits 0.002 ms floor");
    // Non-native build halves B.
    ReleaseTables th = buildReleaseTables({fs, R, ReleaseCurve::C10, false, true});
    check(near(th.b(50), 0.5 * t.b(50)), "halved B");
    // Lookup: T[(int)((s11+1)*k+1)] * T[(int)(min(s10,s11)*k)], T = A then B.
    const int k = 255;
    for (double s10 : {0.0, 0.2, 0.73, 1.0})
        for (double s11 : {0.0, 0.31, 0.5, 0.99}) {
            const int i1 = static_cast<int>((s11 + 1.0) * k + 1.0);
            const int i2 = static_cast<int>(std::min(s10, s11) * k);
            check(i1 >= 256 && i2 < 256, "k=255 index ranges");
            check(t.lookup(s10, s11, k) == t.b(i1 - 256) * t.a(i2), "lookup = B*A");
            check(t.at(i1) == t.b(i1 - 256) && t.at(i2) == t.a(i2), "flat T layout A then B");
        }
    // Slow follower (kernel 0x101100 lines ~313-324): s11' = (s11 - s10) * c + s10,
    // c = slot 0x2c when s10 < s11, else slot 0x30; memory uses the updated s11'.
    {
        const double cBelow = 0.9, cAbove = 0.3;
        ReleaseFollowerState st{0.8};
        const double m1 = releaseStep(t, st, 0.2, cBelow, cAbove, k);
        const double e1 = (0.8 - 0.2) * cBelow + 0.2;
        check(near(st.s11, e1), "follower uses 0x2c coef when s10 < s11");
        check(m1 == t.lookup(0.2, e1, k), "step memory = lookup(s10, s11')");
        st.s11 = 0.1;
        const double m2 = releaseStep(t, st, 0.6, cBelow, cAbove, k);
        const double e2 = (0.1 - 0.6) * cAbove + 0.6;
        check(near(st.s11, e2), "follower uses 0x30 coef when s10 >= s11");
        check(m2 == t.lookup(0.6, e2, k), "step memory rising");
        st.s11 = 0.5;
        releaseStep(t, st, 0.5, cBelow, cAbove, k);
        check(st.s11 == 0.5, "equal input is a fixed point");
    }
    std::cout << "release tables ok\n";
    return 0;
}
