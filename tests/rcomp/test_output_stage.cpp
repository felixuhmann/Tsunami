// SPDX-License-Identifier: GPL-3.0-or-later
#include "OutputStage.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using tsunami::rcomp::OutputStage;
static void close(float actual, float expected, const char* label) {
    if (std::abs(actual - expected) > 1.0e-7f) {
        std::cerr << label << ": " << actual << " != " << expected << '\n';
        std::exit(1);
    }
}
int main() {
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
    const float dry[4]{0.25f, -0.5f, 0.0f, 0.2f};
    float wet[4]{0.1f, -0.2f, 0.0f, 0.05f};
    float reference[4];
    for (unsigned i = 0; i < 4; ++i) reference[i] = stage.tick(dry[i], wet[i]);
    stage.process(dry, wet, wet, 4);
    for (unsigned i = 0; i < 4; ++i) close(wet[i], reference[i], "in-place wet block");
    stage.process(nullptr, nullptr, nullptr, 0);
    const float before = stage.tick(0.25f, 0.1f);
    if (stage.setMix(-1) || stage.setMix(101) || stage.setGainTrim(31, 0)
        || stage.setGainTrim(0, -19) || stage.setMix(INFINITY)
        || stage.setGainTrim(NAN, 0) || stage.setGainTrim(0, NAN)) return 1;
    close(stage.tick(0.25f, 0.1f), before, "invalid update retains previous controls");
    std::cout << "PASS in-place block and atomic invalid control rejection\n";
}
