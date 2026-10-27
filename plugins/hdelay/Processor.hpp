// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tsunami::hdelay {
// PARTIAL independent H-Delay core: free-time (ms) delay, feedback, mix, output.
// Grounded segment only (see docs/evidence/hdelay/README.md):
//  * HBDelayProcMono (XLMC symbol) structure: write = input + feedback state from
//    the previous sample; band-limited (31-tap) fractional read; wet sum.
//  * Tap kernel and wet gain measured from a genuine black-box impulse render
//    (Mono, Analog Off, HiFi, feedback 0, mix 100, 0 dB, 48 kHz): behavioural
//    calibration data, not vendor code or tables.
//  * Loop gain/extra loop sample measured from a genuine feedback-50 render.
// Sync law: BPM/Host note x 60/tempo, Host->ms copy (see effectiveDelayMs).
// Delay glide: exponential smoother (tau 0.5 s, provisional value). Not implemented: Analog, LoFi, modulation,
// HiPass/LoPass, phase. Fractional delays are rounded to whole samples.
class Processor {
public:
    enum Parameter : unsigned { DelayMs, DelayBpm, Feedback, Sync, Mix, Output, Analog, LoFi,
                                Rate, Depth, HiPass, LoPass, PhaseL, Bpm, Bypass, Count };
    static constexpr unsigned kTaps = 31, kHalf = 15;
    // Measured wet tap response per unit impulse (DC sum 0.88892).
    static constexpr std::array<float, kTaps> kTap{{
        -1.274136011e-04f, 2.219249000e-04f, -3.524853382e-04f, 5.271687987e-04f,
        -7.555527845e-04f, 1.049479120e-03f, -1.424431568e-03f, 1.902064541e-03f,
        -2.515017753e-03f, 3.316641320e-03f, -4.402416758e-03f, 5.963332020e-03f,
        -8.444882929e-03f, 1.317560393e-02f, -2.665189095e-02f, 8.874838352e-01f,
        2.840195224e-02f, -1.362358406e-02f, 8.651272394e-03f, -6.084418856e-03f,
        4.483243451e-03f, -3.374843160e-03f, 2.558876760e-03f, -1.935988199e-03f,
        1.451005461e-03f, -1.070353086e-03f, 7.718644338e-04f, -5.397601053e-04f,
        3.620196949e-04f, -2.289510885e-04f, 1.324012992e-04f }};
    // Write-path saturation (round-2 task3). HBDelayProcMono adds a soft saturator before the line
    // write; genuine 500 ms renders show echoes of exactly 1/9 (impulse 0.125) and 1/37 (fb 25:
    // 0.25 x 1/9 -> 1/37), i.e. line = s(x + fb/100 x v) with s(u) = u/(1+|u|). This also explains
    // the old "tap DC 0.88892" (= s(0.125)/0.125) and the rising repeat ratios at fb 50/90/150.
    // It bounds the loop for feedback up to 200 (genuine >100 limiter itself not recovered).
    // Curve recovered behaviourally from exact rational outputs; Analog Off only.
    static float saturate(float u) noexcept { return u / (1.f + std::fabs(u)); }
    // Delay-time glide: one-pole exponential smoother on the delay time (form: H-Delay.dll
    // FUN_1800348c0, coef = e^(-1/(fs*T))). T = 500 ms fitted to the genuine BPM90 glide render
    // (PROVISIONAL value; the genuine T is a table entry that was not recovered).
    static constexpr double kGlideSec = 0.5;
    // HiPass/LoPass cutoff taper: 19 x (20000/19)^(raw x 818/817) (log form as in H-Delay.dll
    // FUN_18002c740 p0 x (p1/p0)^x; constants fitted to genuine readbacks 154 Hz / 588 Hz / 2.49 kHz).
    // HiPass raw 0 = Off, LoPass raw 1 = Off.
    static double filterCutoffHz(float raw) noexcept { return 19. * std::pow(20000. / 19., double(raw) * 818. / 817.); }
    // 2nd-order (RBJ-form) sections on the wet read, outside the loop (PROVISIONAL placement).
    // Q fitted on 154 Hz + 588 Hz renders: HiPass 0.80, LoPass 0.949 (fit max err 0.01 dB);
    // held-out 2.49 kHz: max err 0.16 dB (threshold 0.5 dB).
    static constexpr double kHpQ = 0.80, kLpQ = 0.949;
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        void set(bool hp, double f0, double q, double fs) noexcept {
            const double w = 2 * M_PI * std::min(f0, .49 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
            b0 = (hp ? (1 + c) : (1 - c)) / 2 / a0; b1 = (hp ? -(1 + c) : (1 - c)) / a0; b2 = b0;
            a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
    };
    struct BqState { double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run(const Biquad& q, float x) noexcept { const double y = q.b0 * x + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y; return float(y); } };

    void prepare(double sampleRate, unsigned channels, unsigned) {
        rate_ = std::isfinite(sampleRate) && sampleRate > 0. ? sampleRate : 48000.;
        channels_ = channels;
        size_ = 1; while (size_ < unsigned(rate_ * 3.5) + 2 * kTaps + 4) size_ <<= 1;
        lines_.assign(channels_, std::vector<float>(size_, 0.f));
        state_.assign(channels_, 0.f);
        hpS_.assign(channels_, {}); lpS_.assign(channels_, {});
        clear();
        glide_ = targetSamples();
    }
    void reset() noexcept { clear(); glide_ = targetSamples(); }
    void clear() noexcept {
        for (auto& l : lines_) std::fill(l.begin(), l.end(), 0.f);
        std::fill(state_.begin(), state_.end(), 0.f);
        for (auto& b : hpS_) b = {};
        for (auto& b : lpS_) b = {};
        pos_ = 0;
    }
    void setParameter(unsigned index, float value) noexcept {
        if (index >= Count || !std::isfinite(value)) return;
        value = std::clamp(value, kLo[index], kHi[index]);
        if (index == Sync) value = std::round(value);
        // Genuine behaviour (oracle r2): switching Sync into ms copies the current
        // note-derived time into the ms control (a same-block delay_sec write is overwritten).
        if (index == Sync && value > 1.5f && p_[Sync] < 1.5f) p_[DelayMs] = float(effectiveDelayMs());
        p_[index] = value;
    }
    float getParameter(unsigned index) const noexcept { return index < Count ? p_[index] : 0.f; }
    void process(const float* const* in, float* const* out, unsigned frames) noexcept {
        if (lines_.empty()) return;
        const unsigned mask = size_ - 1;
        const double target = targetSamples(), a = std::exp(-1. / (rate_ * kGlideSec));
        const float fb = p_[Feedback] * .01f;
        const bool hpOn = p_[HiPass] > 0.f, lpOn = p_[LoPass] < 1.f;
        Biquad hp, lp;
        if (hpOn) hp.set(true, filterCutoffHz(p_[HiPass]), kHpQ, rate_);
        if (lpOn) lp.set(false, filterCutoffHz(p_[LoPass]), kLpQ, rate_);
        const float wet = p_[Mix] * .01f, dry = 1.f - wet; // linear law: UNVERIFIED
        const float g = std::pow(10.f, p_[Output] / 20.f);
        const bool bypass = p_[Bypass] >= .5f;
        double d0 = glide_;
        for (unsigned c = 0; c < channels_; ++c) {
            auto& l = lines_[c]; float s = state_[c]; unsigned w = pos_; double d = d0;
            for (unsigned n = 0; n < frames; ++n, w = (w + 1) & mask) {
                const float x = in[c][n];
                l[w] = saturate(x + s);
                d = target + (d - target) * a;
                if (std::fabs(d - target) < 1e-6) d = target;
                const double ds = std::max(double(kHalf + 1), d);
                const unsigned di = unsigned(ds); const float fr = float(ds - di);
                // Kernel at integer delay; fractional part (only while gliding) by linear
                // interpolation between neighbouring integer reads (approximation).
                float v0 = 0.f, v1 = 0.f;
                const unsigned c0 = (w - di) & mask, c1 = (c0 - 1) & mask;
                for (unsigned k = 0; k < kTaps; ++k) v0 += kTap[k] * l[(c0 + kHalf - k) & mask];
                if (fr != 0.f) for (unsigned k = 0; k < kTaps; ++k) v1 += kTap[k] * l[(c1 + kHalf - k) & mask];
                const float v = (v0 + fr * (v1 - v0)) * kNorm;
                s = fb * v;
                float wv = v;
                if (hpOn) wv = hpS_[c].run(hp, wv);
                if (lpOn) wv = lpS_[c].run(lp, wv);
                out[c][n] = bypass ? x : g * (dry * x + wet * wv);
            }
            state_[c] = s; glide_ = d;
        }
        pos_ = (pos_ + frames) & mask;
    }
    // Delay-time law. Sync choices (genuine order): 0 BPM, 1 Host, 2 ms.
    // BPM/Host: note length (beats) x 60 / tempo. Plugin BPM raw r -> 30 + 270 r (readback:
    // 0.2222 = 90, 0.3333 = 120). Host tempo: no host transport here -> 120 (PROVISIONAL).
    double noteBeats() const noexcept {
        static constexpr double kBase[] = {1. / 16, 1. / 8, 1. / 4, 1. / 2, 1., 2.}; // 1/64..1/2 (1/N = 4/N beats)
        const int i = int(std::lround(p_[DelayBpm] * 19.f));
        if (i >= 18) return i == 18 ? 4. : 8.; // "1B"/"2B" read as 1/2 bars of 4/4: PROVISIONAL
        const double k[] = {2. / 3., 1., 1.5};  // T, straight, D
        return kBase[i / 3] * k[i % 3];
    }
    double tempoBpm() const noexcept { return p_[Sync] < .5f ? 30. + 270. * p_[Bpm] : hostTempo_; }
    double effectiveDelayMs() const noexcept {
        return p_[Sync] > 1.5f ? double(p_[DelayMs]) : std::clamp(noteBeats() * 60000. / tempoBpm(), 1., 3500.);
    }
    unsigned latencySamples() const noexcept { return 0; }
    double tailSeconds() const noexcept { return p_[Feedback] > 0.f ? 30. : effectiveDelayMs() * 1e-3 + .01; }
private:
    double targetSamples() const noexcept { return std::round(effectiveDelayMs() * 1e-3 * rate_); }
    double glide_ = 18000.;
    static constexpr double kDc() { double d = 0; for (float t : kTap) d += t; return d; }
    // Read kernel normalised to unity DC (the 8/9 is the saturator). PROVISIONAL: the side lobes
    // were observed at 375 and 250 ms (fractional-read artifact?) but NOT at 500 ms (clean tap).
    static constexpr float kNorm = float(1. / 0.88892);
    static constexpr std::array<float, Count> kLo{{1, 0, 0, 0, 0, -18, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    static constexpr std::array<float, Count> kHi{{3500, 1, 200, 2, 100, 18, 4, 1, 1, 100, 1, 1, 1, 1, 1}};
    // Genuine fresh-instance defaults (Mono component readback).
    std::array<float, Count> p_{{375, 0.579f, 60, 1, 100, 0, 2, 0, 0, 0, 0, 1, 0, 0.3333333f, 0}};
    double hostTempo_ = 120.;
    double rate_ = 48000.; unsigned channels_ = 0, size_ = 0, pos_ = 0;
    std::vector<std::vector<float>> lines_; std::vector<float> state_;
    std::vector<BqState> hpS_, lpS_;
};
} // namespace tsunami::hdelay
