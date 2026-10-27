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
// Not implemented (stored only): BPM/Host sync, Analog, LoFi, modulation,
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
    // Feedback re-injection gain relative to wet tap output, from genuine fb=50 render:
    // repeat2/repeat1 DC ratio 0.473202 = 0.5 * 0.946403.
    static constexpr double kLoopDc = 0.946403;
    static double loopDcGain() noexcept { return kLoopDc; }

    void prepare(double sampleRate, unsigned channels, unsigned) {
        rate_ = std::isfinite(sampleRate) && sampleRate > 0. ? sampleRate : 48000.;
        channels_ = channels;
        size_ = 1; while (size_ < unsigned(rate_ * 3.5) + 2 * kTaps + 4) size_ <<= 1;
        lines_.assign(channels_, std::vector<float>(size_, 0.f));
        state_.assign(channels_, 0.f);
        reset();
    }
    void reset() noexcept {
        for (auto& l : lines_) std::fill(l.begin(), l.end(), 0.f);
        std::fill(state_.begin(), state_.end(), 0.f);
        pos_ = 0;
    }
    void setParameter(unsigned index, float value) noexcept {
        if (index >= Count || !std::isfinite(value)) return;
        value = std::clamp(value, kLo[index], kHi[index]);
        p_[index] = value;
    }
    float getParameter(unsigned index) const noexcept { return index < Count ? p_[index] : 0.f; }
    void process(const float* const* in, float* const* out, unsigned frames) noexcept {
        if (lines_.empty()) return;
        const unsigned mask = size_ - 1;
        const unsigned delay = std::max<unsigned>(kHalf + 1, unsigned(std::lround(p_[DelayMs] * 1e-3 * rate_)));
        const float fb = p_[Feedback] * .01f * float(kLoopDc / kDc());
        const float wet = p_[Mix] * .01f, dry = 1.f - wet; // linear law: UNVERIFIED
        const float g = std::pow(10.f, p_[Output] / 20.f);
        const bool bypass = p_[Bypass] >= .5f;
        for (unsigned c = 0; c < channels_; ++c) {
            auto& l = lines_[c]; float s = state_[c]; unsigned w = pos_;
            for (unsigned n = 0; n < frames; ++n, w = (w + 1) & mask) {
                const float x = in[c][n];
                l[w] = x + s;
                float v = 0.f;
                const unsigned centre = (w - delay) & mask;
                for (unsigned k = 0; k < kTaps; ++k) v += kTap[k] * l[(centre + kHalf - k) & mask];
                s = fb * v;
                out[c][n] = bypass ? x : g * (dry * x + wet * v);
            }
            state_[c] = s;
        }
        pos_ = (pos_ + frames) & mask;
    }
    unsigned latencySamples() const noexcept { return 0; }
    double tailSeconds() const noexcept { return p_[Feedback] > 0.f ? 30. : p_[DelayMs] * 1e-3 + .01; }
private:
    static constexpr double kDc() { double d = 0; for (float t : kTap) d += t; return d; }
    static constexpr std::array<float, Count> kLo{{1, 0, 0, 0, 0, -18, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    static constexpr std::array<float, Count> kHi{{3500, 1, 200, 2, 100, 18, 4, 1, 1, 100, 1, 1, 1, 1, 1}};
    // Genuine fresh-instance defaults (Mono component readback).
    std::array<float, Count> p_{{375, 0.579f, 60, 1, 100, 0, 2, 0, 0, 0, 0, 1, 0, 0.3333333f, 0}};
    double rate_ = 48000.; unsigned channels_ = 0, size_ = 0, pos_ = 0;
    std::vector<std::vector<float>> lines_; std::vector<float> state_;
};
} // namespace tsunami::hdelay
