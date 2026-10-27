// SPDX-License-Identifier: GPL-3.0-or-later
// Independently drawn NS1 editor. Layout follows the original panel shown in the NS1
// user guide ch.3 figure: a recessed attenuation meter (orange bar, scales 0..-48 dB on
// both sides, value window below) on the left; one vertical fader 0..100 with scales on
// both sides and a value window below it on the right; product name at the bottom left.
// Everything is painted here; no vendor images, fonts or logo. The fader binds through an
// APVTS SliderAttachment (foundation EDITOR_FACTORY contract). Bypass has no panel control
// (host only), as in the original. The meter reads the plugin-local
// AttenuationMeterSource when the host wrapper provides it; otherwise it is drawn without
// data rather than faked.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "EditorModel.hpp"
#include <memory>

namespace tsunami::ns1 {
namespace colour {
inline const juce::Colour bodyTop{0xff5a5f66}, bodyBottom{0xff2e3238}, well{0xff24272b}, wellEdge{0xff6c7178},
    scale{0xffb8bcc2}, meter{0xffe8a020}, meterEmpty{0xff1a1a1a}, readoutBg{0xff0c0c0c}, readoutInk{0xfff0b040},
    slot{0xff111214}, capTop{0xff4a4d52}, capBottom{0xff1d1f22}, capLine{0xffe8a020}, title{0xffeef0f2};
}

// Fader cap and slot, drawn in code (ribbed dark cap with an orange index line).
class FaderLook final : public juce::LookAndFeel_V4 {
public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override {
        const float cx = float(x) + float(w) * .5f;
        g.setColour(colour::wellEdge); g.fillRoundedRectangle(cx - 7.f, float(y) - 4.f, 14.f, float(h) + 8.f, 3.f);
        g.setColour(colour::slot); g.fillRoundedRectangle(cx - 3.f, float(y), 6.f, float(h), 2.f);
        const juce::Rectangle<float> cap(cx - 30.f, pos - 24.f, 60.f, 48.f);
        g.setGradientFill(juce::ColourGradient(colour::capTop, cap.getX(), cap.getY(), colour::capBottom, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 4.f);
        g.setColour(juce::Colours::black.withAlpha(.6f));
        for (int k = 1; k < 8; ++k) g.drawHorizontalLine(int(cap.getY() + float(k) * 6.f), cap.getX() + 4.f, cap.getRight() - 4.f);
        g.setColour(colour::capLine); g.fillRect(cap.getX() + 2.f, pos - 1.f, cap.getWidth() - 4.f, 2.f);
    }
    int getSliderThumbRadius(juce::Slider&) override { return 24; } // Editor::thumbRadius
};

class AttenuationBar final : public juce::Component {
public:
    float db = 0; bool available = true;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(colour::wellEdge); g.drawRect(r, 1.f);
        g.setColour(colour::meterEmpty); g.fillRect(r.reduced(2.f));
        if (!available) return;
        // Bar spans from the -48 dB floor up to the current reading (as in the figure).
        auto bar = r.reduced(3.f);
        bar = bar.withTrimmedTop(bar.getHeight() * ui::attenuationFraction(db));
        g.setColour(colour::meter); g.fillRect(bar);
    }
};

class Readout final : public juce::Component {
public:
    juce::String text{"--"};
    void paint(juce::Graphics& g) override {
        g.setColour(colour::readoutBg); g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
        g.setColour(colour::readoutInk); g.setFont(juce::FontOptions(12.f));
        g.drawText(text, getLocalBounds(), juce::Justification::centred);
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    Editor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
        : AudioProcessorEditor(p), state(s), meterSource(dynamic_cast<AttenuationMeterSource*>(&p)) {
        fader.setLookAndFeel(&look);
        fader.setSliderStyle(juce::Slider::LinearVertical); fader.setComponentID("ns1");
        fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, "ns1", fader);
        fader.textFromValueFunction = [](double v) { return juce::String(ui::valueText(0, float(v))); };
        fader.onValueChange = [this] { faderReadout.setText(fader.getTextFromValue(fader.getValue()), juce::dontSendNotification); };
        // Value window below the fader: shows DisplayFloat1 text; typing a value sets the fader.
        faderReadout.setComponentID("ns1_readout"); faderReadout.setEditable(false, true, false);
        faderReadout.setJustificationType(juce::Justification::centred);
        faderReadout.setColour(juce::Label::backgroundColourId, colour::readoutBg);
        faderReadout.setColour(juce::Label::textColourId, colour::readoutInk);
        faderReadout.onTextChange = [this] { fader.setValue(faderReadout.getText().getDoubleValue(), juce::sendNotificationSync); fader.onValueChange(); } /* re-sync text when the value is unchanged or clamped */;
        meter.setComponentID("attenuation_meter"); meterReadout.setComponentID("attenuation_readout");
        meter.available = meterSource != nullptr;
        for (juce::Component* c : std::initializer_list<juce::Component*>{&meter, &meterReadout, &fader, &faderReadout}) addAndMakeVisible(c);
        fader.onValueChange();
        setSize(300, 430);
        startTimerHz(30);
    }
    ~Editor() override { stopTimer(); fader.setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override {
        g.setGradientFill(juce::ColourGradient(colour::bodyTop, 0.f, 0.f, colour::bodyBottom, 0.f, float(getHeight()), false));
        g.fillAll();
        // Recessed meter panel (left), as in the figure.
        g.setColour(colour::well); g.fillRoundedRectangle(meterPanel.toFloat(), 8.f);
        g.setColour(colour::wellEdge); g.drawRoundedRectangle(meterPanel.toFloat(), 8.f, 1.5f);
        g.setColour(colour::scale); g.setFont(juce::FontOptions(10.f));
        for (int db = 0; db <= 48; db += 6) {
            const juce::String t = db ? "-" + juce::String(db) : "0";
            sideTicks(g, meter.getBounds(), ui::attenuationFraction(float(-db)), t, 8);
        }
        for (int v = 0; v <= 100; v += 10) sideTicks(g, faderTrack(), 1.f - ui::faderFraction(float(v)), juce::String(v), 14);
        g.setColour(colour::title); g.setFont(juce::FontOptions(30.f, juce::Font::bold));
        g.drawText("NS1", 16, getHeight() - 74, 120, 34, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(12.f));
        g.drawText("Noise Suppressor", 16, getHeight() - 40, 140, 16, juce::Justification::centredLeft);
    }
    void resized() override {
        meterPanel = {-8, 70, 128, 250};
        meter.setBounds(46, 86, 24, 200); meterReadout.setBounds(34, 294, 48, 18);
        fader.setBounds(170, 40, 80, 290);
        faderReadout.setBounds(182, getHeight() - 52, 56, 20);
    }
    const ui::MeterModel& meterModel() const noexcept { return meters; }
    void pollMetersForTest() { timerCallback(); }

private:
    juce::AudioProcessorValueTreeState& state;
    AttenuationMeterSource* meterSource;
    ui::MeterModel meters;
    FaderLook look;
    juce::Slider fader;
    juce::Label faderReadout;
    AttenuationBar meter;
    Readout meterReadout;
    juce::Rectangle<int> meterPanel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttachment;

    // Travel of the fader thumb centre (JUCE LinearVertical insets by the thumb radius).
    static constexpr int thumbRadius = 24;
    juce::Rectangle<int> faderTrack() const { return fader.getBounds().reduced(0, thumbRadius); }
    static void sideTicks(juce::Graphics& g, juce::Rectangle<int> bar, float f, const juce::String& text, int gap) {
        const float y = float(bar.getY()) + f * float(bar.getHeight());
        const float l = float(bar.getX() - gap), r = float(bar.getRight() + gap);
        g.drawLine(l - 10.f, y, l, y, 1.f); g.drawLine(r, y, r + 10.f, y, 1.f);
        g.drawText(text, int(l) - 40, int(y) - 6, 28, 12, juce::Justification::centredRight);
        g.drawText(text, int(r) + 12, int(y) - 6, 28, 12, juce::Justification::centredLeft);
    }
    void timerCallback() override {
        if (meterSource) meters.poll(*meterSource);
        meter.db = meters.attenuationDb; meter.repaint();
        meterReadout.text = meterSource ? juce::String(ui::meterText(meters.attenuationDb)) : juce::String("--");
        meterReadout.repaint();
    }
};
} // namespace tsunami::ns1

namespace tsunami {
inline juce::AudioProcessorEditor* createPluginEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s) {
    return new ns1::Editor(p, s);
}
}
