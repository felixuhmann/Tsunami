// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tsunami::rcomp {

// Partial, coefficient-domain DSP. These are NOT user-facing threshold/ratio units.
// No full RCompressor Processor or physical-control mapping is claimed here.
class GainStage {
public:
    struct Coefficients {
        std::array<float, 3> phase{};
        float thresholdScale = 1.0f;
        float slope = 0.0f;
        float shape = 1.0f;
        float gainDecrease = 0.0f;
        float gainIncrease = 0.0f;
        float ceiling = 1.0f;
        float ceilingRelease = 0.0f;
    };

    bool configure(const Coefficients& coefficients) noexcept {
        const auto memoryValid = [](float value) {
            return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        };
        for (float phase : coefficients.phase)
            if (!std::isfinite(phase) || std::abs(phase) >= 1.0f) return false;
        if (!std::isfinite(coefficients.thresholdScale) || coefficients.thresholdScale <= 0.0f
            || !std::isfinite(coefficients.slope) || coefficients.slope < 0.0f
            || !std::isfinite(coefficients.shape) || coefficients.shape < 0.0f
            || !memoryValid(coefficients.gainDecrease)
            || !memoryValid(coefficients.gainIncrease)
            || !std::isfinite(coefficients.ceiling) || coefficients.ceiling <= 0.0f
            || !memoryValid(coefficients.ceilingRelease)) return false;
        coefficients_ = coefficients;
        return true;
    }

    void reset() noexcept {
        previousInput_ = 0.0f;
        previousPhase_.fill(0.0f);
        gain_ = 1.0f;
        ceilingGain_ = 1.0f;
    }

    float tick(float input) noexcept {
        float magnitude = std::abs(input);
        // The reference bypasses all three phase branches when the first is zero.
        if (coefficients_.phase[0] != 0.0f) {
            for (unsigned branch = 0; branch < 3; ++branch) {
                const float shifted = previousInput_ + coefficients_.phase[branch]
                    * (input - previousPhase_[branch]);
                previousPhase_[branch] = shifted;
                magnitude = std::max(magnitude, std::abs(shifted));
            }
            previousInput_ = input;
        }
        const float target = targetGain(magnitude, coefficients_.thresholdScale,
                                        coefficients_.slope, coefficients_.shape);
        const float memory = gain_ <= target ? coefficients_.gainIncrease
                                            : coefficients_.gainDecrease;
        gain_ = target + (gain_ - target) * memory;
        const float peakTarget = magnitude > coefficients_.ceiling
            ? coefficients_.ceiling / magnitude : 1.0f;
        ceilingGain_ = std::min(peakTarget, peakTarget
            + (ceilingGain_ - peakTarget) * coefficients_.ceilingRelease);
        return std::min(gain_, ceilingGain_);
    }

    static float targetGain(float magnitude, float thresholdScale, float slope,
                            float shape) noexcept {
        const float q = thresholdScale / (magnitude * slope + thresholdScale);
        return std::min(1.0f, q + shape * q * q * (1.0f - q));
    }

private:
    Coefficients coefficients_{};
    float previousInput_ = 0.0f;
    std::array<float, 3> previousPhase_{};
    float gain_ = 1.0f;
    float ceilingGain_ = 1.0f;
};

} // namespace tsunami::rcomp
