// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Tsunami Opto processor.
//
// PROVENANCE STATUS (see docs/evidence/cla2a/README.md):
//  * Recovered structure: |x| detector -> piecewise-linear static curve ->
//    two-memory ballistics (program-dependent attack table, constant release,
//    0.75/0.25 weighting, unity init) -> gain applied to the programme path;
//    1e-8-scaled LCG noise added at the input; mix and output gain are
//    one-pole smoothed; analog hum is a renormalised quadrature oscillator.
//  * Control mapping (Gain / Peak Reduction knob <-> normalized) reproduces
//    all genuine host display values.
//  * RECOVERED (ControlLaws.h): Gain knob -> makeup dB and Peak Reduction
//    knob -> sidechain drive dB breakpoint laws. Their placement (output
//    gain / detector input gain) is a structural assumption.
//  * CHARACTERIZED (fitted to genuine renders, not recovered): Comp/Limit
//    static curves (CharacterizedCurve.h) and the opto time constants.
//  * RECOVERED design, INFERRED placement: 48 kHz biquad cascade on the
//    detector input (SidechainFilters.h).
//  * PROVISIONAL: remaining values in Provisional:: below (Hi Freq network,
//    hum level, smoothing). NOT recovered, NOT fitted. Wet defaults remain
//    UNMATCHED under the declared thresholds.
#include "ControlLaws.h"
#include "CharacterizedCurve.h"
#include "ControlMapping.h"
#include "Kernel.h"
#include "SidechainFilters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace tsunami::cla2a {
namespace Provisional {
constexpr unsigned CurvePoints = 26;     // breakpoint count recovered; values characterized
constexpr unsigned AttackPoints = 12;    // breakpoint count recovered; values provisional
constexpr double AttackFastMs[2] = {1.0, 10.0}, AttackSlowMs[2] = {10.0, 60.0};
constexpr double ReleaseFastS = 0.06, ReleaseSlowS = 1.5;
constexpr double SmoothS = 0.02;
constexpr double HumLevel = 3.0e-5;
constexpr double HiFreqHz = 2000.0;
}

class Processor {
public:
    enum Parameter : unsigned { Bypass, Gain, PeakReduction, Mode, HiFreq,
        Analog, VuDisplay, Mix, Trim, AutoMakeup, ParameterCount };

    void prepare(double sampleRate, unsigned channels, unsigned /*maxBlockSize*/) {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 || channels < 1 || channels > 2)
            throw std::invalid_argument("CLA2A supports 1/2 channels and positive finite rate");
        rate_ = sampleRate;
        channels_ = channels;
        const auto cascade = designSidechainCascade(rate_);
        for (auto& f : sidechain_) f.set(cascade);
        sidechainRefDb_ = cascadeGainDb(cascade, 1000.0, rate_);
        buildCurves();
        updateDerived();
        reset();
    }
    void reset() noexcept {
        ballistics_.reset();
        noise_.reset(0);
        humReset();
        mix_.reset(mixTarget_);
        out_.reset(outTarget_);
        hp_.fill(0.0);
        for (auto& f : sidechain_) f.reset();
        meterGain_ = 1.0;
        fresh_ = true;
    }
    void setParameter(unsigned index, float physicalValue) noexcept {
        if (index >= ParameterCount || !std::isfinite(physicalValue)) return;
        float v = std::clamp(physicalValue, minimum_[index], maximum_[index]);
        if (discrete_[index]) v = std::round(v);
        parameters_[index] = v;
        if (index == Mode) buildCurves();
        updateDerived();
        // Before the first processed sample, controls take effect without a ramp.
        if (fresh_) { mix_.reset(mixTarget_); out_.reset(outTarget_); }
    }
    float getParameter(unsigned index) const noexcept {
        return index < ParameterCount ? parameters_[index] : 0.0f;
    }
    // Whole parameter space renders; only dry/bypass is fidelity-verified.
    bool supportedConfiguration() const noexcept { return true; }
    bool fidelityVerified() const noexcept { return parameters_[Bypass] != 0 || parameters_[Mix] == 0; }
    double gainReduction() const noexcept { return meterGain_; }
    // Opto ballistics time constants (attack ms at loud/quiet end of the
    // 12-point table, release s). Defaults: Characterized:: (see below).
    struct TimeConstants { double attackFastMs[2], attackSlowMs[2], releaseFastS, releaseSlowS; };
    void setTimeConstants(const TimeConstants& t) noexcept { times_ = t; buildCurves(); }

    void process(const float* const* inputs, float* const* outputs, unsigned frames) noexcept {
        if (!outputs) return;
        const bool bypass = parameters_[Bypass] != 0;
        if (frames) fresh_ = false;
        for (unsigned n = 0; n < frames; ++n) {
            std::array<double, 2> x{};
            for (unsigned c = 0; c < channels_; ++c)
                if (inputs && inputs[c] && std::isfinite(inputs[c][n])) x[c] = inputs[c][n];
            if (bypass) {
                for (unsigned c = 0; c < channels_; ++c) if (outputs[c]) outputs[c][n] = float(x[c]);
                continue;
            }
            const auto dither = noise_.next();
            const double hum = humOn_ ? hum_.next() * Provisional::HumLevel : 0.0;
            double level = 0.0;
            std::array<double, 2> d{};
            for (unsigned c = 0; c < channels_; ++c) {
                d[c] = x[c] + dither.first * 1e-8;
                // Hi Freq: sidechain high-band emphasis (structure provisional).
                // Recovered 48 kHz cascade weights the detector input (SidechainFilters.h).
                const double w = sidechain_[c].process(d[c]);
                hp_[c] = hpPole_ * hp_[c] + (1.0 - hpPole_) * w;
                const double sc = (w - hp_[c]) * hfWeight_ + hp_[c];
                level = std::max(level, detect(sc * drive_));
            }
            const double target = interpolateCurve(level, curveX_.data(), curveY_.data(), Provisional::CurvePoints);
            const Poles attack = attackPoles(level, 1.0, attackLevel_.data(), attackFast_.data(),
                                             attackSlow_.data(), Provisional::AttackPoints);
            const double g = ballistics_.advance(target, attack, release_);
            meterGain_ = g;
            const double mix = mix_.next(mixTarget_, smoothPole_);
            const double out = out_.next(outTarget_, smoothPole_);
            for (unsigned c = 0; c < channels_; ++c) {
                if (!outputs[c]) continue;
                const double wet = d[c] * g * makeup_ + hum;
                const double y = ((wet - x[c]) * mix + x[c]) * out;
                outputs[c][n] = std::isfinite(y) ? float(y) : 0.0f;
            }
        }
    }
    unsigned latencySamples() const noexcept { return 0; }
    double tailSeconds() const noexcept { return 5.0 * times_.releaseSlowS; }

private:
    static double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }
    static double cascadeGainDb(const std::array<Biquad, 3>& c, double hz, double fs) noexcept;
    double pole(double seconds) const noexcept { return std::exp(-1.0 / (seconds * rate_)); }
    void buildCurves() noexcept {
        // CHARACTERIZED (not recovered) static curve, see CharacterizedCurve.h.
        const auto& gr = parameters_[Mode] != 0 ? Characterized::LimitGrDb : Characterized::CompGrDb;
        for (unsigned i = 0; i < Provisional::CurvePoints; ++i) {
            // The curve was characterized with 1 kHz tones; the detector now
            // sees the sidechain cascade, so its grid moves by the 1 kHz gain.
            curveX_[i] = dbToGain(Characterized::DetectorDb0 + Characterized::DetectorStepDb * i + sidechainRefDb_);
            curveY_[i] = dbToGain(gr[i]);
        }
        for (unsigned i = 0; i < Provisional::AttackPoints; ++i) {
            const double t = double(i) / (Provisional::AttackPoints - 1); // loud -> quiet
            attackLevel_[i] = dbToGain(-40.0 * t);
            const double fastMs = times_.attackFastMs[0] + (times_.attackFastMs[1] - times_.attackFastMs[0]) * t;
            const double slowMs = times_.attackSlowMs[0] + (times_.attackSlowMs[1] - times_.attackSlowMs[0]) * t;
            attackFast_[i] = pole(fastMs * 1e-3);
            attackSlow_[i] = pole(slowMs * 1e-3);
        }
        release_ = {pole(times_.releaseFastS), pole(times_.releaseSlowS), 0.75};
    }
    void updateDerived() noexcept {
        drive_ = dbToGain(peakReductionDriveDb(parameters_[PeakReduction]));
        makeup_ = dbToGain(gainMakeupDb(parameters_[Gain]) + Characterized::ProgrammeLevelDb);
        mixTarget_ = parameters_[Mix] / 100.0;
        outTarget_ = dbToGain(parameters_[Trim]);
        hfWeight_ = parameters_[HiFreq] / 50.0;
        hpPole_ = std::exp(-2.0 * 3.14159265358979323846 * Provisional::HiFreqHz / rate_);
        smoothPole_ = pole(Provisional::SmoothS);
        const bool on = parameters_[Analog] < 2;
        if (on != humOn_ || on) {
            const double hz = parameters_[Analog] == 0 ? 50.0 : 60.0;
            const double w = 2.0 * 3.14159265358979323846 * hz / rate_;
            hum_.setRotation(std::cos(w), std::sin(w));
            hum_.setShape(0.0, 1.0);
        }
        humOn_ = on;
    }
    void humReset() noexcept { hum_.reset(1.0, 0.0); }

    double rate_ = 48000;
    unsigned channels_ = 2;
    std::array<float, ParameterCount> parameters_{0, 40, 46, 0, 50, 2, 1, 100, 0, 0};
    inline static constexpr std::array<float, ParameterCount> minimum_{0,0,0,0,0,0,0,0,-18,0};
    inline static constexpr std::array<float, ParameterCount> maximum_{1,100,100,1,100,2,2,100,18,1};
    inline static constexpr std::array<bool, ParameterCount> discrete_{true,false,false,true,false,true,true,false,false,true};
    std::array<double, Provisional::CurvePoints> curveX_{}, curveY_{};
    std::array<double, Provisional::AttackPoints> attackLevel_{}, attackFast_{}, attackSlow_{};
    Poles release_{0, 0, 0.75};
    // CHARACTERIZED (not recovered): fitted by tests/cla2a/fit_ballistics.py to
    // genuine PR 46/70 step bursts (161805, 161819); default renders not used.
    // Bounded point of an ill-conditioned search (quiet-end attack drifts to
    // unphysical extremes if left unbounded): cost 0.1014 (sum of squared dB RMS).
    TimeConstants times_{{4.0, 0.3125}, {10.0, 960.0}, 0.24, 3.0};
    Ballistics ballistics_;
    NoiseSource noise_;
    HumOscillator hum_;
    bool humOn_ = false;
    Smoother mix_, out_;
    std::array<double, 2> hp_{};
    std::array<SidechainCascade, 2> sidechain_{};
    double sidechainRefDb_ = 0;
    double drive_ = 1, makeup_ = 1, mixTarget_ = 1, outTarget_ = 1, hfWeight_ = 1, hpPole_ = 0, smoothPole_ = 0;
    double meterGain_ = 1;
    bool fresh_ = true;
};
inline double Processor::cascadeGainDb(const std::array<Biquad, 3>& c, double hz, double fs) noexcept {
    const double w = 2.0 * 3.14159265358979323846 * hz / fs;
    double mag = 1.0;
    for (const auto& q : c) {
        const double nr = q.b0 + q.b1 * std::cos(w) + q.b2 * std::cos(2 * w), ni = -q.b1 * std::sin(w) - q.b2 * std::sin(2 * w);
        const double dr = 1.0 + q.a1 * std::cos(w) + q.a2 * std::cos(2 * w), di = -q.a1 * std::sin(w) - q.a2 * std::sin(2 * w);
        mag *= std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return 20.0 * std::log10(mag);
}
}
