// SPDX-License-Identifier: GPL-3.0-or-later
// Kernel/unit tests for the H-Delay core. These are NOT full-plugin fidelity
// claims; fidelity vs genuine renders is checked by tests/hdelay/compare_oracle.py.
#include "Processor.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using tsunami::hdelay::Processor;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::vector<float> impulseResponse(Processor& p, unsigned n) {
    std::vector<float> x(n, 0.f), y(n, 0.f); x[0] = .125f;
    const float* in[] = {x.data()}; float* out[] = {y.data()};
    for (unsigned o = 0; o < n; o += 512) { unsigned b = std::min(512u, n - o);
        const float* i2[] = {in[0] + o}; float* o2[] = {out[0] + o}; p.process(i2, o2, b); }
    return y;
}
int main() {
    try {
        Processor p; p.prepare(48000, 1, 512);
        p.setParameter(Processor::Feedback, 0); p.setParameter(Processor::Mix, 100);
        p.setParameter(Processor::DelayMs, 375); p.setParameter(Processor::Output, 0);
        auto y = impulseResponse(p, 48000);
        unsigned peak = 0; for (unsigned i = 1; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[peak])) peak = i;
        check(peak == 18000, "375 ms tap centred at sample 18000");
        for (unsigned i = 0; i < y.size(); ++i) if (i < 17985 || i > 18015) check(y[i] == 0.f, "tap support limited to 31 samples");
        double dc = 0; for (float v : y) dc += v; dc /= .125;
        check(std::fabs(dc - 0.88892) < 1e-3, "wet DC gain");
        check(y[17999] < 0 && y[18001] > 0, "band-limited interpolator side lobes");
        std::cout << "PASS single tap\n";
        p.setParameter(Processor::Feedback, 50); p.reset();
        y = impulseResponse(p, 48000);
        double r1 = 0, r2 = 0; for (unsigned i = 17985; i <= 18015; ++i) r1 += y[i];
        for (unsigned i = 35970; i <= 36031; ++i) r2 += y[i];
        check(std::fabs(r1) > 0 && std::fabs(r2) > 0, "second repeat present with feedback");
        check(std::fabs(r2 / r1 - 0.5 * Processor::loopDcGain()) < 1e-3, "repeat ratio = feedback * loop DC gain");
        std::cout << "PASS feedback repeat\n";
        p.setParameter(Processor::Mix, 0); p.setParameter(Processor::Feedback, 0); p.reset();
        y = impulseResponse(p, 1024);
        check(y[0] == .125f, "mix 0 passes dry"); 
        p.setParameter(Processor::Output, -6); p.reset(); y = impulseResponse(p, 16);
        check(std::fabs(y[0] - .125f * std::pow(10.f, -6.f / 20.f)) < 1e-7f, "output gain dB");
        check(p.latencySamples() == 0, "no reported latency");
        std::cout << "PASS mix/output\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
