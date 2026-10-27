// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

namespace tsunami::rcomp {

// Physical output controls for an already-compressed, time-aligned wet signal.
// This is NOT a complete compressor and does not estimate its missing wet path.
class OutputStage {
public:
    // Control-owner update; no audio-buffer allocation. Invalid updates are atomic.
    bool setGainTrim(float gainDb, float trimDb) noexcept {
        if (!std::isfinite(gainDb) || gainDb < -30.0f || gainDb > 30.0f
            || !std::isfinite(trimDb) || trimDb < -18.0f || trimDb > 18.0f) return false;
        gain_ = static_cast<float>(std::pow(10.0, static_cast<double>(gainDb) / 20.0));
        trim_ = static_cast<float>(std::pow(10.0, static_cast<double>(trimDb) / 20.0));
        return true;
    }

    bool setMix(float percent) noexcept {
        if (!std::isfinite(percent) || percent < 0.0f || percent > 100.0f) return false;
        mix_ = static_cast<double>(percent) * 0.01;
        return true;
    }

    float tick(float alignedDry, float alignedWet) noexcept {
        const float wet = static_cast<float>(mix_ * trim_);
        const float dry = static_cast<float>((1.0 - mix_) * trim_);
        return alignedDry * dry + (alignedWet * gain_) * wet;
    }

    // Aligned planar channel buffers; either input may alias output. The caller
    // owns compression, delay alignment, and control-transition scheduling.
    void process(const float* dry, const float* wet, float* output, unsigned frames) noexcept {
        for (unsigned i = 0; i < frames; ++i) output[i] = tick(dry[i], wet[i]);
    }

private:
    double mix_ = 1.0;
    float gain_ = 1.0f;
    float trim_ = 1.0f;
};

} // namespace tsunami::rcomp
