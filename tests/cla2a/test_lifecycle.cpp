// SPDX-License-Identifier: GPL-3.0-or-later
// Processor API/state lifecycle tests (docs/PLUGIN_CONTRACT.md). These test
// the project's own API contract and replacement-only defensive policy; they
// make NO fidelity or original-equivalence claim.
#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
using tsunami::cla2a::Processor;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static std::vector<float> tone(unsigned frames, double amplitude, unsigned offset = 0) {
    std::vector<float> x(frames);
    for (unsigned n = 0; n < frames; ++n)
        x[n] = float(amplitude * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * (n + offset) / 48000.0));
    return x;
}
// Renders `frames` of a tone continuing at `offset`; returns channel 0 then 1.
static std::vector<float> render(Processor& p, unsigned frames, double amplitude = 0.3, unsigned offset = 0) {
    const auto x = tone(frames, amplitude, offset);
    std::vector<float> y(2 * frames);
    const float* ins[]{x.data(), x.data()}; float* outs[]{y.data(), y.data() + frames};
    p.process(ins, outs, frames);
    return y;
}
static bool allFinite(const std::vector<float>& y) {
    for (float v : y) if (!std::isfinite(v)) return false;
    return true;
}
int main() {
    const Processor::TimeConstants good{{4.0, 0.3125}, {10.0, 960.0}, 0.24, 3.0};
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    { // Invalid/nonfinite time constants are rejected atomically (replacement-only
      // defensive policy): rendering continues bit-identical to a processor that
      // never received the update, and no NaN state survives a later valid update.
        const Processor::TimeConstants bad[]{
            {{nan, 0.3125}, {10.0, 960.0}, 0.24, 3.0}, {{4.0, 0.3125}, {10.0, inf}, 0.24, 3.0},
            {{4.0, 0.3125}, {10.0, 960.0}, -0.24, 3.0}, {{4.0, 0.3125}, {10.0, 960.0}, 0.24, 0.0},
            {{4.0, -1.0}, {10.0, 960.0}, 0.24, 3.0}};
        for (const auto& t : bad) {
            Processor ref, p; ref.prepare(48000, 2, 4800); p.prepare(48000, 2, 4800);
            ref.setParameter(Processor::PeakReduction, 70); p.setParameter(Processor::PeakReduction, 70);
            check(render(ref, 4800, 0.5) == render(p, 4800, 0.5), "identical before invalid update");
            p.setTimeConstants(t);
            check(render(ref, 4800, 0.5, 4800) == render(p, 4800, 0.5, 4800), "invalid time constants rejected");
            ref.setTimeConstants(good); p.setTimeConstants(good);
            check(render(ref, 4800, 0.5, 9600) == render(p, 4800, 0.5, 9600), "no state poisoned by invalid update");
        }
    }
    { // Output is float-finite even when finite double math exceeds float range
      // (defensive policy: nonrepresentable samples are written as 0, matching
      // the existing nonfinite-output guard). Dry +18 dB trim and wet max gain.
        Processor dry; dry.prepare(48000, 2, 256);
        dry.setParameter(Processor::Mix, 0); dry.setParameter(Processor::Trim, 18);
        check(allFinite(render(dry, 2048, 1e38)), "dry trim overflow stays finite");
        Processor wet; wet.prepare(48000, 2, 256); wet.setParameter(Processor::Gain, 100);
        check(allFinite(render(wet, 2048, 1e38)), "wet gain overflow stays finite");
    }
    // ---- Regression guards for existing contract behaviour (already passing) ----
    { // Valid controls applied before prepare survive prepare; repeated prepare
      // and reset preserve accepted parameters and time constants.
        Processor a, b;
        a.setParameter(Processor::PeakReduction, 70); a.setParameter(Processor::Mode, 1);
        a.setParameter(Processor::Trim, -3); a.prepare(48000, 2, 256);
        b.prepare(48000, 2, 256);
        b.setParameter(Processor::PeakReduction, 70); b.setParameter(Processor::Mode, 1);
        b.setParameter(Processor::Trim, -3);
        const auto first = render(a, 4800);
        check(first == render(b, 4800), "controls before prepare equal controls after prepare");
        a.prepare(48000, 2, 512);
        check(render(a, 4800) == first, "repeated prepare preserves parameters");
        a.reset();
        check(render(a, 4800) == first && a.getParameter(Processor::Trim) == -3.0f, "reset preserves parameters");
        Processor c; c.setTimeConstants({{2.0, 1.0}, {20.0, 200.0}, 0.1, 2.0}); c.prepare(48000, 2, 256);
        Processor d; d.prepare(48000, 2, 256); d.setTimeConstants({{2.0, 1.0}, {20.0, 200.0}, 0.1, 2.0});
        check(render(c, 4800, 0.5) == render(d, 4800, 0.5), "time constants before prepare survive prepare");
    }
    { // Rejected prepare and nonfinite controls leave state untouched.
        Processor ref, p; ref.prepare(48000, 2, 256); p.prepare(48000, 2, 256);
        for (double r : {0.0, -48000.0, nan, inf}) {
            bool threw = false;
            try { p.prepare(r, 2, 256); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "invalid rate rejected");
        }
        for (unsigned ch : {0u, 3u}) {
            bool threw = false;
            try { p.prepare(48000, ch, 256); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "invalid channel count rejected");
        }
        p.setParameter(Processor::Gain, std::numeric_limits<float>::quiet_NaN());
        p.setParameter(Processor::Trim, std::numeric_limits<float>::infinity());
        p.setParameter(Processor::ParameterCount, 1.0f);
        check(p.getParameter(Processor::Gain) == ref.getParameter(Processor::Gain), "NaN control ignored");
        check(render(ref, 4800) == render(p, 4800), "rejected prepare/controls leave state intact");
    }
    { // Zero-frame and null-output calls write nothing and change no state;
      // in-place processing equals out-of-place processing.
        Processor ref, p; ref.prepare(48000, 2, 256); p.prepare(48000, 2, 256);
        float sentinel[2]{7.0f, 7.0f}; const float* in[]{sentinel, sentinel}; float* out[]{sentinel, sentinel + 1};
        p.process(in, out, 0); p.process(in, nullptr, 0);
        check(sentinel[0] == 7.0f && sentinel[1] == 7.0f, "zero frames writes nothing");
        p.setParameter(Processor::Mix, 50); ref.setParameter(Processor::Mix, 50);
        auto x = tone(4800, 0.4), l = x, r = x;
        float* io[]{l.data(), r.data()};
        p.process(io, io, 4800);
        const auto y = render(ref, 4800, 0.4);
        check(std::vector<float>(y.begin(), y.begin() + 4800) == l, "in-place equals out-of-place");
    }
    { // Unprepared processing writes finite silence; prepare then yields
      // the same output as a processor that was never run unprepared.
        Processor p, ref;
        const auto y = render(p, 4800);
        bool silent = true; for (float v : y) silent = silent && v == 0.0f;
        check(silent, "unprepared process writes silence");
        p.prepare(48000, 2, 256); ref.prepare(48000, 2, 256);
        check(render(p, 4800) == render(ref, 4800), "unprepared process leaves no state after prepare");
    }
    std::puts("PASS: processor lifecycle/API contract (no fidelity claim)");
}
