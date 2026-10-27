// SPDX-License-Identifier: GPL-3.0-or-later
// Framework-independent NS1 editor model: control set vs the genuine parameter list,
// bindings and display text, state save/restore, and the attenuation meter fed from
// the Processor's realtime-safe atomic.
#include "EditorModel.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace tsunami::ns1;
static void require(bool b, const char* m) { if (!b) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(1); } }
static bool near(float a, float b, float e = 1e-4f) { return std::abs(a - b) <= e; }
static std::vector<float> noise(unsigned n, float amp) {
    std::vector<float> v(n); unsigned s = 777;
    for (auto& x : v) { s = s * 1664525u + 1013904223u; x = (float(s >> 8) / 16777216.f - .5f) * amp; }
    return v;
}

int main(int argc, char** argv) {
    // 1. Every genuine parameter (parameters.json) is bound by exactly one control; no invented ids.
    require(argc > 1, "usage: test_editor_model <parameters.json>");
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); const std::string json = ss.str();
    require(!json.empty(), "parameters.json readable");
    for (const auto& c : ui::controls) {
        require(c.parameter < ui::parameterCount, "control binds a real parameter index");
        require(json.find(std::string("\"id\": \"") + c.id + "\"") != std::string::npos, "control id exists in genuine parameter list");
        require(std::strcmp(ui::parameterIds[c.parameter], c.id) == 0, "control id matches its parameter index");
    }
    for (unsigned i = 0; i < ui::parameterCount; ++i) {
        unsigned bound = 0; for (const auto& c : ui::controls) bound += c.parameter == i;
        require(bound == 1, "each genuine parameter has exactly one binding");
    }
    require(ui::parameterCount == Processor::ParameterCount, "model and Processor agree on parameter count");
    require(json.find("\"PARAM_WET_DRY_CNTRL\"") != std::string::npos, "NS1 fader traced to ParamXML 1Knob");
    require(json.find("\"PARAM_ATTN_VU\"") != std::string::npos, "meter traced to ParamXML AttenVU");

    // 2. Bindings: physical clamp/quantise (Resolution 0.1 %), normalised taper, display text.
    ui::ParameterState s;
    require(near(s.get(0), 0) && near(s.get(1), 0), "defaults equal genuine defaults (0 %, Off)");
    s.set(0, 41.04f); require(near(s.get(0), 41.0f), "NS1 0.1 % steps");
    s.set(0, 250); require(near(s.get(0), 100), "NS1 clamps to 100 %");
    s.set(0, -5); require(near(s.get(0), 0), "NS1 clamps to 0 %");
    s.set(0, std::nanf("")); require(near(s.get(0), 0), "NS1 NaN falls back to default");
    s.set(1, .7f); require(near(s.get(1), 1), "bypass rounds to On");
    s.setNormalised(0, .25f); require(near(s.get(0), 25), "NS1 linear taper 0..100");
    require(near(s.normalised(0), .25f), "NS1 normalised inverse");
    require(ui::valueText(0, 41) == "41.0", "fader readout DisplayFloat1 (manual figure shows 41.0)");
    require(ui::valueText(0, 100) == "100.0", "fader readout at full scale");
    require(ui::valueText(1, 0) == "Off" && ui::valueText(1, 1) == "On", "bypass text");
    require(ui::meterText(-46.94f) == "-46.9", "meter readout one decimal (manual figure shows -46.9)");

    // 3. State save/restore round trip, rejection of foreign or garbled state, application to the core.
    ui::ParameterState a; a.set(0, 62.5f); a.set(1, 1);
    const std::string blob = a.save();
    ui::ParameterState b; require(b.restore(blob), "restore own state");
    require(near(b.get(0), 62.5f) && near(b.get(1), 1), "restored values equal saved");
    ui::ParameterState c; c.set(0, 12);
    require(!c.restore("deesser 1 freq=5000"), "foreign plugin state rejected");
    require(!c.restore("ns1 9 ns1=10"), "unknown schema rejected");
    require(!c.restore("ns1 1 ns1=abc"), "garbled value rejected");
    require(!c.restore("ns1 1 range=3"), "unknown parameter rejected");
    require(near(c.get(0), 12), "failed restore leaves state unchanged");
    Processor core; core.prepare(48000, 1, 512); a.applyTo(core);
    require(near(core.getParameter(Processor::Amount), 62.5f) && near(core.getParameter(Processor::Bypass), 1), "state applies to the core");

    // 4. Processor meter: lock-free atomic, 0 dB when idle/bypassed, negative when suppressing.
    require(std::atomic<float>::is_always_lock_free, "meter atomic is lock-free");
    {
        Processor p; p.prepare(48000, 1, 512);
        require(p.attenuationDb() == 0.f, "fresh meter reads 0 dB");
        const auto in = noise(48000, .02f); std::vector<float> out(in.size());
        p.setParameter(Processor::Amount, 100.f);
        for (unsigned k = 0; k + 512 <= in.size(); k += 512) {
            const float* i1[2]{in.data() + k, nullptr}; float* o1[2]{out.data() + k, nullptr}; p.process(i1, o1, 512);
        }
        const float db = p.attenuationDb();
        require(db < -1.f && db >= -48.f, "steady noise at 100 % reads attenuation within -48..0 dB");
        p.setParameter(Processor::Bypass, 1.f);
        const float* i1[2]{in.data(), nullptr}; float* o1[2]{out.data(), nullptr}; p.process(i1, o1, 512);
        require(p.attenuationDb() == 0.f, "bypass reads 0 dB");
        std::vector<float> z(512, 0.f); p.setParameter(Processor::Bypass, 0.f);
        const float* iz[2]{z.data(), nullptr}; float* oz[2]{out.data(), nullptr}; p.process(iz, oz, 512);
        require(p.attenuationDb() == 0.f, "silent input reads 0 dB (no energy removed)");
        // In-place processing measures input energy before overwriting it.
        Processor q; q.prepare(48000, 2, 512); q.setParameter(Processor::Amount, 100.f);
        std::vector<float> l = in, r = in;
        for (unsigned k = 0; k + 512 <= l.size(); k += 512) {
            const float* i2[]{l.data() + k, r.data() + k}; float* o2[]{l.data() + k, r.data() + k}; q.process(i2, o2, 512);
        }
        require(near(q.attenuationDb(), db, .05f), "linked stereo identical channels match mono meter (in place)");
    }

    // 5. Meter model: scale 0..-48 dB (ParamXML AttenVU range), poll from the core, clamps garbage.
    require(near(ui::attenuationFraction(0), 0) && near(ui::attenuationFraction(-48), 1) && near(ui::attenuationFraction(-24), .5f),
            "meter fraction linear on 0..-48 dB");
    require(near(ui::attenuationFraction(-90), 1) && near(ui::attenuationFraction(3), 0), "meter fraction clamps");
    require(near(ui::faderFraction(41), .41f), "fader travel linear 0..100");
    ui::MeterModel m;
    require(near(m.attenuationDb, 0), "meter model starts at 0 dB");
    m.pollValue(-12.34f); require(near(m.attenuationDb, -12.3f), "meter model quantised to 0.1 dB");
    m.pollValue(std::nanf("")); require(near(m.attenuationDb, 0), "NaN reads 0 dB");
    m.pollValue(-90); require(near(m.attenuationDb, -48), "meter model clamps to -48 dB");
    Processor idle; idle.prepare(48000, 1, 512); m.poll(idle); require(near(m.attenuationDb, 0), "poll reads the core atomic");
    std::puts("ns1 editor model: OK");
    return 0;
}
