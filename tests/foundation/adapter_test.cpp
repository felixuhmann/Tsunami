// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <iostream>
#include <utility>
#if __has_include("Adapter.hpp")
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"
#endif
#if __has_include("Adapter.hpp")
struct LocalSpec : tsunami::PluginSpec {
    inline static juce::AudioProcessor* receivedProcessor = nullptr;
    inline static juce::AudioProcessorValueTreeState* receivedState = nullptr;
    static juce::AudioProcessorEditor* createEditor(juce::AudioProcessor& processor,
                                                   juce::AudioProcessorValueTreeState& state) {
        receivedProcessor = &processor; receivedState = &state;
        auto* editor = new juce::GenericAudioProcessorEditor(processor);
        editor->setName("local editor");
        return editor;
    }
};
struct MeteredCore : tsunami::fixture::Processor {
    static constexpr unsigned meterCount = 2;
    unsigned blocks = 0; float lastIn = 0;
    void process(const float* const* in, float* const* out, unsigned frames) noexcept {
        lastIn = in[0][0]; ++blocks; tsunami::fixture::Processor::process(in, out, frames);
    }
    float meter(unsigned index) const noexcept { return index == 0 ? lastIn : -float(blocks); }
};
struct MeteredSpec : tsunami::PluginSpec { using Core = MeteredCore; };
struct MonoSpec : tsunami::PluginSpec { static constexpr bool supportsMono = true; };
struct TaperSpec : tsunami::PluginSpec {
    inline static constexpr tsunami::Parameter parameters[] = {
        {"frequency", "Frequency", 20.f, 20000.f, 1000.f, "Hz", 0.f, 0.198894f},
    };
};
struct ChoiceSpec : tsunami::PluginSpec {
    inline static constexpr tsunami::Parameter parameters[] = {
        {"mode", "Mode", 0.f, 2.f, 1.f, "", 1.f, 1.f, "BandPass|HighPass|Split"},
    };
};
static juce::MemoryBlock stateWithGain(tsunami::Adapter<tsunami::PluginSpec>& p, const char* text) {
    juce::MemoryBlock data; p.getStateInformation(data);
    auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), int(data.getSize()));
    for (auto* child : xml->getChildIterator()) if (child->getStringAttribute("id") == "gain") child->setAttribute("value", text);
    juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary(*xml, out); return out;
}
#endif
int main() {
#if !__has_include("Adapter.hpp")
    std::cerr << "shared adapter is not implemented\n";
    return 1;
#else
    juce::ScopedJuceInitialiser_GUI gui;
    tsunami::Adapter<tsunami::PluginSpec> p;
    p.prepareToPlay(48000, 64);
    if (p.getParameters().size()!=1 || p.getLatencySamples()!=7 || p.getTailLengthSeconds()!=.25) return 2;
    auto* parameter = p.getParameters()[0];
    parameter->setValueNotifyingHost(.25f); // physical gain .5
    juce::AudioBuffer<float> b(2, 17); b.clear(); b.setSample(0, 0, 1); b.setSample(1, 0, -.5f);
    juce::MidiBuffer midi; p.processBlock(b, midi);
    if (std::abs(b.getSample(0,0)-.5f)>1e-6 || std::abs(b.getSample(1,0)+.25f)>1e-6) return 3;
    auto* meters = dynamic_cast<tsunami::MeterSource*>(static_cast<juce::AudioProcessor*>(&p));
    if (!meters) { std::cerr << "adapter exposes no read-only output meter source\n"; return 13; }
    const auto peaks = meters->takeOutputPeaks(); // L, R, M=(L+R)/2, S=(L-R)/2 after DSP
    if (std::abs(peaks[0]-.5f)>1e-6 || std::abs(peaks[1]-.25f)>1e-6
        || std::abs(peaks[2]-.125f)>1e-6 || std::abs(peaks[3]-.375f)>1e-6) { std::cerr << "wrong output peaks\n"; return 14; }
    if (meters->takeOutputPeaks()!=std::array<float,4>{}) { std::cerr << "peaks not consumed\n"; return 15; }
    juce::MemoryBlock state; p.getStateInformation(state);
    parameter->setValueNotifyingHost(.75f);
    struct Listener : juce::AudioProcessorListener {
        bool rescanned = false;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override { rescanned |= details.programChanged; }
    } listener;
    p.addListener(&listener);
    p.setStateInformation(state.getData(), int(state.getSize()));
    p.removeListener(&listener);
    if (!listener.rescanned) { std::cerr << "state restore did not notify host parameter rescan\n"; return 8; }
    if (std::abs(parameter->getValue()-.25f)>1e-6) return 4;
    tsunami::Adapter<tsunami::PluginSpec> fresh;
    fresh.setStateInformation(state.getData(), int(state.getSize())); fresh.prepareToPlay(44100, 257);
    if (std::abs(fresh.getParameters()[0]->getValue()-.25f)>1e-6) return 5;
    if (fresh.isBusesLayoutSupported({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::mono()}})) return 6;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    if (!editor || editor->getWidth()<400 || editor->getHeight()<100) return 7;
    tsunami::Adapter<LocalSpec> local;
    std::unique_ptr<juce::AudioProcessorEditor> localEditor(local.createEditor());
    if (LocalSpec::receivedProcessor != &local || !LocalSpec::receivedState || !localEditor
        || localEditor->getName() != "local editor") {
        std::cerr << "plugin-local factory did not receive actual processor/APVTS\n"; return 9;
    }
    auto* localParameter = LocalSpec::receivedState->getParameter("gain");
    if (localParameter != local.getParameters()[0]) return 10;
    localParameter->setValueNotifyingHost(.25f);
    local.prepareToPlay(48000, 64);
    b.clear(); b.setSample(0, 0, 1); local.processBlock(b, midi);
    if (std::abs(b.getSample(0,0)-.5f)>1e-6) return 11;
    juce::MemoryBlock localState; local.getStateInformation(localState);
    localParameter->setValueNotifyingHost(.75f);
    local.setStateInformation(localState.getData(), int(localState.getSize()));
    if (std::abs(localParameter->getValue()-.25f)>1e-6) return 12;
    // Non-finite / out-of-range state values: non-finite -> default, finite -> clamped (host and DSP agree).
    for (auto [text, expected] : {std::pair{"nan", .5f}, {"inf", .5f}, {"-inf", .5f}, {"5", 1.f}, {"-3", 0.f}}) {
        tsunami::Adapter<tsunami::PluginSpec> q; q.prepareToPlay(48000, 64);
        q.getParameters()[0]->setValueNotifyingHost(.25f);
        auto bad = stateWithGain(q, text);
        q.setStateInformation(bad.getData(), int(bad.getSize()));
        const float v = q.getParameters()[0]->getValue();
        if (!std::isfinite(v) || std::abs(v-expected)>1e-6) { std::cerr << "state value " << text << " not sanitized: " << v << "\n"; return 16; }
        juce::MemoryBlock round; q.getStateInformation(round);
        if (round.toString().contains("nan") || round.toString().contains("inf")) { std::cerr << "non-finite survived state round trip\n"; return 17; }
        b.clear(); b.setSample(0, 0, 1); q.processBlock(b, midi);
        if (std::abs(b.getSample(0,0)-2.f*expected)>1e-6) { std::cerr << "DSP disagrees with host value for " << text << "\n"; return 18; }
    }
    // Optional core meter hook (compile-time detected).
    if (dynamic_cast<tsunami::CoreMeterSource*>(static_cast<juce::AudioProcessor*>(&p))) { std::cerr << "meterless core must not expose core meters\n"; return 19; }
    tsunami::Adapter<MeteredSpec> metered; metered.prepareToPlay(48000, 64);
    auto* coreMeters = dynamic_cast<tsunami::CoreMeterSource*>(static_cast<juce::AudioProcessor*>(&metered));
    if (!coreMeters || coreMeters->coreMeterCount()!=2) { std::cerr << "metered core not exposed\n"; return 20; }
    b.clear(); b.setSample(0, 0, .5f); metered.processBlock(b, midi); metered.processBlock(b, midi);
    if (coreMeters->coreMeter(0)!=.5f || coreMeters->coreMeter(1)!=-2.f || coreMeters->coreMeter(2)!=0.f) { std::cerr << "wrong core meters\n"; return 21; }
    // Opt-in mono (1-in/1-out); default stays stereo-only (checked above, code 6).
    tsunami::Adapter<MonoSpec> mono;
    if (!mono.isBusesLayoutSupported({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::mono()}})
        || !mono.isBusesLayoutSupported({{juce::AudioChannelSet::stereo()}, {juce::AudioChannelSet::stereo()}})
        || mono.isBusesLayoutSupported({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::stereo()}})) { std::cerr << "mono layout\n"; return 22; }
    if (!mono.setBusesLayout({{juce::AudioChannelSet::mono()}, {juce::AudioChannelSet::mono()}})) return 23;
    mono.prepareToPlay(48000, 64); mono.getParameters()[0]->setValueNotifyingHost(.25f);
    juce::AudioBuffer<float> m(1, 16); m.clear(); m.setSample(0, 0, 1); mono.processBlock(m, midi);
    if (std::abs(m.getSample(0,0)-.5f)>1e-6) { std::cerr << "mono processing\n"; return 24; }
    const auto monoPeaks = mono.takeOutputPeaks();
    if (std::abs(monoPeaks[0]-.5f)>1e-6 || std::abs(monoPeaks[1]-.5f)>1e-6 || monoPeaks[3]!=0.f) { std::cerr << "mono peaks\n"; return 25; }
    // Skewed taper and labelled choices reach the host parameter.
    tsunami::Adapter<TaperSpec> taper;
    auto* frequency = static_cast<juce::RangedAudioParameter*>(taper.getParameters()[0]);
    if (std::abs(frequency->convertTo0to1(std::sqrt(20.f*20000.f))-.5f)>2e-3 || std::abs(frequency->convertFrom0to1(1.f)-20000.f)>1e-2) {
        std::cerr << "log taper midpoint " << frequency->convertTo0to1(std::sqrt(20.f*20000.f)) << "\n"; return 26; }
    tsunami::Adapter<ChoiceSpec> choice;
    auto* mode = choice.getParameters()[0];
    if (mode->getCurrentValueAsText()!="HighPass" || static_cast<juce::RangedAudioParameter*>(mode)->getNormalisableRange().interval!=1.f || mode->getText(0.f, 32)!="BandPass"
        || std::abs(mode->getValueForText("Split")-1.f)>1e-6) { std::cerr << "choice labels: " << mode->getCurrentValueAsText() << "\n"; return 27; }
    std::cout << "adapter physical automation, in-place stereo, latency/tail, state, fresh instance, layout, editor, local factory/APVTS, output meters, state sanitizing, core meters, mono, taper/choice PASS\n";
    return 0;
#endif
}
