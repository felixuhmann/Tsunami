// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// RECOVERED design of the three-biquad cascade built by the reference
// Windows build (CLA-2A.dll 17.1.42): FUN_180031a10 fills three analog
// second-order prototypes from literal constants (sample-rate branches
// <46001 / 46001..64000 / >120000 Hz; 64001..120000 reads hidden controls)
// and converts each with FUN_1800094a0, a bilinear transform prewarped at
// 50 Hz. FUN_180030480 stores the 15 coefficients and chains the three
// filters (ChainBufs). Source: existing decompile export; constants decoded
// from the literal operands; no new Ghidra run. Only the numbers are
// reproduced; the code is independently written.
//
// Prototype: H(s) = (gA + gB*s/(w*Q) + gC*s^2/w^2) / (1 + s/(w*Q) + s^2/w^2),
// g* = dbToGain(*), w = 2*pi*f.
//
// PLACEMENT: a genuine PR 0 multitone render (evidence 20261027T161747)
// shows a flat programme path (+-0.1 dB 40 Hz..2.5 kHz), whereas this
// cascade varies by ~6 dB; it is therefore applied to the detector
// (sidechain) input. The placement is INFERRED from that render, not read
// from the coefficient-id map (unrecovered). Rates 64001..120000 Hz use
// hidden-control values that are not in the exports: the 48 kHz set is
// used there as a PROVISIONAL stand-in.
#include <array>
#include <cmath>
namespace tsunami::cla2a {
struct Biquad { double b0, b1, b2, a1, a2; };
struct ShelfPrototype { double hz, q, lowDb, midDb, highDb; };

inline Biquad bilinearPrewarped(const ShelfPrototype& p, double fs, double prewarpHz = 50.0) noexcept {
    constexpr double twoPi = 6.28318530717958;
    const auto g = [](double db) { return std::pow(10.0, db / 20.0); };
    const double w = p.hz * twoPi;
    const double n0 = g(p.lowDb), n1 = g(p.midDb) / (w * p.q), n2 = g(p.highDb) / (w * w);
    const double d0 = 1.0, d1 = 1.0 / (w * p.q), d2 = 1.0 / (w * w);
    const double k = prewarpHz * twoPi / std::tan(prewarpHz * twoPi / fs * 0.5);
    const double kk = k * k;
    const double a0 = d0 + d1 * k + d2 * kk;
    return {(n0 + n1 * k + n2 * kk) / a0, (2.0 * n0 - 2.0 * n2 * kk) / a0, (n0 - n1 * k + n2 * kk) / a0,
            (2.0 * d0 - 2.0 * d2 * kk) / a0, (d0 - d1 * k + d2 * kk) / a0};
}

inline std::array<ShelfPrototype, 3> sidechainPrototypes(double fs) noexcept {
    const int rate = static_cast<int>(fs);
    ShelfPrototype second{};
    if (rate > 120000) second = {9560.6196, 0.54459, 0.2, 4.951, -23.1224};
    else if (rate < 46001) second = {9993.7179, 0.46242, 0.2, 5.307, 1.9612};
    else second = {9932.4236, 0.47069, 0.2, 5.3152, 1.3328};
    return {{{60.0, 0.5, -2.0, 6.5, 0.0}, second, {722.0, 0.404, 0.0, -0.3, 0.1}}};
}

inline std::array<Biquad, 3> designSidechainCascade(double fs) noexcept {
    const auto p = sidechainPrototypes(fs);
    return {bilinearPrewarped(p[0], fs), bilinearPrewarped(p[1], fs), bilinearPrewarped(p[2], fs)};
}

// Direct-form-I cascade runner.
class SidechainCascade {
public:
    void set(const std::array<Biquad, 3>& c) noexcept { c_ = c; reset(); }
    void reset() noexcept { s_ = {}; }
    double process(double x) noexcept {
        for (unsigned i = 0; i < 3; ++i) {
            auto& s = s_[i];
            const auto& q = c_[i];
            const double y = q.b0 * x + q.b1 * s[0] + q.b2 * s[1] - q.a1 * s[2] - q.a2 * s[3];
            s = {x, s[0], y, s[2]};
            x = y;
        }
        return x;
    }
private:
    std::array<Biquad, 3> c_{};
    std::array<std::array<double, 4>, 3> s_{};
};
}
