// SPDX-License-Identifier: GPL-3.0-or-later
// Native test of the product wrapper: the real shared Adapter built from plugins/deesser
// (generated metadata + Processor.hpp + Editor.hpp), as shipped in the VST3/CLAP targets.
// Checks identity/metadata, that the product editor (not the fixture fallback) is attached
// and bound through APVTS, state round trip into DSP and UI, rendering identical to the
// portable core, and the meters it feeds (output peaks; Energy/Atten current post-block values).
#include <juce_audio_utils/juce_audio_utils.h>
#include "Generated.hpp"
#include "Editor.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
using namespace tsunami::deesser;
using Product = tsunami::Adapter<tsunami::PluginSpec>;
#define CHECK(c, code, what) do { if (!(c)) { std::cerr << "FAIL " << what << "\n"; return code; } } while (0)

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
template<class T> static T* child(juce::Component& root, const char* id) { return dynamic_cast<T*>(root.findChildWithID(id)); }
static void setPhysical(juce::AudioProcessor& p, const char* id, float v) {
    for (auto* candidate : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(candidate); r && r->getParameterID() == id)
            return r->setValueNotifyingHost(r->convertTo0to1(v));
    std::cerr << "FAIL unknown parameter " << id << "\n"; std::exit(90);
}
static float physical(juce::AudioProcessor& p, const char* id) {
    for (auto* candidate : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(candidate); r && r->getParameterID() == id)
            return r->convertFrom0to1(r->getValue());
    return NAN;
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    CHECK(dynamic_cast<Product*>(p.get()), 1, "createPluginFilter builds the shared Adapter for PluginSpec");
    CHECK(p->getName() == "Tsunami De-Esser" && juce::String(tsunami::PluginSpec::id) == "deesser", 2, "product identity");
    const auto& params = p->getParameters();
    CHECK(params.size() == int(ui::parameterCount), 3, "parameter count");
    for (unsigned i = 0; i < ui::parameterCount; ++i) {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(params[int(i)]);
        CHECK(r && r->getParameterID() == ui::parameterIds[i] && r->getNormalisableRange().start == ui::minimum[i]
              && r->getNormalisableRange().end == ui::maximum[i] && std::abs(physical(*p, ui::parameterIds[i]) - ui::defaults[i]) < .5f,
              4, "parameter order/range/default match plugin metadata");
    }
    CHECK(params[2]->getCurrentValueAsText() == "HighPass" && params[3]->getCurrentValueAsText() == "Split"
          && params[4]->getCurrentValueAsText() == "Output", 5, "choice labels exposed to hosts");

    p->setPlayConfigDetails(2, 2, 48000, 512);
    p->prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditorAndMakeActive());
    auto* editor = dynamic_cast<Editor*>(ed.get());
    CHECK(editor, 6, "product DeEsser editor attached (not the fixture fallback)");
    auto* audio = child<juce::TextButton>(*ed, "audio");
    auto* freq = child<juce::Slider>(*ed, "freq");
    auto* threshold = child<juce::Slider>(*ed, "threshold");
    auto* sidechain = child<juce::ComboBox>(*ed, "sidechain");
    auto* monA = child<juce::TextButton>(*ed, "monitor_audio");
    auto* monS = child<juce::TextButton>(*ed, "monitor_sidechain");
    CHECK(audio && freq && threshold && sidechain && monA && monS, 7, "panel controls present");
    CHECK(audio->getButtonText() == "Split" && std::lround(freq->getValue()) == 5506 && sidechain->getText() == "HighPass"
          && monA->getToggleState() && !monS->getToggleState(), 8, "default display from Adapter state");
    // UI -> host parameter.
    audio->setToggleState(true, juce::sendNotificationSync);
    freq->setValue(4500, juce::sendNotificationSync);
    sidechain->setSelectedItemIndex(0, juce::sendNotificationSync);
    monS->setToggleState(true, juce::sendNotificationSync); // click path: Monitor pair -> one parameter
    CHECK(physical(*p, "audio") == 1.f && audio->getButtonText() == "Wideband", 9, "Audio button binds");
    CHECK(std::abs(physical(*p, "freq") - 4500.f) < 1.f, 10, "Freq box binds");
    CHECK(physical(*p, "sidechain") == 0.f && physical(*p, "monitor") == 1.f, 11, "SideChain/Monitor bind");
    // Host automation -> UI.
    setPhysical(*p, "monitor", 0.f); setPhysical(*p, "threshold", -20.f);
    CHECK(monA->getToggleState() && !monS->getToggleState() && std::abs(threshold->getValue() + 20.0) < .06, 12, "host changes reach the UI");
    // State round trip through the Adapter's schema.
    juce::MemoryBlock saved; p->getStateInformation(saved);
    setPhysical(*p, "audio", 0.f); setPhysical(*p, "freq", 12000.f);
    p->setStateInformation(saved.getData(), int(saved.getSize()));
    CHECK(physical(*p, "audio") == 1.f && std::abs(physical(*p, "freq") - 4500.f) < 1.f && audio->getButtonText() == "Wideband"
          && std::abs(freq->getValue() - 4500.0) < 1.0, 13, "state restore reaches parameters and UI");

    // Rendering: the Adapter passes physical values to the core unchanged (bit-identical to the portable core).
    setPhysical(*p, "threshold", -40.f); setPhysical(*p, "audio", 0.f); setPhysical(*p, "sidechain", 1.f);
    Processor reference;
    for (unsigned i = 0; i < ui::parameterCount; ++i) reference.setParameter(i, physical(*p, ui::parameterIds[i]));
    reference.prepare(48000, 2, 512);
    p->reset();
    juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
    std::vector<float> rl(512), rr(512);
    double inEnergy = 0, outEnergy = 0, maxDiff = 0;
    std::uint64_t audioHash = 1469598103934665603ull; // FNV-1a over every output float (complete buffers)
    for (int k = 0; k < 16; ++k) {
        for (int i = 0; i < 512; ++i) {
            const float x = .5f * std::sin(2.f * 3.14159265f * 7000.f * float(k * 512 + i) / 48000.f);
            b.setSample(0, i, x); b.setSample(1, i, .5f * x); rl[size_t(i)] = x; rr[size_t(i)] = .5f * x; inEnergy += double(x) * x;
        }
        const float* in[]{rl.data(), rr.data()}; float* out[]{rl.data(), rr.data()};
        { juce::ScopedNoDenormals sameFloatModeAsAdapter; reference.process(in, out, 512); }
        p->processBlock(b, m);
        for (int i = 0; i < 512; ++i) {
            maxDiff = std::max({maxDiff, double(std::abs(b.getSample(0, i) - rl[size_t(i)])), double(std::abs(b.getSample(1, i) - rr[size_t(i)]))});
            outEnergy += double(b.getSample(0, i)) * b.getSample(0, i);
            for (int c = 0; c < 2; ++c) {
                const float v = b.getSample(c, i); unsigned char bytes[sizeof v]; std::memcpy(bytes, &v, sizeof v);
                for (auto byte : bytes) audioHash = (audioHash ^ byte) * 1099511628211ull;
            }
        }
    }
    std::cout << "AUDIO_FNV1A64 " << std::hex << audioHash << std::dec << "\n";
    CHECK(maxDiff == 0.0, 14, "Adapter render bit-identical to tsunami::deesser::Processor");
    CHECK(outEnergy < .5 * inEnergy, 15, "7 kHz sibilant attenuated at Threshold -40 dB through the wrapper");

    // Meters: output peaks from the shared MeterSource; Energy/Atten from the core's current
    // post-block meter(0/1) published through the shared CoreMeterSource (observation only).
    auto* coreMeters = dynamic_cast<tsunami::CoreMeterSource*>(p.get());
    CHECK(coreMeters && coreMeters->coreMeterCount() == 2, 16, "core Energy/Atten meters exposed through the shared Adapter");
    CHECK(coreMeters->coreMeter(0) == reference.meter(0) && coreMeters->coreMeter(1) == reference.meter(1)
          && coreMeters->coreMeter(1) >= 3.f && coreMeters->coreMeter(2) == 0.f, 17, "published values equal the core's post-block meters");
    editor->pollMetersForTest();
    CHECK(editor->meterModel().outputPeakDb[0] > -20.f && editor->meterModel().outputPeakDb[1] > editor->meterModel().outputPeakDb[0] - 7.f,
          18, "Output meters fed by the Adapter's MeterSource");
    CHECK(editor->meterModel().energyDb == coreMeters->coreMeter(0) && editor->meterModel().attenDb == coreMeters->coreMeter(1), 19,
          "Energy/Atten bars fed from the Adapter's CoreMeterSource");
    for (const char* id : {"energy", "atten"}) {
        auto* readout = dynamic_cast<Readout*>(ed->findChildWithID(juce::String(id) + "_readout"));
        auto* bar = dynamic_cast<BarMeter*>(ed->findChildWithID(juce::String(id) + "_meter"));
        CHECK(readout && readout->text != "--" && bar && bar->available, 20, "Energy/Atten readouts and bars show data");
    }
    // Current post-block values, not a lifetime maximum: after 2 s of silence the published meters
    // fall while the core's held take*() peaks (separate contract) still report the loud passage.
    const float heldReduction = reference.gainReductionDb(), heldDetector = reference.detectorLevelDb();
    for (int k = 0; k < 188; ++k) {
        b.clear(); std::fill(rl.begin(), rl.end(), 0.f); std::fill(rr.begin(), rr.end(), 0.f);
        const float* in[]{rl.data(), rr.data()}; float* out[]{rl.data(), rr.data()};
        { juce::ScopedNoDenormals sameFloatModeAsAdapter; reference.process(in, out, 512); }
        p->processBlock(b, m);
    }
    CHECK(coreMeters->coreMeter(0) == reference.meter(0) && coreMeters->coreMeter(1) == reference.meter(1)
          && coreMeters->coreMeter(1) < heldReduction - 1.f && coreMeters->coreMeter(0) < heldDetector - 10.f
          && reference.gainReductionDb() == heldReduction, 21, "published meters are current, not held maxima");
    editor->pollMetersForTest();
    CHECK(editor->meterModel().attenDb == coreMeters->coreMeter(1) && editor->meterModel().attenPeakDb >= 3.f, 22,
          "Atten bar follows the current value; the editor's own peak readout holds");
    // Paint the product editor: non-uniform image of the expected size.
    auto image = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(image.getWidth() == 380 && image.getHeight() == 380 && image.getPixelAt(5, 5) != image.getPixelAt(158, 200), 23, "editor renders");
    ed.reset();
    p->releaseResources();
    std::cout << "PASS DeEsser real Adapter: identity, editor bindings, state, render parity, meters\n";
    return 0;
}
