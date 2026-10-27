// SPDX-License-Identifier: GPL-3.0-or-later
#include "WetChain.h"
#include <cmath>
#include <cstdlib>
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
    c.softOffset = 0.0f;
    return c;
}
int main() {
    // Composition from input audio: the detector sees x[n] while the gain it
    // produces enters the history in the same sample; audio is read H later.
    for (int rate : {44100, 96000, 192000}) {
        WetChain chain;
        check(chain.prepare(rate) && chain.configure(compressing()) && chain.setMakeup(4.0f),
              "prepare/configure");
        GainStage stage;
        check(stage.configure(compressing()), "stage");
        stage.reset(0.0625f);
        WetPath wet;
        check(wet.prepare(rate) && wet.setMakeup(4.0f), "wet");
        std::uint32_t seed = 5;
        for (int pass = 0; pass < 2; ++pass) {
            for (unsigned i = 0; i < 6 * wet.latency(); ++i) {
                seed = seed * 1664525u + 1013904223u;
                const float burst = (i / 97) % 2 ? 1.5f : 0.05f;
                const float x = burst * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
                const float expected = wet.tickHistory(stage.tick(x), x);
                check(chain.tick(x) == expected, "chain = stage gain into same-sample history");
            }
            chain.reset();
            stage.reset(0.0625f);
            wet.reset();
        }
        check(chain.latency() == wet.latency(), "latency is history delay");
    }
    std::cout << "PASS input-audio wet chain composition, reset at 3 rate bands\n";

    // tickHistory takes the kernel history-domain gain: tick(g) == tickHistory(g/16).
    WetPath a, b;
    check(a.prepare(48000) && b.prepare(48000), "48k");
    for (int i = 0; i < 400; ++i) {
        const float g = (i % 50) / 49.0f, x = std::sin(0.03f * static_cast<float>(i));
        check(a.tick(g, x) == b.tickHistory(g * 0.0625f, x), "history-domain entry");
    }

    // Below the knee (zero slope) with history-domain unity soft gain the chain
    // is the unity wet path: makeup times H-delayed audio.
    WetChain quiet;
    GainStage::Coefficients c = compressing();
    c.slope = 0.0f;
    c.ceiling = 4.0f;
    check(quiet.prepare(48000) && quiet.configure(c) && quiet.setMakeup(0.0f), "quiet");
    WetPath unity;
    check(unity.prepare(48000), "unity");
    for (int i = 0; i < 500; ++i) {
        const float x = 0.5f * std::sin(0.02f * static_cast<float>(i));
        check(quiet.tick(x) == unity.tick(1.0f, x), "no reduction below knee");
    }

    // Loud input is reduced relative to the unity path once delayed audio arrives.
    WetChain loud;
    check(loud.prepare(48000) && loud.configure(compressing()), "loud");
    WetPath ref;
    check(ref.prepare(48000), "ref");
    double reduced = 0.0, plain = 0.0;
    for (int i = 0; i < 2000; ++i) {
        const float x = 0.9f * std::sin(0.05f * static_cast<float>(i));
        reduced += std::abs(loud.tick(x));
        plain += std::abs(ref.tick(1.0f, x));
    }
    check(reduced < 0.5 * plain, "compression reduces wet level");

    GainStage::Coefficients bad = compressing();
    bad.softScale = -1.0f;
    check(!loud.configure(bad), "invalid coefficients rejected");
    check(!loud.prepare(0), "invalid rate rejected");
    std::cout << "PASS history-domain entry, below-knee identity, reduction, validation\n";

    // Lifecycle (project API policy): makeup set before prepare() is kept and
    // applied by prepare(); unprepared ticks are silent instead of indexing an
    // empty history. Invalid pre-prepare makeup is rejected without mutation.
    {
        WetPath early;
        check(early.tick(1.0f, 0.5f) == 0.0f, "unprepared WetPath tick is silent");
        check(!early.setMakeup(31.0f) && !early.setMakeup(NAN), "pre-prepare invalid makeup");
        check(early.setMakeup(4.0f), "pre-prepare WetPath makeup accepted");
        WetPath late;
        check(early.prepare(96000) && late.prepare(96000) && late.setMakeup(4.0f), "wet prep");
        check(early.makeupScale() == late.makeupScale(), "pre-prepare makeup applied");

        WetChain before;
        const WetPath::Aligned silent = before.tickAligned(0.7f);
        check(silent.wet == 0.0f && silent.dry == 0.0f, "unprepared WetChain tick is silent");
        check(before.setMakeup(4.0f) && before.configure(compressing()), "pre-prepare chain");
        check(!before.setMakeup(-31.0f), "pre-prepare chain invalid makeup");
        WetChain after;
        check(before.prepare(48000) && after.prepare(48000) && after.configure(compressing())
              && after.setMakeup(4.0f), "chain prep");
        check(before.wetPath().makeupScale() == after.wetPath().makeupScale(),
              "pre-prepare chain makeup applied");
        for (int i = 0; i < 800; ++i) {
            const float x = 0.9f * std::sin(0.05f * static_cast<float>(i));
            check(before.tick(x) == after.tick(x), "pre-prepare chain equals post-prepare");
        }
    }
    std::cout << "PASS pre-prepare makeup retention and unprepared silence\n";
    return 0;
}
