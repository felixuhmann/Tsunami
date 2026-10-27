// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonoProcessor.h"
#include "OutputStage.h"
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <vector>
using namespace tsunami::rcomp;
static void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
}
static GainStage::Coefficients compressing() {
    GainStage::Coefficients c;
    c.phase = {0.5f, 0.25f, -0.5f};
    c.thresholdScale = 0.25f;
    c.slope = 3.0f;
    c.shape = 1.0f;
    c.gainDecrease = 0.5f;
    c.gainIncrease = 0.99f;
    c.ceiling = 0.5f;
    c.ceilingRelease = 0.999f;
    c.softScale = 0.0625f;
    return c;
}
static float signal(std::uint32_t& seed, unsigned i) {
    seed = seed * 1664525u + 1013904223u;
    const float burst = (i / 131) % 2 ? 1.2f : 0.04f;
    return burst * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
}
int main() {
    // Kernel output: dry is the SAME H-delayed audio the wet path reads; physical
    // Gain lives only in the wet-path makeup; Trim/Mix in the output coefficients.
    for (int rate : {44100, 96000, 192000}) {
        for (float mix : {0.0f, 25.0f, 100.0f}) {
            MonoProcessor p;
            check(p.prepare(rate) && p.configureWet(compressing())
                  && p.setOutput(6.0f, -3.0f, mix), "prepare/configure/output");
            WetChain chain;
            check(chain.prepare(rate) && chain.configure(compressing()) && chain.setMakeup(6.0f),
                  "reference chain");
            OutputStage out;
            check(out.setGainTrim(0.0f, -3.0f) && out.setMix(mix), "reference output");
            std::deque<float> delay(p.latency(), 0.0f);
            std::uint32_t seed = 9;
            std::vector<float> block(257);
            for (int b = 0; b < 12; ++b) {
                std::vector<float> expected(block.size());
                for (unsigned i = 0; i < block.size(); ++i) {
                    block[i] = signal(seed, b * 257u + i);
                    delay.push_back(block[i]);
                    const float dry = delay.front();
                    delay.pop_front();
                    expected[i] = out.tick(dry, chain.tick(block[i]));
                }
                p.process(block.data(), block.data(), static_cast<unsigned>(block.size()));
                for (unsigned i = 0; i < block.size(); ++i)
                    check(block[i] == expected[i], "processor = delayed dry + chain wet");
            }
            check(p.latency() == chain.latency(), "latency is history delay");
        }
    }
    std::cout << "PASS mono processor composition at 3 rate bands x Mix {0,25,100}\n";

    // Mix 0: output is exactly the trimmed H-delayed input (startup silence first).
    MonoProcessor dry;
    check(dry.prepare(48000) && dry.configureWet(compressing()) && dry.setOutput(0.0f, 0.0f, 0.0f),
          "dry");
    std::vector<float> x(1000), y(1000);
    for (unsigned i = 0; i < x.size(); ++i) x[i] = std::sin(0.01f * static_cast<float>(i));
    dry.process(x.data(), y.data(), 1000);
    for (unsigned i = 0; i < y.size(); ++i)
        check(y[i] == (i < dry.latency() ? 0.0f : x[i - dry.latency()]), "mix0 delayed identity");

    // Reset restores startup state: identical output for identical input.
    dry.reset();
    std::vector<float> z(1000);
    dry.process(x.data(), z.data(), 1000);
    check(z == y, "reset reproduces startup");

    // Invalid updates are atomic.
    check(!dry.setOutput(31.0f, 0.0f, 0.0f), "gain range");
    check(!dry.setOutput(0.0f, 19.0f, 0.0f), "trim range");
    check(!dry.setOutput(0.0f, 0.0f, 101.0f), "mix range");
    check(!dry.setOutput(NAN, 0.0f, 0.0f), "nonfinite");
    GainStage::Coefficients bad = compressing();
    bad.softScale = 0.0f;
    check(!dry.configureWet(bad), "invalid wet coefficients");
    check(!dry.prepare(0), "invalid rate");
    dry.reset();
    std::vector<float> w(1000);
    dry.process(x.data(), w.data(), 1000);
    check(w == y, "rejected updates leave processing unchanged");
    std::cout << "PASS mix0 delayed identity, reset, atomic validation\n";
    return 0;
}
