// SPDX-License-Identifier: GPL-3.0-or-later
// Framework-independent editor model: control set vs genuine parameter list, bindings,
// state save/restore, and meters fed from the core's realtime-safe meters.
#include "EditorModel.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace tsunami::deesser;
static void require(bool b, const char* m) { if (!b) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(1); } }
static bool near(float a, float b, float e = 1e-4f) { return std::abs(a - b) <= e; }

int main(int argc, char** argv) {
    // 1. Every genuine parameter id (parameters.json) is bound by exactly one control; no invented ids.
    require(argc > 1, "usage: test_editor_model <parameters.json>");
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); const std::string json = ss.str();
    require(!json.empty(), "parameters.json readable");
    for (const auto& c : ui::controls) {
        require(c.parameter < ui::parameterCount, "control binds a real parameter index");
        const std::string needle = std::string("\"id\": \"") + c.id + "\"";
        require(json.find(needle) != std::string::npos, "control id exists in genuine parameter list");
        require(std::strcmp(ui::parameterIds[c.parameter], c.id) == 0, "control id matches its parameter index");
    }
    for (unsigned i = 0; i < ui::parameterCount; ++i) {
        unsigned bound = 0; for (const auto& c : ui::controls) bound += c.parameter == i;
        require(bound == 1, "each genuine parameter has exactly one binding");
    }
    require(json.find("\"range\"") == std::string::npos, "no Range parameter in genuine list (not invented)");

    // 2. Bindings: physical clamp/quantise, normalised taper, display text.
    ui::ParameterState s;
    require(near(s.get(0), 5506) && near(s.get(1), 0) && near(s.get(2), 1) && near(s.get(3), 0) && near(s.get(4), 0) && near(s.get(5), 0),
            "defaults equal genuine defaults");
    s.set(0, 4499.6f); require(near(s.get(0), 4500), "freq bound to whole Hz");
    s.set(0, 99999); require(near(s.get(0), 16000), "freq clamps to 16 kHz");
    s.set(1, -43.64f); require(near(s.get(1), -43.6f), "threshold 0.1 dB steps");
    s.set(1, -200); require(near(s.get(1), -80), "threshold clamps to -80 dB");
    s.set(2, .2f); require(near(s.get(2), 0), "sidechain choice rounds to BandPass");
    s.set(3, .7f); require(near(s.get(3), 1), "audio choice rounds to Wideband");
    s.setNormalised(0, .5f); require(near(s.get(0), 5657, 1), "freq taper 2000*8^n at n=0.5");
    require(near(s.normalised(0), .5f, 1e-3f), "freq normalised inverse");
    s.setNormalised(1, .25f); require(near(s.get(1), -60), "threshold linear -80..0");
    require(ui::valueText(0, 5506) == "5506", "freq box text");
    require(ui::valueText(1, -43.6f) == "-43.6", "threshold handle text");
    require(ui::valueText(3, 0) == "Split" && ui::valueText(3, 1) == "Wideband", "Audio button text");
    require(ui::valueText(4, 0) == "Audio" && ui::valueText(4, 1) == "S Chain", "Monitor button text");
    require(ui::valueText(2, 0) == "BandPass" && ui::valueText(2, 1) == "HighPass", "SideChain selector text");

    // 3. State save/restore round trip, rejection of foreign/garbled state, and core application.
    ui::ParameterState a; a.set(0, 7200); a.set(1, -31.5f); a.set(2, 0); a.set(3, 1); a.set(4, 1); a.set(5, 1);
    const std::string blob = a.save();
    ui::ParameterState b; require(b.restore(blob), "restore own state");
    for (unsigned i = 0; i < ui::parameterCount; ++i) require(near(a.get(i), b.get(i)), "restored value equals saved");
    ui::ParameterState c; c.set(1, -12);
    require(!c.restore("s1 1 gain=0"), "foreign plugin state rejected");
    require(!c.restore("deesser 9 freq=5000"), "unknown schema rejected");
    require(!c.restore("deesser 1 freq=abc"), "garbled value rejected");
    require(near(c.get(1), -12), "rejected restore leaves state unchanged");
    ui::ParameterState partial; require(partial.restore("deesser 1 threshold=-20"), "partial state accepted");
    require(near(partial.get(1), -20) && near(partial.get(0), 5506), "missing keys keep defaults");
    Processor p; p.prepare(48000, 1, 512); b.applyTo(p);
    for (unsigned i = 0; i < ui::parameterCount; ++i) require(near(p.getParameter(i), b.get(i)), "state applied to core");

    // 4. Meters: Energy/Atten fed from Processor::detectorLevelDb()/gainReductionDb(); infinite peak hold; click reset.
    ui::MeterModel m;
    require(m.attenDb == 0 && m.attenPeakDb == 0 && m.energyDb <= -120, "meters idle at start");
    Processor q; q.prepare(48000, 1, 512); q.setParameter(1, -40);
    std::vector<float> x(4096), y(4096);
    for (unsigned i = 0; i < 4096; ++i) x[i] = .5f * float(std::sin(2 * 3.14159265358979 * 7000.0 * i / 48000.0));
    const float* in[]{x.data()}; float* out[]{y.data()};
    q.process(in, out, 4096); m.poll(q);
    require(m.attenDb >= 3 && near(m.attenDb, q.gainReductionDb()), "Atten meter reads core gain reduction");
    require(m.energyDb > -40 && near(m.energyDb, q.detectorLevelDb()), "Energy meter reads core detector level");
    const float held = m.attenPeakDb;
    q.reset(); m.poll(q);
    require(m.attenDb == 0 && near(m.attenPeakDb, held), "infinite peak hold keeps the maximum");
    m.resetPeaks(); require(m.attenPeakDb == 0 && m.energyPeakDb <= -120, "click resets peak readouts");
    m.pollOutput(.5f, .25f);
    require(near(m.outputPeakDb[0], -6.0206f, 1e-3f) && near(m.outputPeakDb[1], -12.0412f, 1e-3f), "output peak readouts in dBFS");

    // 5. Scales (measured from the manual figure): Threshold/Energy share one 0..-80 scale; Atten ticks; Output 0..-30.
    require(near(ui::thresholdFraction(0), 0) && near(ui::thresholdFraction(-80), 1) && near(ui::thresholdFraction(-40), .5f),
            "threshold slider and energy bar aligned");
    require(near(ui::attenFraction(0), 0) && ui::attenFraction(3) < ui::attenFraction(6) && ui::attenFraction(18) < 1 &&
            near(ui::attenFraction(1000), 1), "Atten scale monotone to Inf");
    require(near(ui::outputFraction(0), 0) && near(ui::outputFraction(-30), 1) && near(ui::outputFraction(-60), 1), "Output scale 0..-30");
    std::puts("PASS editor model bindings, state and meters");
}
