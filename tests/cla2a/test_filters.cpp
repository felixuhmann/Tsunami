// SPDX-License-Identifier: GPL-3.0-or-later
// Recovered 48 kHz sidechain biquad cascade (provenance: plugins/cla2a/SidechainFilters.h).
// Expected coefficients were computed independently (python, numpy) from the
// same decompiled prototype + bilinear(prewarp 50 Hz) description.
#include "SidechainFilters.h"
#include <cmath>
#include <complex>
#include <cstdio>
using namespace tsunami::cla2a;
static bool near(double a, double b, double tol) { return std::abs(a - b) < tol; }
static double cascadeDb(const std::array<Biquad, 3>& c, double f, double fs) {
    const std::complex<double> z = std::polar(1.0, -2.0 * 3.14159265358979323846 * f / fs);
    std::complex<double> h = 1.0;
    for (const auto& q : c) h *= (q.b0 + q.b1 * z + q.b2 * z * z) / (1.0 + q.a1 * z + q.a2 * z * z);
    return 20.0 * std::log10(std::abs(h));
}
int main() {
    const auto c = designSidechainCascade(48000.0);
    const double expect[3][5] = {
        {1.0086739231076385, -1.9843597187846038, 0.9757344116060949, -1.9843534248348689, 0.9844146286634684},
        {1.478416038793153, -0.5231608849285964, -0.3382959279228845, -0.41188068370368214, 0.014796203105970006},
        {1.0067981575620624, -1.8036914713811187, 0.8048741200306602, -1.7829991080734997, 0.7909799142851032}};
    for (int i = 0; i < 3; ++i) {
        const double got[5] = {c[i].b0, c[i].b1, c[i].b2, c[i].a1, c[i].a2};
        for (int k = 0; k < 5; ++k)
            if (!near(got[k], expect[i][k], 1e-9)) { std::printf("FAIL biquad %d coef %d: %.17g\n", i, k, got[k]); return 1; }
    }
    if (!near(cascadeDb(c, 63, 48000), 6.698, 2e-3) || !near(cascadeDb(c, 1000, 48000), 0.557, 2e-3) ||
        !near(cascadeDb(c, 10000, 48000), 5.376, 2e-3)) { std::puts("FAIL cascade response"); return 2; }
    // Running the cascade on a 1 kHz sine reproduces the 1 kHz gain.
    SidechainCascade run; run.set(c);
    double energy = 0;  // RMS over whole periods (sampled peaks miss the true peak)
    for (int n = 0; n < 48000; ++n) {
        const double y = run.process(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * n / 48000.0));
        if (n >= 24000) energy += y * y;
    }
    if (!near(10.0 * std::log10(energy / 24000.0 * 2.0), 0.557, 5e-3)) { std::puts("FAIL cascade run"); return 3; }
    return 0;
}
