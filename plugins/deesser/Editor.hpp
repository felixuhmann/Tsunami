// SPDX-License-Identifier: GPL-3.0-or-later
// Independently drawn DeEsser editor. Layout follows the original panel shown in the
// DeEsser manual ch.4 figure (left column Audio / Frequency+SideChain / Monitor; centre
// Threshold slider beside the blue Energy bar and red Atten bar with readouts; right
// stereo Output meters 0..-30 dBFS with readouts). Everything is painted here; no vendor
// images or fonts. Controls use APVTS attachments (foundation EDITOR_FACTORY contract).
// Output meters read the shared adapter's lock-free MeterSource. Energy/Atten read the
// shared adapter's CoreMeterSource (core meter(0) detector level, meter(1) gain reduction:
// latest post-block values) or a plugin-local DetectorMeterSource; without either they are
// drawn without data rather than faked.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Adapter.hpp"
#include "EditorModel.hpp"
#include <memory>

namespace tsunami::deesser {
namespace colour {
inline const juce::Colour body{0xffdedcd6}, panelLine{0xff9a9890}, ink{0xff3a3a38}, well{0xff262626},
    energy{0xff3a8fe0}, atten{0xffe0402a}, output{0xffe0902a}, lamp{0xffe8e060}, button{0xff5a5a58}, footer{0xff2e2e2e};
}

class BarMeter final : public juce::Component {
public:
    std::function<float(float)> fraction;   // value -> 0 (top) .. 1 (bottom)
    juce::Colour fill;
    float value = 0; bool available = true; bool fromTop = false; // Atten grows downward from 0
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(colour::well); g.fillRoundedRectangle(r, 2.f);
        if (!available) return;
        const float f = fraction(value);
        auto bar = r.reduced(2.f);
        if (fromTop) bar = bar.withHeight(bar.getHeight() * f);
        else bar = bar.withTrimmedTop(bar.getHeight() * f);
        g.setColour(fill); g.fillRect(bar);
    }
};

class Readout final : public juce::Component {
public:
    std::function<void()> onClick; juce::String text{"--"};
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colours::white); g.fillRect(getLocalBounds());
        g.setColour(colour::ink); g.drawRect(getLocalBounds());
        g.setFont(juce::FontOptions(12.f)); g.drawText(text, getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    Editor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
        : AudioProcessorEditor(p), state(s), outputMeters(dynamic_cast<MeterSource*>(&p)),
          detectorMeters(dynamic_cast<DetectorMeterSource*>(&p)), coreMeters(dynamic_cast<CoreMeterSource*>(&p)) {
        if (coreMeters != nullptr && coreMeters->coreMeterCount() < 2) coreMeters = nullptr;
        // Audio: single button showing the current mode (Split/Wideband).
        audio.setClickingTogglesState(true); audio.setComponentID("audio");
        audioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, "audio", audio);
        audio.onStateChange = [this] { audio.setButtonText(ui::valueText(3, audio.getToggleState() ? 1.f : 0.f)); };
        // Frequency: value window, drag to change; genuine 2000*8^n taper (slider-side range).
        freq.setSliderStyle(juce::Slider::LinearBarVertical); freq.setComponentID("freq");
        freq.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 60, 20);
        freqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "freq", freq);
        // Genuine 2000*8^n taper on the slider side: the attachment converts in physical Hz, so a
        // custom range set AFTER it gives the taper even when the host parameter range is linear.
        freq.setNormalisableRange(juce::NormalisableRange<double>(2000.0, 16000.0,
            [](double, double, double n) { return ui::proportionToFreq(n); },
            [](double, double, double hz) { return ui::freqToProportion(hz); },
            [](double, double, double hz) { return std::round(std::clamp(hz, 2000.0, 16000.0)); }));
        freq.textFromValueFunction = [](double v) { return juce::String(ui::valueText(0, float(v))); };
        // SideChain filter shape selector.
        sidechain.addItem("BandPass", 1); sidechain.addItem("HighPass", 2); sidechain.setComponentID("sidechain");
        sidechainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, "sidechain", sidechain);
        // Monitor: Audio / S Chain pair, one parameter; lamp lit in SideChain mode.
        monitorAudio.setComponentID("monitor_audio"); monitorSideChain.setComponentID("monitor_sidechain");
        if (auto* param = state.getParameter("monitor"))
            monitorAttachment = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { setMonitor(v >= .5f); });
        monitorAudio.onClick = [this] { if (monitorAttachment) monitorAttachment->setValueAsCompleteGesture(0.f); };
        monitorSideChain.onClick = [this] { if (monitorAttachment) monitorAttachment->setValueAsCompleteGesture(1.f); };
        // Threshold: vertical slider aligned with the Energy bar (0..-80 dB).
        threshold.setSliderStyle(juce::Slider::LinearVertical); threshold.setComponentID("threshold");
        threshold.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 18);
        thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "threshold", threshold);
        threshold.textFromValueFunction = [](double v) { return juce::String(ui::valueText(1, float(v))); };

        energy.fill = colour::energy; energy.fraction = [](float db) { return ui::thresholdFraction(db); }; energy.setComponentID("energy_meter");
        atten.fill = colour::atten; atten.fromTop = true; atten.fraction = [](float db) { return ui::attenFraction(db); }; atten.setComponentID("atten_meter");
        energy.available = atten.available = hasDetectorMeters();
        for (int k = 0; k < 2; ++k) {
            out[k].fill = colour::output; out[k].fraction = [](float db) { return ui::outputFraction(db); };
            out[k].setComponentID(k ? "output_right" : "output_left"); out[k].available = outputMeters != nullptr;
            outReadout[k].onClick = [this] { meters.resetPeaks(); refreshReadouts(); };
        }
        energyReadout.onClick = attenReadout.onClick = [this] { meters.resetPeaks(); refreshReadouts(); };
        energyReadout.setComponentID("energy_readout"); attenReadout.setComponentID("atten_readout");
        for (juce::Component* c : std::initializer_list<juce::Component*>{&audio, &freq, &sidechain, &monitorAudio, &monitorSideChain, &threshold,
                                     &energy, &atten, &out[0], &out[1], &energyReadout, &attenReadout, &outReadout[0], &outReadout[1]})
            addAndMakeVisible(c);
        if (monitorAttachment) monitorAttachment->sendInitialUpdate();
        audio.onStateChange();
        setSize(380, 380);
        startTimerHz(30);
    }
    ~Editor() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(colour::body);
        g.setColour(colour::panelLine);
        g.drawVerticalLine(90, 0.f, 330.f); g.drawVerticalLine(265, 0.f, 330.f);
        g.drawHorizontalLine(58, 0.f, 90.f); g.drawHorizontalLine(185, 0.f, 90.f);
        g.setColour(colour::ink); g.setFont(juce::FontOptions(12.f));
        auto label = [&](const char* t, int x, int y, int w) { g.drawText(t, x, y, w, 16, juce::Justification::centred); };
        label("Audio", 0, 4, 90); label("Frequency", 0, 70, 90); label("SideChain", 0, 122, 90); label("Monitor", 0, 200, 90);
        label("Threshold", 100, 4, 80); label("Atten", 185, 4, 70); label("Output", 270, 4, 110);
        // Scales drawn from the model so ticks and bars stay aligned.
        g.setFont(juce::FontOptions(10.f));
        for (int db : {0, 20, 40, 60, 80}) tick(g, energy.getBounds(), ui::thresholdFraction(float(-db)), juce::String(db));
        for (int db : {0, 3, 6, 12, 18}) tick(g, atten.getBounds(), ui::attenFraction(float(db)), db ? "-" + juce::String(db) : "0");
        tick(g, atten.getBounds(), 1.f, "Inf");
        for (int db : {0, 6, 12, 18, 24, 30}) tick(g, out[0].getBounds(), ui::outputFraction(float(-db)), db ? "-" + juce::String(db) : "0");
        if (monitorSideChain.getToggleState()) { g.setColour(colour::lamp); g.fillEllipse(40.f, 296.f, 10.f, 10.f); }
        g.setColour(colour::footer); g.fillRect(0, 330, getWidth(), 50);
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(18.f, juce::Font::bold));
        g.drawText("DeEsser", getLocalBounds().removeFromBottom(50).reduced(14, 0), juce::Justification::centredRight);
    }
    void resized() override {
        audio.setBounds(15, 24, 60, 24);
        freq.setBounds(15, 88, 60, 24);
        sidechain.setBounds(15, 140, 60, 24);
        monitorAudio.setBounds(15, 220, 60, 22); monitorSideChain.setBounds(15, 244, 60, 22);
        threshold.setBounds(96, 24, 74, 230);
        energy.setBounds(150, 30, 16, 218); atten.setBounds(200, 30, 16, 218);
        energyReadout.setBounds(136, 262, 44, 18); attenReadout.setBounds(186, 262, 44, 18);
        out[0].setBounds(292, 30, 16, 218); out[1].setBounds(336, 30, 16, 218);
        outReadout[0].setBounds(278, 262, 44, 18); outReadout[1].setBounds(322, 262, 44, 18);
    }
    const ui::MeterModel& meterModel() const noexcept { return meters; }
    void pollMetersForTest() { timerCallback(); }

private:
    juce::AudioProcessorValueTreeState& state;
    MeterSource* outputMeters;
    DetectorMeterSource* detectorMeters;
    CoreMeterSource* coreMeters;
    bool hasDetectorMeters() const noexcept { return detectorMeters != nullptr || coreMeters != nullptr; }
    ui::MeterModel meters;
    juce::TextButton audio, monitorAudio{"Audio"}, monitorSideChain{"S Chain"};
    juce::Slider freq, threshold;
    juce::ComboBox sidechain;
    BarMeter energy, atten, out[2];
    Readout energyReadout, attenReadout, outReadout[2];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> audioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment, thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sidechainAttachment;
    std::unique_ptr<juce::ParameterAttachment> monitorAttachment;

    static void tick(juce::Graphics& g, juce::Rectangle<int> bar, float f, const juce::String& text) {
        const int y = bar.getY() + juce::roundToInt(f * float(bar.getHeight()));
        g.drawHorizontalLine(y, float(bar.getRight() + 1), float(bar.getRight() + 5));
        g.drawText(text, bar.getRight() + 6, y - 6, 24, 12, juce::Justification::centredLeft);
    }
    void setMonitor(bool sideChain) {
        monitorAudio.setToggleState(!sideChain, juce::dontSendNotification);
        monitorSideChain.setToggleState(sideChain, juce::dontSendNotification);
        repaint();
    }
    void refreshReadouts() {
        auto db = [](float v) { return v <= -120.f ? juce::String("--") : juce::String(v, 1); };
        energyReadout.text = hasDetectorMeters() ? db(meters.energyPeakDb) : "--";
        attenReadout.text = hasDetectorMeters() ? juce::String(ui::attenReadout(meters.attenPeakDb)) : "--";
        for (int k = 0; k < 2; ++k) outReadout[k].text = outputMeters ? db(meters.outputPeakDb[size_t(k)]) : "--";
        for (juce::Component* c : std::initializer_list<juce::Component*>{&energyReadout, &attenReadout, &outReadout[0], &outReadout[1]}) c->repaint();
    }
    void timerCallback() override {
        if (detectorMeters) meters.poll(*detectorMeters);
        else if (coreMeters) meters.pollDetector(coreMeters->coreMeter(0), coreMeters->coreMeter(1));
        if (outputMeters) { const auto peaks = outputMeters->takeOutputPeaks(); meters.pollOutput(peaks[0], peaks[1]); }
        energy.value = meters.energyDb; atten.value = meters.attenDb;
        out[0].value = meters.outputDb[0]; out[1].value = meters.outputDb[1];
        for (juce::Component* c : std::initializer_list<juce::Component*>{&energy, &atten, &out[0], &out[1]}) c->repaint();
        refreshReadouts();
    }
};
} // namespace tsunami::deesser

namespace tsunami {
inline juce::AudioProcessorEditor* createPluginEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s) {
    return new deesser::Editor(p, s);
}
}
