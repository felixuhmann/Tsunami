// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Tsunami Suppressor (NS1 target) plugin-facing processor, mono only.
//
// Parameters follow the reference readback: index 0 "NS1" 0..100 % (step 0.1,
// fresh default 0), index 1 "Bypass" Off/On. Coefficients are only established
// at NS1 = 0, 50 and 100 %. Any other value runs the NEAREST established design
// and controlEstablished() returns false; this is a provisional placeholder,
// not a recovered mapping. Stereo/linked processing is not implemented and
// prepare() refuses channels != 1 (the reference stereo kernel shares its
// detector between channels, so two independent monos would be wrong).
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
        if (channels != 1) throw std::invalid_argument("NS1 stereo/linked processing not implemented");
        const float sampled[3] = {0.f, .5f, 1.f};
        for (unsigned k = 0; k < 3; ++k) designs_[k] = designSampledMono(sampleRate, sampled[k]);
        mono_ = std::make_unique<Mono>(designs_[0], initialMonoState());
        select();
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
        if (bypass_) { if (outputs[0] != inputs[0]) std::memmove(outputs[0], inputs[0], frames * sizeof(float)); return; }
        mono_->process(inputs[0], outputs[0], frames);
    }
    unsigned latencySamples() const noexcept { return 0; }
    double tailSeconds() const noexcept { return 0.5; }
private:
    void select() noexcept {
        const unsigned k = amount_ < 25.f ? 0u : (amount_ < 75.f ? 1u : 2u);
        if (mono_) mono_->configure(designs_[k]);
    }
    std::array<MonoConfiguration, 3> designs_{};
    std::unique_ptr<Mono> mono_;
    float amount_ = 0.f;
    bool bypass_ = false;
};
}
