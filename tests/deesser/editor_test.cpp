// SPDX-License-Identifier: GPL-3.0-or-later
// Native JUCE test of the actual DeEsser editor: real components bound through APVTS
// attachments, UI<->parameter sync, state save/restore, and Energy/Atten meters fed from
// the core's realtime-safe meters through DetectorMeterSource. The host processor below
// is a test harness (the shared Adapter does not yet build DeEsser: log taper, choice
// steps and the detector-meter tap are pending in the foundation).
#include <juce_audio_utils/juce_audio_utils.h>
#include "Editor.hpp"
#include <cmath>
#include <iostream>
using namespace tsunami::deesser;
#define CHECK(c, code, what) do { if (!(c)) { std::cerr << "FAIL " << what << "\n"; return code; } } while (0)

class Harness final : public juce::AudioProcessor, public tsunami::MeterSource, public DetectorMeterSource {
public:
    Processor core;
    juce::AudioProcessorValueTreeState state;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        for (unsigned i = 0; i < ui::parameterCount; ++i)
            l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(ui::parameterIds[i], 1), ui::parameterIds[i],
                juce::NormalisableRange<float>(ui::minimum[i], ui::maximum[i], i == 0 ? 1.f : i == 1 ? .1f : 1.f), ui::defaults[i]));
        return l;
    }
    Harness() : AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::mono(), true).withOutput("Out", juce::AudioChannelSet::mono(), true)),
                state(*this, nullptr, "TsunamiState", layout()) {}
    void apply() { for (unsigned i = 0; i < ui::parameterCount; ++i) core.setParameter(i, state.getRawParameterValue(ui::parameterIds[i])->load()); }
    const juce::String getName() const override { return "DeEsser harness"; }
    void prepareToPlay(double r, int b) override { apply(); core.prepare(r, 1, unsigned(b)); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        apply(); const float* in[]{b.getReadPointer(0)}; float* out[]{b.getWritePointer(0)};
        core.process(in, out, unsigned(b.getNumSamples()));
        peak = std::max(peak, b.getMagnitude(0, 0, b.getNumSamples()));
    }
    float peak = 0;
    std::array<float, 4> takeOutputPeaks() noexcept override { auto p = peak; peak = 0; return {p, p, p, 0}; }
    float detectorLevelDb() const noexcept override { return core.detectorLevelDb(); }
    float gainReductionDb() const noexcept override { return core.gainReductionDb(); }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return tsunami::createPluginEditor(*this, state); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override { if (auto x = state.copyState().createXml()) copyXmlToBinary(*x, d); }
    void setStateInformation(const void* d, int n) override { if (auto x = getXmlFromBinary(d, n)) state.replaceState(juce::ValueTree::fromXml(*x)); }
};

template<class T> static T* child(juce::Component& root, const char* id) { return dynamic_cast<T*>(root.findChildWithID(id)); }

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    Harness p; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* editor = dynamic_cast<Editor*>(ed.get());
    CHECK(editor, 1, "actual DeEsser editor");
    auto* audio = child<juce::TextButton>(*ed, "audio");
    auto* freq = child<juce::Slider>(*ed, "freq");
    auto* threshold = child<juce::Slider>(*ed, "threshold");
    auto* sidechain = child<juce::ComboBox>(*ed, "sidechain");
    auto* monA = child<juce::TextButton>(*ed, "monitor_audio");
    auto* monS = child<juce::TextButton>(*ed, "monitor_sidechain");
    CHECK(audio && freq && threshold && sidechain && monA && monS, 2, "all original panel controls present");
    auto value = [&](const char* id) { return p.state.getRawParameterValue(id)->load(); };
    // Defaults shown as on the original panel figure (Split, 5506 Hz, HighPass, Monitor Audio).
    CHECK(audio->getButtonText() == "Split" && std::lround(freq->getValue()) == 5506 && sidechain->getText() == "HighPass"
          && monA->getToggleState() && !monS->getToggleState(), 3, "default display");
    // UI -> parameter.
    audio->setToggleState(true, juce::sendNotificationSync);
    CHECK(value("audio") == 1.f && audio->getButtonText() == "Wideband", 4, "Audio button binds Wideband");
    threshold->setValue(-43.6, juce::sendNotificationSync);
    CHECK(std::abs(value("threshold") + 43.6f) < 1e-3f, 5, "threshold slider binds");
    freq->setValue(4500, juce::sendNotificationSync);
    CHECK(value("freq") == 4500.f, 6, "freq box binds");
    sidechain->setSelectedItemIndex(0, juce::sendNotificationSync);
    CHECK(value("sidechain") == 0.f, 7, "SideChain selector binds BandPass");
    monS->setToggleState(true, juce::sendNotificationSync);
    CHECK(value("monitor") == 1.f && monS->getToggleState() && !monA->getToggleState(), 8, "Monitor S Chain binds");
    // Parameter (host automation) -> UI.
    p.state.getParameter("monitor")->setValueNotifyingHost(0.f);
    p.state.getParameter("threshold")->setValueNotifyingHost(p.state.getParameter("threshold")->convertTo0to1(-20.f));

    CHECK(monA->getToggleState() && std::abs(threshold->getValue() + 20.0) < 1e-3, 9, "host changes reach the UI");
    // State save / restore round trip reflected in the editor.
    juce::MemoryBlock saved; p.getStateInformation(saved);
    p.state.getParameter("audio")->setValueNotifyingHost(0.f);
    p.state.getParameter("freq")->setValueNotifyingHost(p.state.getParameter("freq")->convertTo0to1(12000.f));

    p.setStateInformation(saved.getData(), int(saved.getSize()));

    CHECK(value("audio") == 1.f && value("freq") == 4500.f && audio->getButtonText() == "Wideband" && std::lround(freq->getValue()) == 4500,
          10, "state restore reaches parameters and UI");
    // Meters from the core's realtime-safe meters.
    juce::AudioBuffer<float> b(1, 512); juce::MidiBuffer m;
    p.state.getParameter("threshold")->setValueNotifyingHost(p.state.getParameter("threshold")->convertTo0to1(-40.f));
    for (int k = 0; k < 8; ++k) {
        for (int i = 0; i < 512; ++i) b.setSample(0, i, .5f * std::sin(2.f * 3.14159265f * 7000.f * float(k * 512 + i) / 48000.f));
        p.processBlock(b, m);
    }
    editor->pollMetersForTest();
    CHECK(editor->meterModel().attenDb >= 3.f && std::abs(editor->meterModel().attenDb - p.core.gainReductionDb()) < 1e-4f, 11, "Atten meter fed by core");
    CHECK(std::abs(editor->meterModel().energyDb - p.core.detectorLevelDb()) < 1e-4f, 12, "Energy meter fed by core");
    CHECK(editor->meterModel().outputPeakDb[0] > -20.f, 13, "Output meter fed by MeterSource");
    auto* attenReadout = ed->findChildWithID("atten_readout");
    CHECK(attenReadout, 14, "Atten readout present");
    std::cout << "PASS DeEsser editor bindings, state restore and meters\n";
    return 0;
}
