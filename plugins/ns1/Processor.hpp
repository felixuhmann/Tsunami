// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Tsunami Suppressor (NS1 target) plugin-facing processor, mono only.
//
// Parameters follow the reference readback: index 0 "NS1" 0..100 % (step 0.1,
// fresh default 0), index 1 "Bypass" Off/On. Any NS1 value runs the continuous
// control law controlParameters() (see design.hpp). controlEstablished() stays
// true only at 0, 50 and 100 %, the controls covered by calibrated captures;
// the law between them is evidenced by derivation renders and the predeclared
// 25/75 % gate (docs/evidence/ns1).
// Two channels run the linked (NS1 Stereo) kernel: per-channel front and audio
// sections with one detector driven by the mean of both front outputs.
// prepare() refuses other channel counts.
// prepare() also throws std::invalid_argument for sample rates outside
// 40-100 kHz, the only validated design range.
#include "design.hpp"
#include "mono.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace tsunami::ns1 {
class Processor {
public:
    enum Index : unsigned { Amount = 0, Bypass = 1, ParameterCount = 2 };
    void prepare(double sampleRate, unsigned channels, unsigned /*maxBlockSize*/) {
        if (channels != 1 && channels != 2) throw std::invalid_argument("NS1 supports mono or linked stereo only");
        channels_ = channels;
        designMono(sampleRate, controlParameters(0.f)); // validates the rate (throws outside 40-100 kHz)
        rate_ = sampleRate;
        mono_ = std::make_unique<Mono>(designMono(rate_, controlParameters(amount_ / 100.f)), initialMonoState());
    }
    void reset() noexcept { if (mono_) mono_->reset(initialMonoState()); }
    void setParameter(unsigned index, float value) noexcept {
        if (index == Amount) {
            const float v = std::isfinite(value) ? (value < 0.f ? 0.f : (value > 100.f ? 100.f : value)) : 0.f;
            amount_ = v; select();
        } else if (index == Bypass) {
            bypass_ = std::isfinite(value) && value >= .5f;
        }
    }
    float getParameter(unsigned index) const noexcept {
        return index == Amount ? amount_ : (index == Bypass ? (bypass_ ? 1.f : 0.f) : 0.f);
    }
    bool controlEstablished() const noexcept { return amount_ == 0.f || amount_ == 50.f || amount_ == 100.f; }
    void process(const float* const* inputs, float* const* outputs, unsigned frames) noexcept {
        if (!mono_) return;
        if (bypass_) {
            for (unsigned ch = 0; ch < channels_; ++ch)
                if (outputs[ch] != inputs[ch]) std::memmove(outputs[ch], inputs[ch], frames * sizeof(float));
            return;
        }
        if (channels_ == 1) { mono_->process(inputs[0], outputs[0], frames); return; }
        for (unsigned i = 0; i < frames; ++i) {
            const float l = inputs[0][i], r = inputs[1][i];
            mono_->processLinked(l, r, outputs[0][i], outputs[1][i]);
        }
    }
    unsigned latencySamples() const noexcept { return 0; }
    double tailSeconds() const noexcept { return 0.5; }
private:
    void select() noexcept {
        if (mono_) mono_->configure(designMono(rate_, controlParameters(amount_ / 100.f)));
    }
    double rate_ = 48000;
    unsigned channels_ = 1;
    std::unique_ptr<Mono> mono_;
    float amount_ = 0.f;
    bool bypass_ = false;
};
}
