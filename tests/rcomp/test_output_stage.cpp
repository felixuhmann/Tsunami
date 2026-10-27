// SPDX-License-Identifier: GPL-3.0-or-later
#include "OutputStage.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using tsunami::rcomp::OutputStage;
static bool within(float actual, float expected) {
    return std::isfinite(actual) && std::isfinite(expected)
        && std::abs(actual - expected) <= 1.0e-7f;
}
static void close(float actual, float expected, const char* label) {
    if (!within(actual, expected)) {
        std::cerr << label << ": " << actual << " != " << expected << '\n';
        std::exit(1);
    }
}
int main() {
    // The oracle itself must reject nonfinite results instead of passing NaN.
    if (within(NAN, 0.0f) || within(0.0f, NAN) || within(NAN, NAN) || within(INFINITY, INFINITY)) {
        std::cerr << "comparison helper accepts nonfinite values\n";
        return 1;
    }
    std::cout << "PASS comparison helper rejects nonfinite values\n";
    OutputStage stage;
    if (!stage.setGainTrim(6.0f, -3.0f)) return 1;
    // Physical dB gains, checked independently against genuine original renders.
    const float expected = static_cast<float>(0.125 * std::pow(10.0, 3.0 / 20.0));
    close(stage.tick(0.5f, 0.125f), expected, "gain + trim at fully wet");
    close(stage.tick(-0.5f, -0.125f), -expected, "polarity");
    std::cout << "PASS physical gain/trim output path\n";
    if (!stage.setMix(25.0f)) return 1;
    const float mixed = static_cast<float>((0.5 * 0.75
        + 0.125 * std::pow(10.0, 6.0 / 20.0) * 0.25) * std::pow(10.0, -3.0 / 20.0));
    close(stage.tick(0.5f, 0.125f), mixed, "trim applies to BOTH wet and dry");
    if (!stage.setMix(0.0f)) return 1;
    close(stage.tick(0.5f, 0.125f), static_cast<float>(0.5 * std::pow(10.0, -3.0 / 20.0)), "fully dry ignores makeup gain");
    std::cout << "PASS physical mix/trim placement\n";
    // Nontrivial mix/gain/trim so both dry and wet contributions are visible.
    if (!stage.setGainTrim(4.5f, -2.0f) || !stage.setMix(60.0f)) return 1;
    const float dry[4]{0.25f, -0.5f, 0.0f, 0.2f};
    const float wet[4]{0.1f, -0.2f, 0.3f, 0.05f};
    float reference[4], outOfPlace[4], dryAlias[4], wetAlias[4];
    const double g = std::pow(10.0, 4.5 / 20.0), t = std::pow(10.0, -2.0 / 20.0);
    for (unsigned i = 0; i < 4; ++i) {
        reference[i] = stage.tick(dry[i], wet[i]);
        close(reference[i], static_cast<float>((dry[i] * 0.4 + wet[i] * g * 0.6) * t), "analytic block sample");
        dryAlias[i] = dry[i];
        wetAlias[i] = wet[i];
    }
    stage.process(dry, wet, outOfPlace, 4);
    stage.process(dryAlias, wet, dryAlias, 4);
    stage.process(dry, wetAlias, wetAlias, 4);
    for (unsigned i = 0; i < 4; ++i) {
        close(outOfPlace[i], reference[i], "out-of-place block");
        close(dryAlias[i], reference[i], "dry-alias block");
        close(wetAlias[i], reference[i], "wet-alias block");
    }
    stage.process(nullptr, nullptr, nullptr, 0);
    // Rejected updates must leave every active coefficient unchanged.
    const float before = stage.tick(0.25f, 0.1f);
    const bool rejected[]{stage.setMix(-1), stage.setMix(101), stage.setMix(NAN),
        stage.setMix(INFINITY), stage.setGainTrim(31, 0), stage.setGainTrim(0, -19),
        stage.setGainTrim(-31, 0), stage.setGainTrim(0, 19), stage.setGainTrim(NAN, 0),
        stage.setGainTrim(0, NAN), stage.setGainTrim(INFINITY, 0), stage.setGainTrim(6, NAN)};
    for (bool accepted : rejected) {
        if (accepted) return 1;
        close(stage.tick(0.25f, 0.1f), before, "invalid update retains previous controls");
    }
    // Accepted endpoints and defaults.
    OutputStage fresh;
    close(fresh.tick(0.5f, 0.125f), 0.125f, "default fully wet unity");
    if (!fresh.setGainTrim(30, 18) || !fresh.setGainTrim(-30, -18) || !fresh.setMix(0)
        || !fresh.setMix(100)) return 1;
    close(fresh.tick(0.0f, 1.0f), static_cast<float>(std::pow(10.0, -48.0 / 20.0)), "lower endpoints");
    std::cout << "PASS in-place block and atomic invalid control rejection\n";
}
