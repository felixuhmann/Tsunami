// SPDX-License-Identifier: GPL-3.0-or-later
#include "WetPath.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace tsunami::rcomp;
static void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
int main() {
    // Independent model: the kernel multiplies the smoothed history gain by
    // mantissa*2^(exponent+4) (float), then by the H-delayed audio. A unit
    // computed gain enters history as 0.0625, so steady state yields dB->linear(Gain).
    for (int rate : {44100, 96000, 192000}) {
        for (float gainDb : {0.0f, 6.0f, -12.5f, 30.0f, -30.0f}) {
            WetPath wet;
            check(wet.prepare(rate), "prepare");
            check(wet.setMakeup(gainDb), "makeup accepted");
            HistoryGeometry g{};
            historyGeometryForRate(rate, g);
            MakeupSplit m{};
            splitMakeup(gainDb, g, m);
            const float k = static_cast<float>(m.mantissa)
                * static_cast<float>(1 << m.shiftCompensation);
            check(wet.makeupScale() == k, "makeup scale = float mantissa * 2^(exponent+4)");
            GainHistory reference;
            reference.prepare(g.firstSpan, g.middleSpan, g.shift, g.delay);
            // Reference startup (0x180029540/0x180028450): unity-at-1/16 Q31 word in
            // every past gain slot and both recursion states at its steady sum.
            const std::uint32_t product = g.firstSpan * g.secondRectangle;
            reference.prefill(0x08000000u, product << (27 - g.shift));
            std::vector<float> audio;
            std::uint32_t seed = 99;
            const unsigned n = 4 * g.delay + 64;
            double worst = 0.0;
            for (unsigned i = 0; i < n; ++i) {
                seed = seed * 1664525u + 1013904223u;
                const float x = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
                seed = seed * 1664525u + 1013904223u;
                const float unit = i < 2 * g.delay ? static_cast<float>(seed >> 8) / 16777216.0f : 1.0f;
                audio.push_back(x);
                const float out = wet.tick(unit, x);
                const auto f = reference.tick(unit * 0.0625f, x);
                const float delayed = i >= g.delay ? audio[i - g.delay] : 0.0f;
                check(out == (f.gain * k) * delayed, "exact kernel order (smoothed*k)*delayed");
                if (i >= 3 * g.delay + 2) {
                    const double expect = std::pow(10.0, gainDb / 20.0) * delayed;
                    worst = std::max(worst, std::abs(out - expect));
                }
            }
            check(worst <= 2e-6 * std::pow(10.0, gainDb / 20.0), "unity steady state equals physical makeup");
        }
    }
    std::cout << "PASS makeup integration, kernel order, unity steady state at 3 rate bands\n";
    // Startup/reset state: no fade-in. From the first delayed sample the unity
    // computed gain yields exactly float(0.0625*inverseNormalization)*k*x[n-H].
    for (int rate : {44100, 96000, 192000}) {
        WetPath wet;
        check(wet.prepare(rate) && wet.setMakeup(-6.0f), "prepare startup");
        const HistoryGeometry& g = wet.geometry();
        const float smoothed = static_cast<float>(0.0625 * g.inverseNormalization);
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<float> audio;
            for (unsigned i = 0; i < 3 * g.delay; ++i) {
                const float x = std::sin(0.01f * static_cast<float>(i + 1));
                audio.push_back(x);
                const float out = wet.tick(1.0f, x);
                const float delayed = i >= g.delay ? audio[i - g.delay] : 0.0f;
                check(out == (smoothed * wet.makeupScale()) * delayed,
                      "reference prefill gives unity gain from the first sample");
            }
            for (int i = 0; i < 50; ++i) wet.tick(0.0f, 0.3f);
            wet.reset();
        }
    }
    std::cout << "PASS reference startup prefill and reset at 3 rate bands\n";
    WetPath wet;
    check(!wet.prepare(0), "rejects nonpositive rate");
    check(wet.prepare(48000) && wet.setMakeup(3.0f), "48k");
    const float kept = wet.makeupScale();
    check(!wet.setMakeup(30.5f) && !wet.setMakeup(NAN) && wet.makeupScale() == kept,
          "invalid makeup atomic");
    for (int i = 0; i < 300; ++i) wet.tick(1.0f, 0.5f);
    wet.reset();
    check(wet.tick(1.0f, 0.5f) == 0.0f, "reset clears history and delay line");
    std::cout << "PASS validation, atomic rejection, reset\n";
    return 0;
}
