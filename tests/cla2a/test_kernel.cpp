// SPDX-License-Identifier: GPL-3.0-or-later
// Restricted kernel tests: each segment below was identified in the
// unstripped reference kernel and is re-expressed independently. All
// coefficient values used here are SYNTHETIC test values, not device values.
#include "Kernel.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using namespace tsunami::cla2a;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static bool near(double a, double b, double tol = 1e-12) { return std::abs(a - b) <= tol; }

int main() {
    // Noise source: 32-bit wrapping LCG, two draws per sample, sum/difference pair.
    {
        NoiseSource noise;
        noise.reset(0);
        const auto p = noise.next();
        const std::int32_t s1 = static_cast<std::int32_t>(0u * 0x9E3779u + 1u);
        const std::int32_t s2 = static_cast<std::int32_t>(static_cast<std::uint32_t>(s1) * 0x9E3779u + 1u);
        const double a = s1 / 2147483648.0, b = s2 / 2147483648.0;
        check(near(p.first, (a + b) * 0.25, 1e-9) && near(p.second, (a - b) * 0.25, 1e-9), "noise pair");
        check(noise.state() == s2, "noise state advances twice");
        for (int i = 0; i < 100000; ++i) {
            const auto q = noise.next();
            check(std::abs(q.first) <= 0.5 && std::abs(q.second) <= 0.5, "noise bounded");
        }
    }
    // Static gain curve: piecewise-linear breakpoints, clamped at both ends, input floored at 0.
    {
        const std::array<double, 4> x{0.1, 0.2, 0.4, 0.8};
        const std::array<double, 4> y{1.0, 0.9, 0.5, 0.25};
        check(near(interpolateCurve(-1.0, x.data(), y.data(), 4), 1.0), "curve below range");
        check(near(interpolateCurve(0.1, x.data(), y.data(), 4), 1.0), "curve at first knee");
        check(near(interpolateCurve(0.3, x.data(), y.data(), 4), 0.7), "curve interior");
        check(near(interpolateCurve(0.8, x.data(), y.data(), 4), 0.25), "curve at last point");
        check(near(interpolateCurve(5.0, x.data(), y.data(), 4), 0.25), "curve above range");
    }
    // Attack poles: sensitivity-scaled level, clamped to [last breakpoint, 1],
    // looked up in a DESCENDING breakpoint table; weight always 0.75.
    {
        const std::array<double, 3> lv{1.0, 0.5, 0.1};
        const std::array<double, 3> fp{0.9, 0.8, 0.6};
        const std::array<double, 3> sp{0.99, 0.95, 0.9};
        Poles p = attackPoles(0.375, 2.0, lv.data(), fp.data(), sp.data(), 3); // u = 0.75
        check(near(p.fast, 0.85) && near(p.slow, 0.97) && p.weight == 0.75, "attack interpolation");
        p = attackPoles(10.0, 2.0, lv.data(), fp.data(), sp.data(), 3);         // u -> 1
        check(near(p.fast, 0.9) && near(p.slow, 0.99), "attack clamp high");
        p = attackPoles(0.0, 2.0, lv.data(), fp.data(), sp.data(), 3);          // u -> 0.1
        check(near(p.fast, 0.6) && near(p.slow, 0.9), "attack clamp low");
    }
    // Ballistics: attack branch only while target is below the current memory sum.
    {
        Ballistics b;
        const Poles attack{0.5, 0.5, 0.75}, release{0.0, 0.0, 0.75};
        double g = b.advance(0.5, attack, release);   // falling: attack poles
        check(near(g, 0.75), "attack branch selected");
        g = b.advance(1.0, attack, release);          // rising: release poles (instant here)
        check(near(g, 1.0), "release branch selected");
        g = b.advance(1.0, attack, release);          // equal: release branch
        check(near(g, 1.0), "equal target uses release branch");
    }
    // Gain-dependent blend polynomial: clamp to [floor, 1] then cubic.
    {
        const BoostPolynomial poly{0.2, 1.0, 2.0, 3.0, 4.0};
        check(near(poly(0.5), 1.0 + 2.0 * 0.5 + 3.0 * 0.25 + 4.0 * 0.125), "cubic");
        check(near(poly(2.0), 10.0), "upper clamp");
        check(near(poly(0.0), poly(0.2)), "floor clamp");
    }
    // Mains-hum oscillator: rotation with first-order amplitude renormalisation.
    {
        HumOscillator hum;
        const double w = 2.0 * 3.14159265358979323846 * 50.0 / 48000.0;
        hum.setRotation(std::cos(w), std::sin(w));
        hum.setShape(1.0, 0.0);
        hum.reset(1.0, 0.0);
        double peak = 0.0;
        for (int n = 0; n < 48000 * 4; ++n) peak = std::max(peak, std::abs(hum.next()));
        check(std::abs(peak - 1.0) < 1e-3, "hum amplitude stays normalised");
    }
    // Parameter smoother: one-pole toward target.
    {
        Smoother s; s.reset(0.0);
        s.next(1.0, 0.5);
        check(near(s.value(), 0.5), "smoother first step");
        s.next(1.0, 0.5);
        check(near(s.value(), 0.75), "smoother second step");
    }
    std::puts("PASS: restricted kernel segments (synthetic coefficients)");
}
