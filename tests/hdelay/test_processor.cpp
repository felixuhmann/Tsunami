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
        y = impulseResponse(p, 60000);
        double r1 = 0, r2 = 0; for (unsigned i = 17985; i <= 18015; ++i) r1 += y[i];
        for (unsigned i = 35970; i <= 36031; ++i) r2 += y[i];
        check(std::fabs(r1) > 0 && std::fabs(r2) > 0, "second repeat present with feedback");
        // Genuine fb50 @250 ms (fit render 20261027T161809): repeat2/repeat1 = 0.47324, repeat ratios
        // rise towards 0.5 as the level falls (level-dependent loop saturation).
        check(std::fabs(r2 / r1 - 0.47324) < 1e-3, "fb50 repeat ratio (level-dependent loop)");
        {
            double r3 = 0; for (unsigned i = 53955; i <= 54046; ++i) r3 += y[i];
            check(r3 / r2 > r2 / r1 + 0.01 && r3 / r2 < 0.5, "repeat ratio rises toward fb/100 as level falls");
        }
        std::cout << "PASS feedback repeat\n";
        p.setParameter(Processor::Mix, 0); p.setParameter(Processor::Feedback, 0); p.reset();
        y = impulseResponse(p, 1024);
        check(y[0] == .125f, "mix 0 passes dry"); 
        p.setParameter(Processor::Output, -6); p.reset(); y = impulseResponse(p, 16);
        check(std::fabs(y[0] - .125f * std::pow(10.f, -6.f / 20.f)) < 1e-7f, "output gain dB");
        check(p.latencySamples() == 0, "no reported latency");
        std::cout << "PASS mix/output\n";
        // Sync/delay-time law (genuine oracle r2: Sync->ms switch takes note x 60/tempo).
        auto peakOf = [](const std::vector<float>& s) {
            unsigned k = 0;
            for (unsigned i = 1; i < s.size(); ++i) if (std::fabs(s[i]) > std::fabs(s[k])) k = i;
            return k;
        };
        Processor q; q.prepare(48000, 1, 512);
        q.setParameter(Processor::Mix, 100); q.setParameter(Processor::Feedback, 0);
        q.setParameter(Processor::Sync, 0); q.setParameter(Processor::DelayBpm, 13.f / 19.f); // BPM, 1/4
        q.setParameter(Processor::Bpm, 1.f / 3.f);                                           // 120 BPM
        check(std::fabs(q.effectiveDelayMs() - 500.) < 1e-3, "BPM 1/4 at 120 = 500 ms");
        q.setParameter(Processor::Bpm, 60.f / 270.f);                                         // 90 BPM
        check(std::fabs(q.effectiveDelayMs() - 2000. / 3.) < 1e-3, "BPM 1/4 at 90 = 666.7 ms");
        Processor r; r.prepare(48000, 1, 512);
        r.setParameter(Processor::Mix, 100); r.setParameter(Processor::Feedback, 0);
        r.setParameter(Processor::DelayMs, 100); r.setParameter(Processor::DelayBpm, 10.f / 19.f); // 1/8
        r.setParameter(Processor::Sync, 2); // Host -> ms: time copied from note at host tempo 120
        check(std::fabs(r.getParameter(Processor::DelayMs) - 250.f) < 1e-3f, "Host->ms switch copies 1/8 @120 = 250 ms");
        r.reset(); y = impulseResponse(r, 24000);
        check(peakOf(y) == 12000, "250 ms tap at 12000");
        r.setParameter(Processor::DelayMs, 100); r.reset(); y = impulseResponse(r, 24000);
        check(peakOf(y) == 4800, "ms mode honours DelayMs afterwards");
        std::cout << "PASS sync/delay-time law\n";
        // Delay-time glide (genuine BPM90 1/8D render 20261027T154836: fresh 375 ms -> 500 ms,
        // impulse at 0, first echo peak at 21555; exponential smoother, tau 0.5 s fitted).
        Processor gl; gl.prepare(48000, 1, 512);
        gl.setParameter(Processor::Mix, 100); gl.setParameter(Processor::Feedback, 0);
        gl.setParameter(Processor::Sync, 2); gl.setParameter(Processor::DelayMs, 500); // no reset(): glide from fresh 375 ms
        y = impulseResponse(gl, 30000);
        std::cerr << "glide peak " << peakOf(y) << "\n";
        check(std::abs(int(peakOf(y)) - 21555) <= 2, "glide 375->500 ms: first echo at 21555 +-2");
        gl.reset(); y = impulseResponse(gl, 30000);
        check(peakOf(y) == 24000, "reset() settles the glide at the target");
        std::cout << "PASS delay glide\n";
        // Review finding: fb > ~106 must not explode. Genuine write path saturates (x/(1+|x|)),
        // which bounds the line; 500 ms genuine echoes are exactly 1/9 and 1/37 (fb 25).
        Processor fx; fx.prepare(48000, 1, 512);
        fx.setParameter(Processor::Mix, 100); fx.setParameter(Processor::Feedback, 200);
        fx.setParameter(Processor::DelayMs, 10); fx.reset();
        y = impulseResponse(fx, 96000);
        float mx = 0; for (float v : y) { check(std::isfinite(v), "fb200 finite"); mx = std::max(mx, std::fabs(v)); }
        check(mx < 1.2f, "fb200 output bounded by write-path saturation");
        check(std::fabs(Processor::saturate(.125f) - 1.f / 9.f) < 1e-7f && std::fabs(Processor::saturate(1.f / 36.f) - 1.f / 37.f) < 1e-7f,
              "write saturator reproduces genuine 1/9, 1/37 echoes");
        std::cout << "PASS feedback bounded/saturator\n";
        // HiPass/LoPass (genuine fit renders: 154 Hz / 588 Hz; held-out 2.49 kHz checked in
        // compare_filters_r3.py). Cutoff taper 19 x (20000/19)^(raw x 818/817); RBJ 2nd order.
        check(std::fabs(Processor::filterCutoffHz(0.3f) - 153.7) < 1.0 && std::fabs(Processor::filterCutoffHz(0.49266504f) - 588.2) < 1.0,
              "cutoff taper (genuine readback 154 Hz, 588 Hz)");
        auto magDb = [&](float hp, float lp, double f) {
            Processor h; h.prepare(48000, 1, 512);
            h.setParameter(Processor::Mix, 100); h.setParameter(Processor::Feedback, 0); h.setParameter(Processor::Sync, 2);
            h.setParameter(Processor::DelayMs, 10); h.setParameter(Processor::HiPass, hp); h.setParameter(Processor::LoPass, lp);
            h.reset(); auto z = impulseResponse(h, 16384);
            double re = 0, im = 0; for (unsigned i = 0; i < z.size(); ++i) { re += z[i] * std::cos(2 * M_PI * f * i / 48000); im -= z[i] * std::sin(2 * M_PI * f * i / 48000); }
            return 20 * std::log10(std::hypot(re, im));
        };
        const double flat = magDb(0, 1, 62.5), flat2 = magDb(0, 1, 1000);
        check(std::fabs(magDb(0.3f, 1, 62.5) - flat - (-15.466)) < 0.3, "HiPass 154 Hz: -15.47 dB at 62.5 Hz (genuine)");
        check(std::fabs(magDb(0, 0.3f, 1000) - flat2 - (-32.434)) < 0.3, "LoPass 154 Hz: -32.43 dB at 1 kHz (genuine)");
        std::cout << "PASS hipass/lopass\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
