// SPDX-License-Identifier: GPL-3.0-or-later
// Independently drawn RVox editor. Geometry follows the original 17.1 GUI layout
// (371x357 view; Gate/Compression/Gain fader rects; Energy, Total Compression and
// Output meter rects) and the user guide's interactions: option-click reset,
// double-click value entry, box-select linking of faders, click-to-reset peaks.
// No vendor images are used; everything is painted here. Output meters read the
// adapter's lock-free MeterSource. Energy and Total Compression read the core's
// existing detector envelope and smoothed gate*compressor gain (Processor::meter)
// through the shared CoreMeterSource bridge; the scale is project-defined, not
// proven original meter ballistics.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Adapter.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace tsunami::rvox {
namespace colour {
inline const juce::Colour body{0xffe9e6de}, bodyShade{0xffd6d2c8}, ink{0xff4a4a48}, value{0xff262626},
    accent{0xffee8a1c}, track{0xff23466b}, display{0xff16324f}, displayEdge{0xff0c1b2b},
    clipBand{0xff7d2027}, brand{0xffa3262f};
}

inline juce::String formatDb(double db, double floor) {
    return db <= floor + 0.05 ? juce::String("-inf") : juce::String(db, 1);
}

class FaderLook final : public juce::LookAndFeel_V4 {
public:
    int getSliderThumbRadius(juce::Slider& s) override { return s.getComponentID() == "compression" ? 22 : 12; }
    void drawLinearSlider(juce::Graphics& g, int x, int, int width, int, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider& s) override {
        if (s.getComponentID() == "compression") {
            // Threshold line across the display with the value pill riding on it.
            const auto pill = juce::Rectangle<float>(50.f, 21.f).withCentre({x + width * .5f, pos});
            g.setColour(colour::accent);
            g.fillRect(juce::Rectangle<float>(float(x) + 6.f, pos - 1.f, float(width) - 12.f, 2.f));
            g.setColour(colour::display.darker(.3f));
            g.fillRoundedRectangle(pill, 10.f);
            g.setColour(colour::accent);
            g.drawRoundedRectangle(pill.reduced(1.f), 9.f, 2.f);
            return; // the value text is the comp_value box riding on the pill
        }
        const auto ring = juce::Rectangle<float>(18.f, 11.f).withCentre({x + width * .5f, pos});
        g.setColour(juce::Colours::white.withAlpha(.35f));
        g.fillRoundedRectangle(ring, 4.f);
        g.setColour(colour::accent);
        g.drawRoundedRectangle(ring, 4.f, 2.f);
        g.drawHorizontalLine(int(pos), ring.getX() + 3.f, ring.getRight() - 3.f);
    }
};

// Vertical bar meter; level is 0..1 of the drawn dB span, negative means no data.
class Meter final : public juce::Component {
public:
    explicit Meter(bool fromTop = false) : topDown(fromTop) { setInterceptsMouseClicks(false, false); }
    void setLevel(float fraction) { if (fraction != level) { level = fraction; repaint(); } }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(colour::track);
        g.fillRect(r);
        if (level <= 0.f) return;
        const float h = r.getHeight() * juce::jmin(level, 1.f);
        g.setColour(colour::accent);
        g.fillRect(topDown ? r.removeFromTop(h) : r.removeFromBottom(h));
    }
private:
    bool topDown;
    float level = -1.f;
};

class AttnDisplay final : public juce::Component {
public:
    AttnDisplay() { setInterceptsMouseClicks(false, false); }
    // Fraction of the 40 dB scale, drawn down from the top; negative means no data.
    void setReduction(float fraction) { if (fraction != reduction) { reduction = fraction; repaint(); } }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(colour::displayEdge);
        g.fillRoundedRectangle(r, 8.f);
        auto inner = r.reduced(4.f);
        g.setColour(colour::display);
        g.fillRoundedRectangle(inner, 6.f);
        g.setColour(colour::clipBand);
        g.fillRoundedRectangle(inner.removeFromTop(16.f), 5.f);
        g.setColour(juce::Colours::white.withAlpha(.12f));
        for (int k = 1; k <= 6; ++k) {
            const float y = r.getY() + 4.f + 16.f + (r.getHeight() - 24.f) * float(k) / 6.5f;
            g.drawHorizontalLine(int(y), r.getX() + 6.f, r.getRight() - 6.f);
        }
        if (reduction > 0.f) {
            auto bar = juce::Rectangle<float>(r.getCentreX() - 7.f, r.getY(), 14.f, r.getHeight() * juce::jmin(reduction, 1.f));
            g.setColour(colour::accent.withAlpha(.85f));
            g.fillRect(bar.withTrimmedTop(4.f));
        }
    }
private:
    float reduction = -1.f;
};

class PeakReadout final : public juce::Label {
public:
    std::function<void()> onReset;
    void mouseDown(const juce::MouseEvent&) override { if (onReset) onReset(); }
};

// Link only changes made synchronously by user input, never APVTS callbacks.
class UserFader final : public juce::Slider {
public:
    UserFader() : juce::Slider(juce::Slider::LinearVertical, juce::Slider::NoTextBox) {}
    bool userChange = false;
    void mouseDown(const juce::MouseEvent& e) override {
        const juce::ScopedValueSetter<bool> origin(userChange, true); juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const juce::ScopedValueSetter<bool> origin(userChange, true); juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        const juce::ScopedValueSetter<bool> origin(userChange, true); juce::Slider::mouseUp(e);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const juce::ScopedValueSetter<bool> origin(userChange, true); juce::Slider::mouseDoubleClick(e);
    }
    bool keyPressed(const juce::KeyPress& e) override {
        const juce::ScopedValueSetter<bool> origin(userChange, true); return juce::Slider::keyPressed(e);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const juce::ScopedValueSetter<bool> origin(userChange, true); juce::Slider::mouseWheelMove(e, w);
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    Editor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
        : AudioProcessorEditor(p), state(s), meters(dynamic_cast<MeterSource*>(&p)), coreMeters(dynamic_cast<CoreMeterSource*>(&p)) {
        setOpaque(true);
        struct Spec { Fader* f; const char* id; juce::Rectangle<int> bounds; juce::Rectangle<int> readout; };
        const Spec specs[]{{&gate, "gate", {41, 105, 33, 195}, {22, 80, 70, 18}},
                           {&comp, "compression", {135, 99, 102, 207}, {161, 0, 50, 21}},
                           {&gain, "gain", {297, 105, 33, 195}, {279, 80, 70, 18}}};
        addAndMakeVisible(energy); energy.setComponentID("energy_meter"); energy.setBounds(50, 121, 15, 156);
        addAndMakeVisible(attn); attn.setComponentID("attn_display"); attn.setBounds(145, 121, 82, 155);
        for (size_t k = 0; k < 2; ++k) {
            addAndMakeVisible(out[k]);
            out[k].setComponentID(k ? "out_meter_r" : "out_meter_l");
            out[k].setBounds(k ? 315 : 307, 122, 5, 155);
        }
        for (const auto& spec : specs) setUpFader(*spec.f, spec.id, spec.bounds, spec.readout);
        comp.value.setComponentID("comp_value"); // value box follows the knob, clicks pass to the fader
        comp.value.setInterceptsMouseClicks(false, false);
        comp.value.setColour(juce::Label::textColourId, juce::Colours::white);
        comp.value.setFont(juce::FontOptions(13.f));
        placeCompValue();
        attnPeak.setText(juce::String::fromUTF8("\xe2\x80\x93"), juce::dontSendNotification);
        energyValue.setText(juce::String::fromUTF8("\xe2\x80\x93"), juce::dontSendNotification);
        for (auto* l : {static_cast<juce::Label*>(&attnPeak), &energyValue}) { styleReadout(*l); addAndMakeVisible(*l); }
        attnPeak.setComponentID("attn_peak");
        attnPeak.setBounds(151, 304, 70, 18); // peak total gain reduction, click resets
        attnPeak.onReset = [this] { reductionHold = 0.f; if (coreMeters) attnPeak.setText("0.0", juce::dontSendNotification); };
        energyValue.setComponentID("energy_value");
        energyValue.setBounds(22, 322, 70, 18);
        for (size_t k = 0; k < 2; ++k) {
            auto& r = peak[k];
            styleReadout(r);
            r.setComponentID(k ? "out_peak_r" : "out_peak_l");
            r.setText("-inf", juce::dontSendNotification);
            r.setBounds(k ? 316 : 270, 322, 46, 18);
            r.onReset = [this] { resetPeaks(); };
            addAndMakeVisible(r);
        }
        setSize(371, 357);
        startTimerHz(30);
    }
    ~Editor() override {
        stopTimer();
        endFollowerGestures();
        // A live drag ends during slider destruction; don't call back into faders
        // already destroyed. ~Fader still closes the leader's own gesture.
        for (auto* f : {&gate, &comp, &gain}) f->slider.onDragStart = f->slider.onDragEnd = f->slider.onValueChange = nullptr;
        for (auto* f : {&gate, &comp, &gain}) f->slider.setLookAndFeel(nullptr);
    }

    bool isLinked(const juce::String& id) const {
        for (auto* f : linked) if (f->slider.getComponentID() == id) return true;
        return false;
    }
    float outputLevel(int channel) const { return level[size_t(channel)]; }
    float energyLevel() const { return energyFraction; }
    float reductionDb() const { return reduction; }
    // Pulls and resets the adapter's output peaks; public so tests can drive it.
    void refreshMeters() {
        refreshCoreMeters();
        if (!meters) return;
        const auto taken = meters->takeOutputPeaks();
        for (size_t k = 0; k < 2; ++k) {
            level[k] = juce::jmax(taken[k], level[k] * 0.8f);
            hold[k] = juce::jmax(hold[k], taken[k]);
            const float db = juce::Decibels::gainToDecibels(level[k], -60.f);
            out[k].setLevel(level[k] > 0.f ? (db + 60.f) / 60.f : 0.f);
            peak[k].setText(formatDb(juce::Decibels::gainToDecibels(hold[k], -200.f), -200.0), juce::dontSendNotification);
        }
    }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(colour::body, 0.f, 0.f, colour::bodyShade, 0.f, r.getBottom(), false));
        g.fillRect(r);
        g.setColour(colour::brand);
        g.setFont(juce::FontOptions(26.f, juce::Font::italic));
        g.drawText("R", juce::Rectangle<int>(150, 14, 26, 32), juce::Justification::centredRight);
        g.setFont(juce::FontOptions(21.f));
        g.drawText("VOX", juce::Rectangle<int>(178, 18, 60, 28), juce::Justification::centredLeft);
        // Raised frame around the Total Compression display.
        const auto frame = juce::Rectangle<float>(126.f, 74.f, 120.f, 258.f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white, 0.f, frame.getY(), colour::bodyShade.darker(.1f), 0.f, frame.getBottom(), false));
        g.fillRoundedRectangle(frame, 24.f);
        g.setColour(colour::ink.withAlpha(.25f));
        g.drawRoundedRectangle(frame, 24.f, 1.f);
        g.setColour(colour::ink);
        g.setFont(juce::FontOptions(13.f));
        g.drawText("Gate", juce::Rectangle<int>(22, 66, 70, 16), juce::Justification::centred);
        g.drawText("Gain", juce::Rectangle<int>(279, 66, 70, 16), juce::Justification::centred);
        g.drawText("Comp", juce::Rectangle<int>(151, 98, 70, 16), juce::Justification::centred);
        g.drawText("Energy", juce::Rectangle<int>(22, 308, 70, 16), juce::Justification::centred);
        g.drawText("Output", juce::Rectangle<int>(279, 308, 70, 16), juce::Justification::centred);
        g.setFont(juce::FontOptions(10.f));
        for (int db = 12; db <= 72; db += 12) { // input energy scale, dB below full scale
            const int y = 121 + int(156.f * float(db) / 80.f);
            g.drawText(juce::String(db), juce::Rectangle<int>(18, y - 6, 22, 12), juce::Justification::centredRight);
        }
        for (int db = 6; db <= 36; db += 6) { // gain reduction scale beside the display
            const int y = 121 + int(155.f * float(db) / 40.f);
            g.drawText(juce::String(db), juce::Rectangle<int>(128, y - 6, 16, 12), juce::Justification::centredRight);
        }
        if (!box.isEmpty()) {
            g.setColour(colour::accent.withAlpha(.15f));
            g.fillRect(box);
            g.setColour(colour::accent);
            g.drawRect(box, 1);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override { boxStart = e.getMouseDownPosition().toInt(); box = {}; }
    void mouseDrag(const juce::MouseEvent& e) override {
        box = juce::Rectangle<int>(boxStart, e.getPosition());
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        linked.clear();
        if (e.mouseWasDraggedSinceMouseDown()) {
            const auto selection = juce::Rectangle<int>(boxStart, e.getPosition());
            for (auto* f : {&gate, &comp, &gain})
                if (selection.intersects(f->slider.getBounds())) linked.push_back(f);
            if (linked.size() < 2) linked.clear();
        }
        box = {};
        repaint();
    }

private:
    struct Fader {
        UserFader slider;
        juce::Label value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        double last = 0.0;
        juce::RangedAudioParameter* parameter = nullptr;
        bool dragOpen = false;
        bool followOpen = false; // host gesture the editor opened for a linked follower write
        ~Fader() {
            // SliderAttachment is destroyed before slider; JUCE removes its listener
            // without ending a live gesture. Close it while APVTS still exists.
            if (std::exchange(dragOpen, false) && parameter != nullptr)
                parameter->endChangeGesture();
        }
    };

    static void styleReadout(juce::Label& l) {
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::FontOptions(15.f));
        l.setColour(juce::Label::textColourId, colour::value);
    }
    void setUpFader(Fader& f, const char* id, juce::Rectangle<int> bounds, juce::Rectangle<int> readout) {
        f.slider.setComponentID(id);
        f.slider.setLookAndFeel(&look);
        f.slider.setBounds(bounds);
        f.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, f.slider);
        auto* param = state.getParameter(id);
        f.parameter = param;
        f.slider.onDragStart = [&f] { f.dragOpen = true; };
        f.slider.onDragEnd = [this, &f] { f.dragOpen = false; endFollowerGestures(); };
        const double minimum = f.slider.getMinimum();
        f.slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
        f.value.setComponentID(juce::String(id) + "_value");
        styleReadout(f.value);
        f.value.setEditable(false, true, false);
        f.value.setBounds(readout);
        f.last = f.slider.getValue();
        f.value.setText(formatDb(f.last, juce::String(id) == "gate" ? minimum : -1e9), juce::dontSendNotification);
        f.value.onTextChange = [&f, minimum] {
            const juce::ScopedValueSetter<bool> origin(f.slider.userChange, true);
            const auto text = f.value.getText();
            f.slider.setValue(text.containsIgnoreCase("inf") ? minimum : text.getDoubleValue(), juce::sendNotificationSync);
        };
        f.slider.onValueChange = [this, &f, minimum, id = juce::String(id)] {
            const double now = f.slider.getValue(), delta = now - f.last;
            f.last = now;
            f.value.setText(formatDb(now, id == "gate" ? minimum : -1e9), juce::dontSendNotification);
            if (&f == &comp) placeCompValue();
            if (propagating || !f.slider.userChange || delta == 0.0 || !isLinked(id)) return;
            propagating = true;
            // The follower's attachment writes without a gesture of its own; hold one
            // open for the leader's drag (closed on its drag end), or bracket a single
            // write when the leader changed outside a drag (value entry).
            for (auto* other : linked) {
                if (other == &f) continue;
                if (!other->dragOpen && !std::exchange(other->followOpen, true)) other->parameter->beginChangeGesture();
                other->slider.setValue(other->slider.getValue() + delta, juce::sendNotificationSync);
                if (!f.dragOpen) endFollowerGestures();
            }
            propagating = false;
        };
        addAndMakeVisible(f.slider);
        addAndMakeVisible(f.value);
    }
    void endFollowerGestures() {
        for (auto* f : {&gate, &comp, &gain})
            if (std::exchange(f->followOpen, false)) f->parameter->endChangeGesture();
    }
    void placeCompValue() {
        const int y = int(std::lround(comp.slider.getPositionOfValue(comp.slider.getValue())));
        comp.value.setBounds(juce::Rectangle<int>(50, 21).withCentre(comp.slider.getPosition() + juce::Point<int>(51, y)));
    }
    void resetPeaks() {
        hold = {};
        for (auto& r : peak) r.setText("-inf", juce::dontSendNotification);
    }
    void timerCallback() override { refreshMeters(); }
    // Latest post-block core state: index 0 detector envelope, 1 total gain (linear).
    // The bridge publishes 0/0 before the first block; that pair is shown as no data.
    void refreshCoreMeters() {
        if (!coreMeters) return;
        if (coreMeters->coreMeterCount() < 2) return;
        const float envelope = coreMeters->coreMeter(0), total = coreMeters->coreMeter(1);
        if (!(std::isfinite(envelope) && std::isfinite(total)) || (envelope <= 0.f && total <= 0.f)) return;
        const float db = juce::Decibels::gainToDecibels(envelope, -80.f);
        energyFraction = envelope > 0.f ? (db + 80.f) / 80.f : 0.f;
        energy.setLevel(energyFraction);
        energyValue.setText(formatDb(db, -80.0), juce::dontSendNotification);
        reduction = juce::jlimit(0.f, 200.f, -juce::Decibels::gainToDecibels(juce::jmin(total, 1.f), -200.f));
        attn.setReduction(reduction / 40.f);
        reductionHold = juce::jmax(reductionHold, reduction);
        attnPeak.setText(juce::String(reductionHold, 1), juce::dontSendNotification);
    }

    juce::AudioProcessorValueTreeState& state;
    MeterSource* meters;
    CoreMeterSource* coreMeters;
    FaderLook look;
    Fader gate, comp, gain;
    Meter energy, out[2];
    AttnDisplay attn;
    PeakReadout attnPeak;
    juce::Label energyValue;
    float energyFraction = -1.f, reduction = 0.f, reductionHold = 0.f;
    std::array<PeakReadout, 2> peak;
    std::array<float, 2> level{}, hold{};
    std::vector<Fader*> linked;
    juce::Point<int> boxStart;
    juce::Rectangle<int> box;
    bool propagating = false;
};
} // namespace tsunami::rvox

namespace tsunami {
inline juce::AudioProcessorEditor* createPluginEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s) {
    return new rvox::Editor(p, s);
}
} // namespace tsunami
