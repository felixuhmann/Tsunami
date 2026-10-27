// SPDX-License-Identifier: GPL-3.0-or-later
// Native JUCE test of the actual RVox editor: geometry from the original 17.1
// GUI layout (371x357 view, fader rects), APVTS-bound faders, value entry,
// option-click reset, Comp+Gain box linking (user guide p5) and process-driven
// output meters with click-to-reset peak readouts (user guide p6), host
// automation isolated from linking, and balanced host gestures on editor close.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"
#include <array>
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
    // Linking follows user input only; a programmatic/APVTS setValue is host state.
    child<juce::Label>(*editor, "comp_value")->setText("-15.3", juce::sendNotificationSync);
    CHECK(std::abs(physical(2) - (-3.8f - 4.f)) < 1e-3f, 24, "linked gain follows comp offset");
    auto near = [](float a, float b) { return std::abs(a - b) < 1e-3f; };
    // Host automation of one linked parameter must not write the other (the
    // attachment updates the slider synchronously here on the message thread).
    setPhysical(0, -18.f);
    CHECK(near(physical(0), -18.f) && near(physical(2), -7.8f) && near(physical(1), -80.f), 40, "comp automation isolated while linked");
    setPhysical(2, -5.f);
    CHECK(near(physical(0), -18.f) && near(physical(2), -5.f), 41, "gain automation isolated while linked");
    {
        juce::MemoryBlock saved; p.getStateInformation(saved);
        setPhysical(0, -30.f); setPhysical(2, -1.f);
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        CHECK(near(physical(0), -18.f) && near(physical(2), -5.f) && near(physical(1), -80.f), 42, "state recall exact while linked");
    }
    // Every user input path still moves the linked pair by the same offset.
    auto linkedDelta = [&](auto&& action, int code, const char* what) {
        const float c = physical(0), g = physical(2);
        action();
        const float dc = physical(0) - c, dg = physical(2) - g;
        if (dg == 0.f || !near(dc, dg) || !near(physical(1), -80.f)) {
            std::cerr << "FAIL " << what << " comp_delta=" << dc << " gain_delta=" << dg << "\n"; return code;
        }
        return 0;
    };
    if (int r = linkedDelta([&] { child<juce::Label>(*editor, "gain_value")->setText("-6.5", juce::sendNotificationSync); }, 43, "linked value entry")) return r;
    if (int r = linkedDelta([&] { CHECK(gain->keyPressed(juce::KeyPress(juce::KeyPress::upKey)), 44, "key handled"); return 0; }, 44, "linked key")) return r;
    setPhysical(0, -20.f); setPhysical(2, -15.f);
    if (int r = linkedDelta([&] {
            gain->mouseDown(event(*gain, {16.f, 90.f}, {16.f, 90.f}, false));
            gain->mouseDrag(event(*gain, {16.f, 120.f}, {16.f, 90.f}, true));
            gain->mouseUp(event(*gain, {16.f, 120.f}, {16.f, 90.f}, true));
        }, 45, "linked drag")) return r;
    if (int r = linkedDelta([&] {
            juce::MouseWheelDetails wheel{}; wheel.deltaY = 0.1f;
            gain->mouseWheelMove(event(*gain, {16.f, 90.f}, {16.f, 90.f}, false, juce::ModifierKeys()), wheel);
        }, 46, "linked wheel")) return r;
    setPhysical(0, -20.f); setPhysical(2, -10.f);
    gain->mouseDoubleClick(event(*gain, {16.f, 90.f}, {16.f, 90.f}, false));
    CHECK(near(physical(0), -10.f) && near(physical(2), 0.f), 47, "linked double-click reset keeps offset");
    // Host automation right after user input must not inherit user origin.
    setPhysical(2, -4.f);
    CHECK(near(physical(0), -10.f), 48, "automation after user input isolated");
    setPhysical(0, -15.3f); setPhysical(2, -7.8f);
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
    // Windows' default native image is Direct2D-backed and only flushed when the
    // Graphics context ends; scope it so pixel reads see the painted result.
    juce::Image image(juce::Image::ARGB, 371, 357, true);
    {
        juce::Graphics g(image);
        editor->paintEntireComponent(g, true);
    }
    CHECK(image.getPixelAt(185, 20).getAlpha() == 255, 31, "opaque painted body");
    if (const char* path = std::getenv("RVOX_EDITOR_PNG")) { // optional inspection artifact
        juce::FileOutputStream stream{juce::File(path)};
        stream.setPosition(0); stream.truncate();
        CHECK(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream), 32, "write editor PNG");
    }
    editor.reset();

    // Host gestures stay balanced: normal release, editor closed mid-drag,
    // repeated press and duplicate release, on every fader. APVTS outlives editors.
    struct Gestures final : juce::AudioProcessorListener {
        std::array<int, 3> open{}, begins{}, ends{};
        bool invalid = false;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int i) override {
            if (i < 0 || i >= 3) { invalid = true; return; }
            ++begins[size_t(i)]; if (++open[size_t(i)] != 1) invalid = true;
        }
        void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int i) override {
            if (i < 0 || i >= 3) { invalid = true; return; }
            ++ends[size_t(i)]; if (--open[size_t(i)] != 0) invalid = true;
        }
        bool balanced() const { return !invalid && open == std::array<int, 3>{} && begins == ends; }
    } gestures;
    p.addListener(&gestures);
    const char* ids[] = {"compression", "gate", "gain"}; // parameter index order
    for (int i = 0; i < 3; ++i) {
        for (int scenario = 0; scenario < 3; ++scenario) {
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            auto* s = child<juce::Slider>(*ed, ids[i]);
            CHECK(s, 60, "fader for gesture test");
            const int before = gestures.begins[size_t(i)];
            s->mouseDown(event(*s, {16.f, 90.f}, {16.f, 90.f}, false));
            s->mouseDrag(event(*s, {16.f, 120.f}, {16.f, 90.f}, true));
            if (scenario == 0) s->mouseUp(event(*s, {16.f, 120.f}, {16.f, 90.f}, true));
            if (scenario == 2) {
                s->mouseDown(event(*s, {16.f, 100.f}, {16.f, 100.f}, false));
                s->mouseUp(event(*s, {16.f, 100.f}, {16.f, 100.f}, true));
                s->mouseUp(event(*s, {16.f, 100.f}, {16.f, 100.f}, true));
            }
            ed.reset(); // scenario 1 closes the editor while the drag is live
            std::cout << "GESTURE " << ids[i] << " scenario=" << scenario << " begins=" << gestures.begins[size_t(i)]
                      << " ends=" << gestures.ends[size_t(i)] << " open=" << gestures.open[size_t(i)] << "\n";
            CHECK(gestures.begins[size_t(i)] > before, 61, "drag begins a host gesture");
            CHECK(gestures.balanced(), 62, "host gestures balanced after editor close");
        }
    }
    p.removeListener(&gestures);
    std::cout << "PASS rvox editor\n";
    return 0;
}
