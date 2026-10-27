// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <iostream>
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
    std::cout << "adapter physical automation, in-place stereo, latency/tail, state, fresh instance, layout, editor, local factory/APVTS PASS\n";
    return 0;
#endif
}
