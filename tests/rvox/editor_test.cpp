// SPDX-License-Identifier: GPL-3.0-or-later
// Native JUCE test of the actual RVox editor: geometry from the original 17.1
// GUI layout (371x357 view, fader rects), APVTS-bound faders, value entry,
// option-click reset, Comp+Gain box linking (user guide p5) and process-driven
// output meters with click-to-reset peak readouts (user guide p6).
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(cond, code, what) do { if (!(cond)) { std::cerr << "FAIL " << what << "\n"; return code; } } while (0)

template<class T> static T* child(juce::Component& root, const char* id) {
    return dynamic_cast<T*>(root.findChildWithID(id));
}
static juce::MouseEvent event(juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged,
                              juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 0.f, 0.f, 0.f, 0.f, 0.f,
                            &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

static juce::Rectangle<int> rect(juce::Component& root, const char* id) {
    auto* c = root.findChildWithID(id);
    return c ? c->getBounds() : juce::Rectangle<int>();
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    tsunami::Adapter<tsunami::PluginSpec> p;
    p.prepareToPlay(48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor, 1, "editor created");
    auto* rv = dynamic_cast<tsunami::rvox::Editor*>(editor.get());
    CHECK(rv, 2, "actual RVox editor (not fixture fallback)");
    CHECK(editor->getWidth() == 371 && editor->getHeight() == 357, 3, "original 371x357 view rect");
    CHECK(rect(*editor, "gate") == juce::Rectangle<int>(41, 105, 33, 195), 4, "gate fader rect");
    CHECK(rect(*editor, "compression") == juce::Rectangle<int>(135, 99, 102, 207), 5, "comp fader rect");
    CHECK(rect(*editor, "gain") == juce::Rectangle<int>(297, 105, 33, 195), 6, "gain fader rect");
    CHECK(rect(*editor, "energy_meter") == juce::Rectangle<int>(50, 121, 15, 156), 7, "energy meter rect");
    CHECK(rect(*editor, "attn_display") == juce::Rectangle<int>(145, 121, 82, 155), 8, "total compression rect");
    CHECK(rect(*editor, "out_meter_l") == juce::Rectangle<int>(307, 122, 5, 155), 9, "out L rect");
    CHECK(rect(*editor, "out_meter_r") == juce::Rectangle<int>(315, 122, 5, 155), 10, "out R rect");

    auto& params = p.getParameters();
    auto ranged = [&](int i) { return static_cast<juce::RangedAudioParameter*>(params[i]); };
    auto physical = [&](int i) { return ranged(i)->convertFrom0to1(params[i]->getValue()); };
    auto setPhysical = [&](int i, float v) { ranged(i)->setValueNotifyingHost(ranged(i)->convertTo0to1(v)); };
    auto* comp = child<juce::Slider>(*editor, "compression");
    auto* gate = child<juce::Slider>(*editor, "gate");
    auto* gain = child<juce::Slider>(*editor, "gain");
    CHECK(comp && gate && gain, 11, "three faders");
    for (auto* s : {comp, gate, gain})
        CHECK(s->getSliderStyle() == juce::Slider::LinearVertical, 12, "vertical faders");

    // Fader -> parameter and host automation -> fader / readout.
    comp->setValue(-11.3, juce::sendNotificationSync);
    CHECK(std::abs(physical(0) + 11.3f) < 1e-3f, 13, "comp fader drives parameter");
    setPhysical(1, -19.6f);
    CHECK(std::abs(gate->getValue() + 19.6) < 1e-3, 14, "gate automation reaches fader");
    CHECK(child<juce::Label>(*editor, "gate_value")->getText() == "-19.6", 15, "gate readout one decimal");
    CHECK(child<juce::Label>(*editor, "comp_value")->getText() == "-11.3", 16, "comp readout follows");
    CHECK(child<juce::Label>(*editor, "gate_value")->getText() != "" && gate->getMinimum() == -80.0, 17, "gate range");
    setPhysical(1, -80.f);
    CHECK(child<juce::Label>(*editor, "gate_value")->getText() == "-inf", 18, "gate minimum shows original -Infinity/off");

    // Value box entry (double-click edit) sets the parameter.
    child<juce::Label>(*editor, "gain_value")->setText("-3.8", juce::sendNotificationSync);
    CHECK(std::abs(physical(2) + 3.8f) < 1e-3f, 19, "gain value entry");
    // Option-click resets to default.
    CHECK(comp->isDoubleClickReturnEnabled() && comp->getDoubleClickReturnValue() == 0.0, 20, "comp option-reset to 0");
    CHECK(gate->isDoubleClickReturnEnabled() && gate->getDoubleClickReturnValue() == -80.0, 21, "gate option-reset to off");

    // Box around Compression and Gain links them (user guide p5).
    CHECK(!rv->isLinked("compression") && !rv->isLinked("gain"), 22, "unlinked initially");
    editor->mouseDown(event(*editor, {120.f, 92.f}, {120.f, 92.f}, false));
    editor->mouseDrag(event(*editor, {340.f, 312.f}, {120.f, 92.f}, true));
    editor->mouseUp(event(*editor, {340.f, 312.f}, {120.f, 92.f}, true));
    CHECK(rv->isLinked("compression") && rv->isLinked("gain") && !rv->isLinked("gate"), 23, "box links comp+gain only");
    comp->setValue(-15.3, juce::sendNotificationSync);
    CHECK(std::abs(physical(2) - (-3.8f - 4.f)) < 1e-3f, 24, "linked gain follows comp offset");
    editor->mouseDown(event(*editor, {100.f, 340.f}, {100.f, 340.f}, false));
    editor->mouseUp(event(*editor, {100.f, 340.f}, {100.f, 340.f}, false));
    CHECK(!rv->isLinked("compression"), 25, "background click unlinks");
    comp->setValue(-12.0, juce::sendNotificationSync);
    CHECK(std::abs(physical(2) + 7.8f) < 1e-3f, 26, "unlinked gain unchanged");

    // Output meters are driven by actual processed audio; peak readouts reset on click.
    CHECK(child<juce::Label>(*editor, "out_peak_l")->getText() == "-inf", 27, "peak empty before audio");
    juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer midi;
    for (int block = 0; block < 40; ++block) {
        for (int n = 0; n < 256; ++n) {
            const float t = float(block * 256 + n) / 48000.f;
            b.setSample(0, n, .8f * std::sin(2.f * 3.14159265f * 440.f * t));
            b.setSample(1, n, .2f * std::sin(2.f * 3.14159265f * 440.f * t));
        }
        p.processBlock(b, midi);
    }
    rv->refreshMeters();
    const auto l = child<juce::Label>(*editor, "out_peak_l")->getText(), r = child<juce::Label>(*editor, "out_peak_r")->getText();
    CHECK(l != "-inf" && r != "-inf" && l.getFloatValue() <= 0.f && l.getFloatValue() > r.getFloatValue(), 28, "output peaks from processed audio");
    CHECK(rv->outputLevel(0) > 0.f && rv->outputLevel(0) <= 1.f, 29, "output meter level");
    auto* peak = editor->findChildWithID("out_peak_l");
    peak->mouseDown(event(*peak, {2.f, 2.f}, {2.f, 2.f}, false));
    CHECK(child<juce::Label>(*editor, "out_peak_l")->getText() == "-inf", 30, "click resets peak hold");

    // Paint the real editor (no fixture) to an image without errors.
    juce::Image image(juce::Image::ARGB, 371, 357, true);
    juce::Graphics g(image);
    editor->paintEntireComponent(g, true);
    CHECK(image.getPixelAt(185, 20).getAlpha() == 255, 31, "opaque painted body");
    if (const char* path = std::getenv("RVOX_EDITOR_PNG")) { // optional inspection artifact
        juce::FileOutputStream stream{juce::File(path)};
        stream.setPosition(0); stream.truncate();
        CHECK(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream), 32, "write editor PNG");
    }
    editor.reset();
    std::cout << "PASS rvox editor\n";
    return 0;
}
