// SPDX-License-Identifier: GPL-3.0-or-later
// Framework-independent DeEsser editor model: control set (genuine parameter list),
// value bindings/text, state save/restore and meter model fed from the core's
// realtime-safe meters. Mirrors the S1 EditorModel split (model testable without JUCE).
#pragma once
#include "Processor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace tsunami::deesser {
// Optional editor tap for the Energy and Atten meters. The shared foundation Adapter
// exposes only output peaks (MeterSource); a host wrapper must implement this to feed
// the core's Processor::detectorLevelDb()/gainReductionDb(). Without it the editor
// draws those meters without data (never faked).
struct DetectorMeterSource {
    virtual ~DetectorMeterSource() = default;
    virtual float detectorLevelDb() const noexcept = 0;
    virtual float gainReductionDb() const noexcept = 0;
};

namespace ui {
inline constexpr unsigned parameterCount = 6;
inline constexpr const char* parameterIds[parameterCount]{"freq", "threshold", "sidechain", "audio", "monitor", "bypass"};
inline constexpr float minimum[parameterCount]{2000, -80, 0, 0, 0, 0};
inline constexpr float maximum[parameterCount]{16000, 0, 1, 1, 1, 1};
inline constexpr float defaults[parameterCount]{5506, 0, 1, 0, 0, 0};

// Original panel (manual ch.4 figure): Audio button; Frequency value window; SideChain
// filter-shape selector; Monitor Audio/S Chain pair; Threshold slider beside the Energy bar.
// Bypass has no panel control in the original (host/toolbar); it is bound to host state only.
enum class Kind { Toggle, ValueBox, FilterSelector, MonitorPair, Fader, HostOnly };
struct Control { const char* id; const char* label; Kind kind; unsigned parameter; };
inline constexpr Control controls[]{
    {"audio", "Audio", Kind::Toggle, 3},
    {"freq", "Frequency", Kind::ValueBox, 0},
    {"sidechain", "SideChain", Kind::FilterSelector, 2},
    {"monitor", "Monitor", Kind::MonitorPair, 4},
    {"threshold", "Threshold", Kind::Fader, 1},
    {"bypass", "Bypass", Kind::HostOnly, 5},
};

inline float quantise(unsigned i, float v) noexcept {
    if (i >= parameterCount || !std::isfinite(v)) return i < parameterCount ? defaults[i] : 0.f;
    v = std::clamp(v, minimum[i], maximum[i]);
    if (i == 0) return std::round(v);                      // core binds Freq to whole Hz
    if (i == 1) return std::round(v * 10.f) / 10.f;        // genuine step 0.1 dB
    return v >= .5f ? 1.f : 0.f;                           // choices / bypass
}

class ParameterState {
    std::array<float, parameterCount> v_{{defaults[0], defaults[1], defaults[2], defaults[3], defaults[4], defaults[5]}};
public:
    float get(unsigned i) const noexcept { return i < parameterCount ? v_[i] : 0.f; }
    void set(unsigned i, float value) noexcept { if (i < parameterCount) v_[i] = quantise(i, value); }
    // Genuine tapers: Freq 2000*8^n (log), Threshold linear -80..0, choices 0/1.
    float normalised(unsigned i) const noexcept {
        if (i == 0) return float(std::log(v_[0] / 2000.0) / std::log(8.0));
        return i < parameterCount ? (v_[i] - minimum[i]) / (maximum[i] - minimum[i]) : 0.f;
    }
    void setNormalised(unsigned i, float n) noexcept {
        n = std::clamp(n, 0.f, 1.f);
        set(i, i == 0 ? float(2000.0 * std::pow(8.0, double(n))) : minimum[i] + n * (maximum[i] - minimum[i]));
    }
    std::string save() const {
        std::string s = "deesser 1";
        char buf[64];
        for (unsigned i = 0; i < parameterCount; ++i) { std::snprintf(buf, sizeof buf, " %s=%.9g", parameterIds[i], double(v_[i])); s += buf; }
        return s;
    }
    // All-or-nothing: on any error the state is left unchanged.
    bool restore(const std::string& s) {
        std::istringstream in(s); std::string plugin, schema, token;
        if (!(in >> plugin >> schema) || plugin != "deesser" || schema != "1") return false;
        auto next = v_;
        while (in >> token) {
            const auto eq = token.find('='); if (eq == std::string::npos) return false;
            const std::string key = token.substr(0, eq), value = token.substr(eq + 1);
            unsigned i = 0; while (i < parameterCount && key != parameterIds[i]) ++i;
            if (i == parameterCount || value.empty()) return false;
            char* end = nullptr; const float x = std::strtof(value.c_str(), &end);
            if (*end != '\0' || !std::isfinite(x)) return false;
            next[i] = quantise(i, x);
        }
        v_ = next; return true;
    }
    void applyTo(Processor& p) const noexcept { for (unsigned i = 0; i < parameterCount; ++i) p.setParameter(i, v_[i]); }
};

inline std::string valueText(unsigned i, float v) {
    char buf[32];
    switch (i) {
    case 0: std::snprintf(buf, sizeof buf, "%d", int(std::lround(v))); return buf;
    case 1: std::snprintf(buf, sizeof buf, "%.1f", double(v)); return buf;
    case 2: return v >= .5f ? "HighPass" : "BandPass";
    case 3: return v >= .5f ? "Wideband" : "Split";
    case 4: return v >= .5f ? "S Chain" : "Audio";   // original panel labels for Output/SideChain
    case 5: return v >= .5f ? "Bypassed" : "Active";
    default: return {};
    }
}

// Scales, top = 0, bottom = 1. Threshold slider and Energy bar share 0..-80 dB (manual:
// "aligned for easy adjustments"). Atten tick fractions measured from the manual ch.4
// figure (meter top y232, Inf y453; ticks 3/6/12/18 dB at y254/284/342/400, +-2 px);
// between ticks linear; beyond 18 dB the 6..18 dB slope continues to Inf (provisional).
inline float thresholdFraction(float db) noexcept { return std::clamp(-db / 80.f, 0.f, 1.f); }
inline float outputFraction(float dbfs) noexcept { return std::clamp(-dbfs / 30.f, 0.f, 1.f); }
inline float attenFraction(float grDb) noexcept {
    constexpr float db[]{0, 3, 6, 12, 18}, at[]{0.f, 22.f / 221, 52.f / 221, 110.f / 221, 168.f / 221};
    if (!(grDb > 0)) return 0.f;
    for (int k = 1; k < 5; ++k)
        if (grDb <= db[k]) return at[k - 1] + (grDb - db[k - 1]) * (at[k] - at[k - 1]) / (db[k] - db[k - 1]);
    return std::min(1.f, at[4] + (grDb - 18.f) * (at[4] - at[2]) / 12.f);
}
inline float toDbfs(float linear) noexcept { return linear > 1e-6f ? 20.f * std::log10(linear) : -120.f; }

// Readouts below the meters show infinite peak hold; clicking resets (manual ch.2/ch.4).
struct MeterModel {
    float energyDb = -120, energyPeakDb = -120, attenDb = 0, attenPeakDb = 0;
    std::array<float, 2> outputDb{{-120, -120}}, outputPeakDb{{-120, -120}};
    void pollDetector(float detectorDb, float grDb) noexcept {
        energyDb = std::isfinite(detectorDb) ? detectorDb : -120.f;
        attenDb = std::isfinite(grDb) ? std::max(0.f, grDb) : 0.f;
        energyPeakDb = std::max(energyPeakDb, energyDb); attenPeakDb = std::max(attenPeakDb, attenDb);
    }
    void poll(const Processor& p) noexcept { pollDetector(p.detectorLevelDb(), p.gainReductionDb()); }
    void poll(const DetectorMeterSource& s) noexcept { pollDetector(s.detectorLevelDb(), s.gainReductionDb()); }
    void pollOutput(float leftPeak, float rightPeak) noexcept {
        outputDb = {toDbfs(leftPeak), toDbfs(rightPeak)};
        for (int k = 0; k < 2; ++k) outputPeakDb[k] = std::max(outputPeakDb[k], outputDb[k]);
    }
    void resetPeaks() noexcept { energyPeakDb = -120; attenPeakDb = 0; outputPeakDb = {-120, -120}; }
};
} // namespace ui
} // namespace tsunami::deesser
