// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonoProcessor.h"
#include "CoefficientLaws.h"
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
            GainStage::Coefficients ref = compressing();
            ref.ceiling = static_cast<float>(ceilingForGainDb(6.0));
            check(chain.prepare(rate) && chain.configure(ref) && chain.setMakeup(6.0f),
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
    // Review fixes: the Gain ceiling writer (0x18002c790) also depends on Gain, so
    // setOutput recomputes coefficient 4 with the recovered law; process() before
    // prepare() is a silent no-op output instead of a division by zero.
    {
        MonoProcessor g;
        check(g.prepare(48000) && g.configureWet(compressing()) && g.setOutput(6.0f, 0.0f, 100.0f),
              "gain ceiling setup");
        check(g.wetCoefficients().ceiling == static_cast<float>(ceilingForGainDb(6.0)),
              "setOutput recomputes ceiling from Gain");
        check(g.configureWet(compressing())
              && g.wetCoefficients().ceiling == static_cast<float>(ceilingForGainDb(6.0)),
              "configureWet keeps the Gain-derived ceiling");
        MonoProcessor unprepared;
        std::vector<float> in(64, 0.5f), outv(64, 1.0f);
        unprepared.process(in.data(), outv.data(), 64);
        for (float v : outv) check(v == 0.0f, "unprepared process outputs silence");
    }
    std::cout << "PASS gain ceiling recompute and unprepared guard\n";
    // Lifecycle (project API contract, replacement policy): host wrappers apply
    // controls before prepare(). Accepted controls survive prepare, reprepare and
    // reset; the result equals the same controls applied after prepare().
    {
        std::vector<float> in(3000);
        std::uint32_t seed = 21;
        for (unsigned i = 0; i < in.size(); ++i) in[i] = signal(seed, i);
        const auto render = [&in](MonoProcessor& m) {
            std::vector<float> out(in.size());
            m.process(in.data(), out.data(), static_cast<unsigned>(in.size()));
            return out;
        };
        MonoProcessor after;
        check(after.prepare(96000) && after.configureWet(compressing())
              && after.setOutput(6.0f, -3.0f, 40.0f), "reference after prepare");
        const std::vector<float> expected = render(after);

        MonoProcessor before;
        check(before.setOutput(6.0f, -3.0f, 40.0f), "setOutput accepted before prepare");
        check(before.configureWet(compressing()), "configureWet accepted before prepare");
        check(before.wetCoefficients().ceiling == static_cast<float>(ceilingForGainDb(6.0)),
              "pre-prepare Gain sets the ceiling");
        check(!before.setOutput(31.0f, 0.0f, 0.0f) && !before.setOutput(NAN, 0.0f, 0.0f)
              && !before.setOutput(0.0f, 19.0f, 0.0f), "pre-prepare invalid rejected");
        check(before.prepare(96000), "prepare after controls");
        check(render(before) == expected, "pre-prepare controls survive prepare");

        // Reprepare at another rate then back: identical to a fresh prepare.
        check(before.prepare(44100) && before.prepare(96000), "reprepare");
        check(render(before) == expected, "reprepare keeps accepted controls");
        check(!before.prepare(-1), "invalid reprepare");
        before.reset();
        check(render(before) == expected, "reset/failed prepare keep accepted controls");

        // In-place and zero-frame processing.
        before.reset();
        std::vector<float> inplace = in;
        before.process(inplace.data(), inplace.data(), static_cast<unsigned>(inplace.size()));
        check(inplace == expected, "in-place equals out-of-place");
        before.process(nullptr, nullptr, 0);
        MonoProcessor idle;
        idle.reset();
        idle.process(nullptr, nullptr, 0);
    }
    std::cout << "PASS pre-prepare controls, reprepare/reset preservation, in-place/zero-frame\n";
    return 0;
}
