// SPDX-License-Identifier: GPL-3.0-or-later
#include "GainStage.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

void near(float actual, float expected, float tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << message << ": actual=" << actual << " expected=" << expected << '\n';
        std::exit(1);
    }
}

int main() {
    // Coefficient-domain law recovered from the ELF ProcessFuncM kernel.
    // q=threshold/(level*slope+threshold); gain=min(1,q+shape*q*q*(1-q)).
    using tsunami::rcomp::GainStage;
    near(GainStage::targetGain(0.0f, 0.25f, 3.0f, 1.0f), 1.0f, 0.0f, "silence");
    near(GainStage::targetGain(0.25f, 0.25f, 3.0f, 1.0f), 0.296875f, 1e-7f, "nonlinear knee");
    near(GainStage::targetGain(0.75f, 0.25f, 3.0f, 1.0f), 0.109f, 1e-7f, "above knee");
    near(GainStage::targetGain(0.25f, 0.25f, 0.0f, 1.0f), 1.0f, 0.0f, "zero slope");
    near(GainStage::targetGain(0.25f, 0.25f, 1.0f, 8.0f), 1.0f, 0.0f, "upper clamp");
    std::cout << "PASS nonlinear coefficient-domain gain law\n";

    // End-to-end impulse through parallel phase detectors, nonlinear law and
    // asymmetric smoothing. Fixed coefficient domain, not physical ratio units.
    GainStage stage;
    GainStage::Coefficients c;
    c.phase = {0.5f, 0.25f, -0.5f};
    c.thresholdScale = 1.0f;
    c.slope = 1.0f;
    c.shape = 0.0f;
    c.gainDecrease = 0.25f;
    c.gainIncrease = 0.5f;
    if (!stage.configure(c)) return 1;
    const float input[] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float magnitudes[] = {1.0f, 0.9375f, 0.375f, 0.1875f};
    float expected = 1.0f;
    for (unsigned i = 0; i < 4; ++i) {
        const float target = 1.0f / (1.0f + magnitudes[i]);
        expected = target + (expected - target) * (expected <= target ? 0.5f : 0.25f);
        near(stage.tick(input[i]), expected, 1e-7f, "phase detector and gain follower");
    }
    stage.reset();
    near(stage.tick(1.0f), 0.625f, 1e-7f, "reset restores detector and unity gain");
    std::cout << "PASS impulse through detector and asymmetric follower\n";

    // Invalid updates must fail atomically instead of poisoning subsequent audio.
    auto invalid = c;
    invalid.thresholdScale = 0.0f;
    if (stage.configure(invalid)) { std::cerr << "accepted zero denominator scale\n"; return 1; }
    invalid = c; invalid.phase[2] = 1.0f;
    if (stage.configure(invalid)) { std::cerr << "accepted unstable phase\n"; return 1; }
    invalid = c; invalid.gainDecrease = -0.1f;
    if (stage.configure(invalid)) { std::cerr << "accepted negative memory\n"; return 1; }
    invalid = c; invalid.slope = -1.0f;
    if (stage.configure(invalid)) { std::cerr << "accepted negative slope\n"; return 1; }
    invalid = c; invalid.shape = std::nanf("");
    if (stage.configure(invalid)) { std::cerr << "accepted nonfinite shape\n"; return 1; }
    stage.reset();
    near(stage.tick(1.0f), 0.625f, 1e-7f, "invalid update leaves configuration intact");
    std::cout << "PASS invalid coefficient rejection is atomic\n";

    // Source has a distinct fast-attack ceiling follower in parallel with the
    // soft gain computer; it selects the lower gain before integer smoothing.
    GainStage ceiling;
    GainStage::Coefficients limit;
    limit.ceiling = 0.5f;
    limit.ceilingRelease = 0.5f;
    if (!ceiling.configure(limit)) return 1;
    near(ceiling.tick(2.0f), 0.25f, 1e-7f, "ceiling attacks immediately");
    near(ceiling.tick(0.0f), 0.625f, 1e-7f, "ceiling releases exponentially");
    near(ceiling.tick(0.0f), 0.8125f, 1e-7f, "ceiling retains release state");
    ceiling.reset();
    near(ceiling.tick(0.0f), 1.0f, 0.0f, "ceiling reset");
    std::cout << "PASS parallel peak ceiling and release\n";

    // Regression/property checks of the recovered magnitude detector path.
    GainStage positive, negative;
    if (!positive.configure(c) || !negative.configure(c)) return 1;
    unsigned seed = 731u;
    for (unsigned n = 0; n < 20000; ++n) {
        seed = seed * 1664525u + 1013904223u;
        const float inputValue = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
        const float g = positive.tick(inputValue);
        near(negative.tick(-inputValue), g, 1e-7f, "polarity invariant detector");
        if (!(g >= 0.0f && g <= 1.0f)) return 1;
    }
    GainStage phaseDisabled;
    c.phase = {0.0f, 0.5f, -0.5f};
    c.gainIncrease = 0.0f; c.gainDecrease = 0.0f;
    if (!phaseDisabled.configure(c)) return 1;
    near(phaseDisabled.tick(1.0f), 0.5f, 0.0f, "phase branch disable flag");
    near(phaseDisabled.tick(0.0f), 1.0f, 0.0f, "disabled branch has no impulse tail");
    std::cout << "PASS polarity, bounded gain and detector disable regressions\n";

    // Kernel order: soft = followed*scale + 2*offset (float), then the lower of
    // soft and the ceiling follower state; the ceiling state may start below unity.
    GainStage soft;
    GainStage::Coefficients s;
    s.softScale = 0.0625f;
    s.softOffset = 0.01f;
    s.ceilingRelease = 0.99f;
    if (!soft.configure(s)) return 1;
    soft.reset();
    near(soft.tick(0.0f), 1.0f * 0.0625f + 2.0f * 0.01f, 0.0f, "soft scale and doubled offset");
    soft.reset(0.0625f);
    const float released = 1.0f + (0.0625f - 1.0f) * 0.99f;
    near(soft.tick(0.0f), released, 0.0f, "ceiling state from startup value limits soft gain");
    soft.reset(0.0625f);
    s.ceilingRelease = 0.0f;
    if (!soft.configure(s)) return 1;
    near(soft.tick(0.0f), 0.0625f + 2.0f * 0.01f, 0.0f, "released ceiling leaves soft gain");
    GainStage::Coefficients bad = s;
    bad.softScale = 0.0f;
    if (soft.configure(bad)) { std::cerr << "accepted zero soft scale\n"; return 1; }
    bad = s;
    bad.softOffset = NAN;
    if (soft.configure(bad)) { std::cerr << "accepted nonfinite soft offset\n"; return 1; }
    near(soft.tick(0.0f), 0.0625f + 2.0f * 0.01f, 0.0f, "rejected soft update is atomic");
    std::cout << "PASS soft scale/offset before ceiling minimum, startup ceiling state\n";
    {
        // Kernel maximum-history: the detector magnitude is the maximum over the
        // last detectorWindow samples (64 in the reference) before the gain law.
        GainStage::Coefficients c;
        c.thresholdScale = 1.0f; c.slope = 1.0f; c.shape = 0.0f;
        c.detectorWindow = 64;
        GainStage s;
        if (!s.configure(c)) return 1;
        s.reset();
        const float held = s.tick(1.0f);
        bool ok = held < 1.0f;
        for (unsigned n = 1; n < 64; ++n) ok = ok && s.tick(0.0f) == held;
        ok = ok && s.tick(0.0f) == 1.0f;
        c.detectorWindow = 0;
        ok = ok && !s.configure(c);
        c.detectorWindow = GainStage::kMaxDetectorWindow + 1;
        ok = ok && !s.configure(c);
        if (!ok) { std::cerr << "FAIL detector window\n"; return 1; }
        std::cout << "PASS 64-sample detector maximum window\n";
    }
}
