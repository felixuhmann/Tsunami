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
        // Kernel soft gain: followed*softScale + 2*softOffset, formed before the
        // ceiling minimum. Only scale+2*offset = 1/16 at unity is evidenced for the
        // wrapper; the writer of the individual values is unresolved.
        float softScale = 1.0f;
        float softOffset = 0.0f;
        // Detector maximum over the last detectorWindow magnitudes (including the
        // current one) before the gain law. The reference kernel's maximum-history
        // tree spans 64 samples; 1 = instantaneous. The exact dyadic tree/ring
        // interleaving of the reference is approximated by a plain sliding maximum.
        unsigned detectorWindow = 1;
    };
    static constexpr unsigned kMaxDetectorWindow = 64;

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
            || !memoryValid(coefficients.ceilingRelease)
            || !std::isfinite(coefficients.softScale) || coefficients.softScale <= 0.0f
            || !std::isfinite(coefficients.softOffset)
            || coefficients.detectorWindow < 1
            || coefficients.detectorWindow > kMaxDetectorWindow) return false;
        coefficients_ = coefficients;
        return true;
    }

    // ceilingState is the ceiling follower's starting value; the reference
    // wrapper starts it at 0.0625, the history-domain unity (0x180028450).
    void reset(float ceilingState = 1.0f) noexcept {
        previousInput_ = 0.0f;
        previousPhase_.fill(0.0f);
        window_.fill(0.0f);
        windowPos_ = 0;
        gain_ = 1.0f;
        ceilingGain_ = ceilingState;
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
        window_[windowPos_] = magnitude;
        windowPos_ = (windowPos_ + 1) % kMaxDetectorWindow;
        for (unsigned back = 1; back < coefficients_.detectorWindow; ++back)
            magnitude = std::max(magnitude,
                window_[(windowPos_ + kMaxDetectorWindow - 1 - back) % kMaxDetectorWindow]);
        const float target = targetGain(magnitude, coefficients_.thresholdScale,
                                        coefficients_.slope, coefficients_.shape);
        const float memory = gain_ <= target ? coefficients_.gainIncrease
                                            : coefficients_.gainDecrease;
        gain_ = target + (gain_ - target) * memory;
        const float peakTarget = magnitude > coefficients_.ceiling
            ? coefficients_.ceiling / magnitude : 1.0f;
        ceilingGain_ = std::min(peakTarget, peakTarget
            + (ceilingGain_ - peakTarget) * coefficients_.ceilingRelease);
        const float soft = gain_ * coefficients_.softScale + 2.0f * coefficients_.softOffset;
        return std::min(soft, ceilingGain_);
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
    std::array<float, kMaxDetectorWindow> window_{};
    unsigned windowPos_ = 0;
    float gain_ = 1.0f;
    float ceilingGain_ = 1.0f;
};

} // namespace tsunami::rcomp
