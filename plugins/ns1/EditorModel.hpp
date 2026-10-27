// SPDX-License-Identifier: GPL-3.0-or-later
// Framework-independent NS1 editor model: control set (genuine parameter list),
// value bindings and display text, state save/restore, and the attenuation meter
// model fed from the Processor's realtime-safe atomic. Mirrors the S1/DeEsser
// EditorModel split so the model is testable without JUCE.
#pragma once
#include "Processor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace tsunami::ns1 {
// Optional editor tap for the attenuation meter. The shared foundation Adapter only
// exposes output peaks (MeterSource). A host wrapper must implement this to forward
// Processor::attenuationDb(). Without it the editor draws the meter without data
// (never faked).
struct AttenuationMeterSource {
    virtual ~AttenuationMeterSource() = default;
    virtual float attenuationDb() const noexcept = 0;
};

namespace ui {
inline constexpr unsigned parameterCount = 2;
inline constexpr const char* parameterIds[parameterCount]{"ns1", "bypass"};
inline constexpr float minimum[parameterCount]{0, 0};
inline constexpr float maximum[parameterCount]{100, 1};
inline constexpr float defaults[parameterCount]{0, 0};
inline constexpr float meterFloorDb = -48.f; // ParamXML PARAM_ATTN_VU Minimum

// Original panel (manual ch.3 figure): one vertical fader 0..100 with a value window
// below it, and an attenuation meter 0..-48 dB with a readout. Bypass has no panel
// control in the original (host / WaveShell toolbar), so it is bound to host state only.
enum class Kind { Fader, HostOnly };
struct Control { const char* id; const char* label; Kind kind; unsigned parameter; };
inline constexpr Control controls[]{
    {"ns1", "NS1", Kind::Fader, 0},
    {"bypass", "Bypass", Kind::HostOnly, 1},
};

inline float quantise(unsigned i, float v) noexcept {
    if (i >= parameterCount) return 0.f;
    if (!std::isfinite(v)) return defaults[i];
    v = std::clamp(v, minimum[i], maximum[i]);
    if (i == 0) return std::round(v * 10.f) / 10.f; // ParamXML Resolution 0.1 %
    return v >= .5f ? 1.f : 0.f;
}

class ParameterState {
    std::array<float, parameterCount> v_{{defaults[0], defaults[1]}};
public:
    float get(unsigned i) const noexcept { return i < parameterCount ? v_[i] : 0.f; }
    void set(unsigned i, float value) noexcept { if (i < parameterCount) v_[i] = quantise(i, value); }
    // ParamXML ScaleType linear for the fader; bypass 0/1.
    float normalised(unsigned i) const noexcept {
        return i < parameterCount ? (v_[i] - minimum[i]) / (maximum[i] - minimum[i]) : 0.f;
    }
    void setNormalised(unsigned i, float n) noexcept {
        if (i < parameterCount) set(i, minimum[i] + std::clamp(n, 0.f, 1.f) * (maximum[i] - minimum[i]));
    }
    std::string save() const {
        std::string s = "ns1 1"; char buf[64];
        for (unsigned i = 0; i < parameterCount; ++i) { std::snprintf(buf, sizeof buf, " %s=%.9g", parameterIds[i], double(v_[i])); s += buf; }
        return s;
    }
    // All-or-nothing: on any error the state is left unchanged.
    bool restore(const std::string& s) {
        std::istringstream in(s); std::string plugin, schema, token;
        if (!(in >> plugin >> schema) || plugin != "ns1" || schema != "1") return false;
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

// ParamXML DisplayFloat1 for the fader (manual figure value window "41.0").
inline std::string valueText(unsigned i, float v) {
    char buf[32];
    if (i == 0) { std::snprintf(buf, sizeof buf, "%.1f", double(v)); return buf; }
    if (i == 1) return v >= .5f ? "On" : "Off";
    return {};
}
// Meter readout, ParamXML AttenVU DisplayFloat1 (manual figure "-46.9").
inline std::string meterText(float db) {
    char buf[32]; std::snprintf(buf, sizeof buf, "%.1f", double(std::round(db * 10.f) / 10.f)); return buf;
}

// Scales, 0 = top, 1 = bottom for the meter; 0 = bottom, 1 = top for the fader travel.
inline float attenuationFraction(float db) noexcept { return std::clamp(db / meterFloorDb, 0.f, 1.f); }
inline float faderFraction(float percent) noexcept { return std::clamp(percent / 100.f, 0.f, 1.f); }

struct MeterModel {
    float attenuationDb = 0;
    void pollValue(float db) noexcept {
        if (!std::isfinite(db)) { attenuationDb = 0; return; }
        attenuationDb = std::round(std::clamp(db, meterFloorDb, 0.f) * 10.f) / 10.f;
    }
    void poll(const Processor& p) noexcept { pollValue(p.attenuationDb()); }
    void poll(const AttenuationMeterSource& s) noexcept { pollValue(s.attenuationDb()); }
};
} // namespace ui
} // namespace tsunami::ns1
