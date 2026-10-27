// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameter.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <type_traits>
#include <utility>

namespace tsunami {
// Optional read-only output meter bridge for plugin editors (dynamic_cast from AudioProcessor).
// Peaks are collected on the audio thread after DSP with lock-free atomics; the
// consumer takes and resets them. Order: L, R, M=(L+R)/2, S=(L-R)/2 (linear).
static_assert(std::atomic<float>::is_always_lock_free, "meter bridge requires lock-free atomic<float>");
// The CAS max loop is lock-free, not wait-free: retries are unbounded under contention.
struct MeterSource {
    virtual ~MeterSource() = default;
    virtual std::array<float, 4> takeOutputPeaks() noexcept = 0;
};
namespace detail {
template<class Spec, class = void> struct HasEditorFactory : std::false_type {};
template<class Spec> struct HasEditorFactory<Spec, std::void_t<decltype(Spec::createEditor(
    std::declval<juce::AudioProcessor&>(), std::declval<juce::AudioProcessorValueTreeState&>()))>> : std::true_type {};
}
// Physical-value, block-boundary automation. No claim of sample-offset automation.
// All core calls are serialized on the host's audio/lifecycle thread.
template<class Spec> class Adapter final : public juce::AudioProcessor, public MeterSource {
    static constexpr size_t count = std::size(Spec::parameters);
    typename Spec::Core core;
    juce::AudioProcessorValueTreeState state;
    std::array<std::atomic<float>*, count> values{};
    unsigned maxBlock = 0;
    std::array<std::atomic<float>, 4> peaks{};
    void publishPeaks(const float* l, const float* r, int frames) noexcept {
        std::array<float, 4> block{};
        for (int n=0;n<frames;++n) {
            const float values[]{std::abs(l[n]), std::abs(r[n]), std::abs(.5f*(l[n]+r[n])), std::abs(.5f*(l[n]-r[n]))};
            for (size_t k=0;k<4;++k) block[k]=juce::jmax(block[k], values[k]);
        }
        for (size_t k=0;k<4;++k) {
            float old=peaks[k].load(std::memory_order_relaxed);
            while (block[k]>old && !peaks[k].compare_exchange_weak(old, block[k], std::memory_order_relaxed)) {}
        }
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        for (const auto& p : Spec::parameters)
            result.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(p.id, 1), p.name,
                juce::NormalisableRange<float>(p.minimum, p.maximum, p.step), p.initial,
                juce::AudioParameterFloatAttributes().withLabel(p.unit)));
        return result;
    }
    void apply() noexcept {
        for (size_t i=0;i<count;++i) core.setParameter(unsigned(i), values[i]->load(std::memory_order_relaxed));
    }
    class Editor final : public juce::AudioProcessorEditor {
        juce::Label title;
        juce::OwnedArray<juce::Label> labels;
        juce::OwnedArray<juce::Slider> sliders;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attachments;
    public:
        explicit Editor(Adapter& p) : AudioProcessorEditor(p) {
            title.setText(Spec::name, juce::dontSendNotification);
            title.setFont(juce::Font(23.f, juce::Font::bold));
            title.setColour(juce::Label::textColourId, juce::Colour(0xff4ce0c3));
            addAndMakeVisible(title);
            for (const auto& metadata : Spec::parameters) {
                auto* label = labels.add(new juce::Label());
                label->setText(metadata.name, juce::dontSendNotification); addAndMakeVisible(label);
                auto* slider = sliders.add(new juce::Slider());
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 88, 24);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff4ce0c3));
                addAndMakeVisible(slider);
                attachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(p.state, metadata.id, *slider));
            }
            setSize(600, 90+int(count)*44);
        }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff17232d)); }
        void resized() override {
            title.setBounds(22, 12, getWidth()-44, 48);
            for (int i=0;i<sliders.size();++i) {
                labels[i]->setBounds(22, 76+i*44, 140, 30);
                sliders[i]->setBounds(168, 76+i*44, getWidth()-190, 30);
            }
        }
    };
public:
    Adapter() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
                state(*this, nullptr, "TsunamiState", layout()) {
        for (size_t i=0;i<count;++i) values[i]=state.getRawParameterValue(Spec::parameters[i].id);
    }
    const juce::String getName() const override { return Spec::name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return core.tailSeconds(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate, int block) override {
        maxBlock=unsigned(juce::jmax(1, block)); apply(); core.prepare(rate, 2, maxBlock);
        setLatencySamples(int(core.latencySamples()));
    }
    void releaseResources() override { core.reset(); }
    void reset() override { apply(); core.reset(); }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals guard;
        if (b.getNumChannels()!=2 || unsigned(b.getNumSamples())>maxBlock || maxBlock==0) { b.clear(); return; }
        apply();
        const float* inputs[]{b.getReadPointer(0), b.getReadPointer(1)};
        float* outputs[]{b.getWritePointer(0), b.getWritePointer(1)};
        core.process(inputs, outputs, unsigned(b.getNumSamples()));
        publishPeaks(outputs[0], outputs[1], b.getNumSamples());
    }
    std::array<float, 4> takeOutputPeaks() noexcept override {
        std::array<float, 4> result{};
        for (size_t k=0;k<4;++k) result[k]=peaks[k].exchange(0.f, std::memory_order_relaxed);
        return result;
    }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override {
        if constexpr (detail::HasEditorFactory<Spec>::value)
            return Spec::createEditor(*this, state);
        else
            return new Editor(*this); // Infrastructure fixture fallback, not a product UI.
    }
    void getStateInformation(juce::MemoryBlock& data) override {
        auto copy=state.copyState(); copy.setProperty("plugin", Spec::id, nullptr); copy.setProperty("schema", 1, nullptr);
        if (auto xml=copy.createXml()) copyXmlToBinary(*xml, data);
    }
    void setStateInformation(const void* data, int bytes) override {
        if (auto xml=getXmlFromBinary(data, bytes)) {
            auto restored=juce::ValueTree::fromXml(*xml);
            if (restored.hasType("TsunamiState") && restored["plugin"].toString()==Spec::id && int(restored["schema"])==1) {
                state.replaceState(restored);
                // Preset/state loads change values; CLAP must request a VALUES rescan.
                updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
            }
        }
    }
};
}
