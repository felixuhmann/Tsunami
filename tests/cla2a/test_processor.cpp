// SPDX-License-Identifier: GPL-3.0-or-later
// Processor behaviour tests. These assert recovered STRUCTURE (direction of
// control effects, determinism, dry paths); they make no fidelity claim.
#include "ControlLaws.h"
#include "Processor.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using tsunami::cla2a::Processor;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static std::vector<float> render(Processor& p, double amplitude, unsigned frames) {
    std::vector<float> x(frames), y(frames), z(frames);
    for (unsigned n = 0; n < frames; ++n)
        x[n] = static_cast<float>(amplitude * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * n / 48000.0));
    const float* ins[]{x.data(), x.data()}; float* outs[]{y.data(), z.data()};
    for (unsigned off = 0; off < frames; off += 256) {
        const unsigned count = std::min(256u, frames - off);
        const float* i2[]{ins[0] + off, ins[1] + off}; float* o2[]{outs[0] + off, outs[1] + off};
        p.process(i2, o2, count);
    }
    return y;
}
static double rmsTail(const std::vector<float>& y, unsigned from) {
    double s = 0; for (unsigned n = from; n < y.size(); ++n) s += double(y[n]) * y[n];
    return std::sqrt(s / (y.size() - from));
}
int main() {
    { // dry / trim subset (measured against genuine capture previously)
        Processor p; p.prepare(48000, 2, 64);
        p.setParameter(Processor::Mix, 0);
        p.setParameter(Processor::Trim, 6);
        std::array<float,4> a{0.1f,-0.2f,0.0f,1.0f},b{},c{};
        const float* ins[]{a.data(),a.data()}; float* outs[]{b.data(),c.data()};
        p.process(ins,outs,4);
        check(std::abs(b[0]-0.19952623f) < 1e-6f, "dry trim");
    }
    { // bypass is exact
        Processor p; p.prepare(48000, 2, 64);
        p.setParameter(Processor::Bypass, 1);
        auto y = render(p, 0.5, 4800);
        check(std::abs(y[100] - float(0.5*std::sin(2.0*3.14159265358979323846*1000.0*100/48000.0))) < 1e-7f, "bypass exact");
    }
    const unsigned frames = 48000 * 2;
    { // wet default renders a finite non-silent signal
        Processor p; p.prepare(48000, 2, 256);
        auto y = render(p, 0.25, frames);
        bool finite = true; for (float v : y) finite = finite && std::isfinite(v);
        check(finite && rmsTail(y, frames/2) > 1e-3, "wet default produces finite audio");
    }
    double lowPr, highPr, comp, limit;
    { Processor p; p.prepare(48000, 2, 256); p.setParameter(Processor::PeakReduction, 0);
      lowPr = rmsTail(render(p, 0.5, frames), frames/2); }
    { Processor p; p.prepare(48000, 2, 256); p.setParameter(Processor::PeakReduction, 80);
      highPr = rmsTail(render(p, 0.5, frames), frames/2); }
    check(highPr < lowPr * 0.5, "more Peak Reduction reduces loud steady-state level");
    { Processor p; p.prepare(48000, 2, 256); p.setParameter(Processor::PeakReduction, 70);
      comp = rmsTail(render(p, 0.5, frames), frames/2); }
    { Processor p; p.prepare(48000, 2, 256); p.setParameter(Processor::PeakReduction, 70);
      p.setParameter(Processor::Mode, 1);
      limit = rmsTail(render(p, 0.5, frames), frames/2); }
    check(limit < comp, "Limit mode holds a loud signal lower than Comp mode");
    double g30, g60;
    { Processor p; p.prepare(48000, 2, 256); p.setParameter(Processor::Gain, 30);
      g30 = rmsTail(render(p, 0.05, frames), frames/2); }
    { Processor p; p.prepare(48000, 2, 256); p.setParameter(Processor::Gain, 60);
      g60 = rmsTail(render(p, 0.05, frames), frames/2); }
    check(g60 > g30 * 1.5, "Gain raises output");
    { // recovered Gain makeup law: quiet signal, minimal drive -> wet gain = law
        for (float knob : {40.0f, 0.0f, 75.0f}) {
            Processor p; p.prepare(48000, 2, 256);
            p.setParameter(Processor::PeakReduction, 0);
            p.setParameter(Processor::Gain, knob);
            const double ratio = rmsTail(render(p, 1e-3, frames), frames/2) / (1e-3 / std::sqrt(2.0));
            const double expectDb = tsunami::cla2a::gainMakeupDb(knob) + tsunami::cla2a::Characterized::ProgrammeLevelDb;
            check(std::abs(20.0 * std::log10(ratio) - expectDb) < 0.05, "wet gain follows recovered makeup law");
        }
    }
    { // characterized Comp static curve: genuine stepped 1 kHz tone renders
      // (runtime/evidence/cla2a 20261027T161007 PR46, 20261027T161031 PR70)
      // steady gain at peak input -12 / 0 dBFS. Tolerance 0.5 dB.
        struct Point { float pr; double peak, genuineDb; };
        for (const Point& q : {Point{46, 0.25118864315, 7.24}, Point{46, 1.0, -1.51},
                               Point{70, 0.25118864315, -7.67}, Point{70, 0.0223872114, 7.64}}) {
            Processor p; p.prepare(48000, 2, 256);
            p.setParameter(Processor::Analog, 2);
            p.setParameter(Processor::PeakReduction, q.pr);
            const double ratio = rmsTail(render(p, q.peak, frames), frames/2) / (q.peak / std::sqrt(2.0));
            check(std::abs(20.0 * std::log10(ratio) - q.genuineDb) < 0.5, "Comp static curve matches characterization");
        }
    }
    { // reset determinism
        Processor p; p.prepare(48000, 2, 256);
        auto a = render(p, 0.3, 9600); p.reset(); auto b = render(p, 0.3, 9600);
        check(a == b, "reset restores identical state");
        check(p.latencySamples() == 0 && p.tailSeconds() > 0.0, "latency / tail report");
    }
    std::puts("PASS: processor structure (no fidelity claim)");
}
