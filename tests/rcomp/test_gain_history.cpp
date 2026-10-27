// SPDX-License-Identifier: GPL-3.0-or-later
#include "GainHistory.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

void near(float actual, float expected, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-7f) {
        std::cerr << message << ": actual=" << actual << " expected=" << expected << '\n';
        std::exit(1);
    }
}
int main() {
    tsunami::rcomp::GainHistory history;
    if (!history.prepare(2, 2, 3, 1)) return 1;
    // Two rectangular integrations, widths 2 and 4, normalized by 2^3.
    // Synthetic coefficient-domain case, NOT captured full-plugin controls.
    const float gains[] = {0.0625f, 0.1875f, 0.3125f, 0.4375f, 0.5f, 0.5f};
    for (unsigned n = 0; n < 6; ++n) {
        const auto frame = history.tick(0.5f, static_cast<float>(n + 1) / 32.0f);
        near(frame.gain, gains[n], "fixed-point trapezoidal step");
        near(frame.audio, gains[n] * static_cast<float>(n) / 32.0f,
             "smoothed gain applied to delayed audio");
    }
    std::cout << "PASS fixed-point gain history and delayed audio\n";
    history.reset();
    const auto restarted = history.tick(0.5f, 1.0f);
    near(restarted.gain, 0.0625f, "reset clears recursive gain state");
    near(restarted.audio, 0.0f, "reset clears delayed audio");
    std::cout << "PASS reset clears both histories\n";
    if (history.prepare(0, 2, 3, 1)) {
        std::cerr << "accepted zero first span\n"; return 1;
    }
    if (history.prepare(2, 2, 32, 1) || history.prepare(2, 2, 3, 0)
        || history.prepare(2, 2, 3, 3)
        || history.prepare(65536, 65536, 3, 1)
        || history.prepare(2, 2, 3, 65537)) {
        std::cerr << "accepted invalid history geometry\n"; return 1;
    }
    const auto continued = history.tick(0.5f, 0.0f);
    near(continued.gain, 0.1875f, "invalid prepare preserves recursive state");
    near(continued.audio, 0.1875f, "invalid prepare preserves audio history");
    std::cout << "PASS invalid preparation is atomic\n";
    // CVTTSS2SI returns the signed-minimum sentinel at +2^31, NOT saturation.
    // Exercise under float-cast-overflow sanitization as well as ordinary tests.
    for (float endpoint : {1.0f, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
        history.reset();
        near(history.tick(endpoint, 0.0f).gain, -0.125f,
             "x86 integer-indefinite conversion at unity/nonfinite gain");
    }
    history.reset();
    near(history.tick(-0.5f, 0.0f).gain, -0.0625f, "signed arithmetic right shift");
    std::cout << "PASS defined x86 quantization and signed shifts\n";

    // Independent direct FIR summation: no second-order recurrence, shared ring,
    // or vendor data. Quantized input includes signed words and the unity sentinel.
    unsigned cases = 0, samples = 0;
    for (unsigned a : {1u, 2u, 3u, 7u})
    for (unsigned b : {0u, 1u, 2u, 5u})
    for (unsigned delay : {1u, 2u, 4u, 8u})
    for (unsigned shift : {0u, 1u, 3u, 7u, 31u}) {
        if (!history.prepare(a, b, shift, delay)) return 1;
        std::array<std::int64_t, 128> quantized{};
        unsigned seed = 731;
        for (unsigned n = 0; n < 1024; ++n) {
            seed = seed * 1664525u + 1013904223u;
            const int code = static_cast<int>(seed % 17u) - 8;
            const std::int64_t word = code == 8 ? -2147483648LL
                : static_cast<std::int64_t>(code) * 268435456LL;
            quantized[n % quantized.size()] = static_cast<std::int64_t>(
                std::floor(static_cast<double>(word) / std::ldexp(1.0, shift)));
            std::int64_t sum = 0;
            for (unsigned i = 0; i < a; ++i)
                for (unsigned j = 0; j < a + b; ++j)
                    if (n >= i + j) sum += quantized[(n - i - j) % quantized.size()];
            auto reduced = sum % 4294967296LL;
            if (reduced < 0) reduced += 4294967296LL;
            if (reduced >= 2147483648LL) reduced -= 4294967296LL;
            const float expectedGain = static_cast<float>(
                static_cast<double>(reduced) * 4.656612873077e-10);
            const auto signal = [](unsigned sample) {
                return static_cast<float>(sample % 97) / 64.0f - 0.5f;
            };
            const auto frame = history.tick(static_cast<float>(code) / 8.0f, signal(n));
            const float expectedAudio = expectedGain * (n < delay ? 0.0f : signal(n - delay));
            if (frame.gain != expectedGain || frame.audio != expectedAudio) {
                std::cerr << "direct FIR mismatch at sample " << n << '\n'; return 1;
            }
            ++samples;
        }
        ++cases;
    }
    std::cout << "PASS independent direct FIR: " << cases << " geometries, "
              << samples << " exact sample comparisons\n";
}
