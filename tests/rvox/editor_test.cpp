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
#include <vector>

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
    auto approx = [](float a, float b) { return std::abs(a - b) < 1e-3f; };
    {
        const float c = physical(0), g = physical(2);
        CHECK(comp->keyPressed(juce::KeyPress(juce::KeyPress::downKey)), 24, "comp key handled");
        CHECK(physical(0) != c && approx(physical(2) - g, physical(0) - c), 24, "linked gain follows comp offset");
    }
    setPhysical(0, -15.3f); setPhysical(2, -7.8f);
    // Host automation of one linked parameter must not write the other (the
    // attachment updates the slider synchronously here on the message thread).
    setPhysical(0, -18.f);
    CHECK(approx(physical(0), -18.f) && approx(physical(2), -7.8f) && approx(physical(1), -80.f), 40, "comp automation isolated while linked");
    setPhysical(2, -5.f);
    CHECK(approx(physical(0), -18.f) && approx(physical(2), -5.f), 41, "gain automation isolated while linked");
    {
        juce::MemoryBlock saved; p.getStateInformation(saved);
        setPhysical(0, -30.f); setPhysical(2, -1.f);
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        CHECK(approx(physical(0), -18.f) && approx(physical(2), -5.f) && approx(physical(1), -80.f), 42, "state recall exact while linked");
    }
    // Every user input path still moves the linked pair by the same offset.
    auto linkedDelta = [&](auto&& action, int code, const char* what) {
        const float c = physical(0), g = physical(2);
        action();
        const float dc = physical(0) - c, dg = physical(2) - g;
        if (dg == 0.f || !approx(dc, dg) || !approx(physical(1), -80.f)) {
            std::cerr << "FAIL " << what << " comp_delta=" << dc << " gain_delta=" << dg << "\n"; return code;
        }
        return 0;
    };
    if (int r = linkedDelta([&] { child<juce::Label>(*editor, "gain_value")->setText("-6.5", juce::sendNotificationSync); }, 43, "linked value entry")) return r;
    bool keyHandled = false;
    if (int r = linkedDelta([&] { keyHandled = gain->keyPressed(juce::KeyPress(juce::KeyPress::upKey)); }, 44, "linked key")) return r;
    CHECK(keyHandled, 44, "gain key handled");
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
    CHECK(approx(physical(0), -10.f) && approx(physical(2), 0.f), 47, "linked double-click reset keeps offset");
    setPhysical(0, -20.f); setPhysical(2, -10.f);
    const juce::ModifierKeys alt(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    gain->mouseDown(event(*gain, {16.f, 90.f}, {16.f, 90.f}, false, alt));
    gain->mouseUp(event(*gain, {16.f, 90.f}, {16.f, 90.f}, false, alt));
    CHECK(approx(physical(0), -10.f) && approx(physical(2), 0.f), 49, "linked alt-click reset keeps offset");
    // Host automation right after user input must not inherit user origin.
    setPhysical(2, -4.f);
    CHECK(approx(physical(0), -10.f), 48, "automation after user input isolated");
    setPhysical(0, -15.3f); setPhysical(2, -7.8f);
    editor->mouseDown(event(*editor, {100.f, 340.f}, {100.f, 340.f}, false));
    editor->mouseUp(event(*editor, {100.f, 340.f}, {100.f, 340.f}, false));
    CHECK(!rv->isLinked("compression"), 25, "background click unlinks");
    {
        const float c = physical(0);
        CHECK(comp->keyPressed(juce::KeyPress(juce::KeyPress::upKey)) && physical(0) != c, 26, "unlinked comp key moves comp");
        CHECK(std::abs(physical(2) + 7.8f) < 1e-3f, 26, "unlinked gain unchanged");
    }

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

    // Energy and Total Compression show the core's existing detector envelope and
    // smoothed gate*compressor gain through the shared CoreMeterSource bridge,
    // polled by the editor's own timer (no direct refresh call).
    {
        auto* bridge = dynamic_cast<tsunami::CoreMeterSource*>(&p);
        CHECK(bridge && bridge->coreMeterCount() == 2, 69, "product adapter exposes the RVox core meter bridge");
        // Let the editor's 30 Hz timer expire, then deliver it on this message thread. JUCE's
        // timer thread only re-counts after a 300 ms wait for its undispatched message.
        auto pump = [] { for (int k = 0; k < 4; ++k) { juce::Thread::sleep(350); juce::Timer::callPendingTimersSynchronously(); } };
        ranged(0)->setValueNotifyingHost(ranged(0)->convertTo0to1(-24.f)); // compression threshold
        juce::AudioBuffer<float> v(2, 256);
        for (int block = 0; block < 60; ++block) {
            for (int n = 0; n < 256; ++n) {
                const float s = .5f * std::sin(2.f * 3.14159265f * 440.f * float(block * 256 + n) / 48000.f);
                v.setSample(0, n, s); v.setSample(1, n, s);
            }
            p.processBlock(v, midi);
        }
        pump();
        const float energyDb = child<juce::Label>(*editor, "energy_value")->getText().getFloatValue();
        CHECK(energyDb > -7.f && energyDb < -2.f, 70, "energy readout follows -6dBFS input detector");
        CHECK(rv->energyLevel() > .9f && rv->energyLevel() < 1.f, 71, "energy bar drawn from live envelope");
        CHECK(rv->reductionDb() > 14.f && rv->reductionDb() < 30.f, 72, "total compression from live core gain");
        const float held = child<juce::Label>(*editor, "attn_peak")->getText().getFloatValue();
        CHECK(held >= rv->reductionDb() - .051f, 73, "peak total compression held");
        float outPeak = 0.f;
        for (int n = 0; n < 256; ++n) outPeak = juce::jmax(outPeak, std::abs(v.getSample(0, n)));
        CHECK(std::abs(juce::Decibels::gainToDecibels(outPeak) - energyDb) > 1.f, 74, "input energy is not output level");
        if (const char* path = std::getenv("RVOX_EDITOR_LIVE_PNG")) { // optional live-meter inspection artifact
            juce::Image live(juce::Image::ARGB, 371, 357, true);
            { juce::Graphics g(live); editor->paintEntireComponent(g, true); }
            juce::FileOutputStream stream{juce::File(path)};
            stream.setPosition(0); stream.truncate();
            CHECK(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(live, stream), 79, "write live PNG");
        }
        for (int block = 0; block < 400; ++block) { v.clear(); p.processBlock(v, midi); } // >2s silence; in-place output cleared
        pump();
        CHECK(rv->energyLevel() <= 0.f && child<juce::Label>(*editor, "energy_value")->getText() == "-inf", 75, "silence empties energy");
        CHECK(rv->reductionDb() < .1f, 76, "silence releases total compression");
        auto* attnPeak = editor->findChildWithID("attn_peak");
        attnPeak->mouseDown(event(*attnPeak, {2.f, 2.f}, {2.f, 2.f}, false));
        CHECK(child<juce::Label>(*editor, "attn_peak")->getText().getFloatValue() < .1f, 77, "click resets compression hold");
    }

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

    // A linked follower is written by the editor, not by its own slider gesture;
    // the host must still see every follower change inside a balanced gesture.
    // Gain (index 2) leads, Compression (index 0) follows; Gate (1) stays put.
    struct Bracketed final : juce::AudioProcessorListener {
        struct Counts { std::array<int, 3> open{}, begins{}, ends{}, changes{}, loose{}; } c;
        std::array<int, 3>& open = c.open; std::array<int, 3>& begins = c.begins; std::array<int, 3>& ends = c.ends;
        std::array<int, 3>& changes = c.changes; std::array<int, 3>& loose = c.loose;
        bool invalid = false;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int i, float) override {
            if (i < 0 || i >= 3) { invalid = true; return; }
            ++changes[size_t(i)]; if (open[size_t(i)] == 0) ++loose[size_t(i)];
        }
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int i) override {
            if (i < 0 || i >= 3) { invalid = true; return; }
            ++begins[size_t(i)]; if (++open[size_t(i)] != 1) invalid = true;
        }
        void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int i) override {
            if (i < 0 || i >= 3) { invalid = true; return; }
            ++ends[size_t(i)]; if (--open[size_t(i)] != 0) invalid = true;
        }
    } host;
    auto linkCompGain = [&](juce::AudioProcessorEditor& ed) {
        ed.mouseDown(event(ed, {120.f, 92.f}, {120.f, 92.f}, false));
        ed.mouseDrag(event(ed, {340.f, 312.f}, {120.f, 92.f}, true));
        ed.mouseUp(event(ed, {340.f, 312.f}, {120.f, 92.f}, true));
        return dynamic_cast<tsunami::rvox::Editor&>(ed).isLinked("compression");
    };
    // The leader's own write must also sit in exactly one host gesture; failures are
    // collected so every leader path is reported before the check.
    std::vector<std::string> leaderFailures;
    auto leaderCheck = [&](const std::string& what, size_t li, const Bracketed::Counts& before) {
        const int changes = host.changes[li] - before.changes[li], loose = host.loose[li] - before.loose[li],
                  begins = host.begins[li] - before.begins[li];
        std::cout << "LEADER " << what << " changes=" << changes << " unbracketed=" << loose << " begins=" << begins
                  << " ends=" << host.ends[li] - before.ends[li] << "\n";
        if (changes == 0 || loose != 0 || begins != 1) leaderFailures.push_back(what);
    };
    p.addListener(&host);
    const char* paths[] = {"drag", "close-mid-drag", "key", "wheel", "double-click reset", "alt-click reset", "value entry",
                           "comp-led close-mid-drag"};
    for (int k = 0; k < 8; ++k) {
        const size_t fi = k == 7 ? 2 : 0; // follower parameter index
        p.removeListener(&host); setPhysical(0, -20.f); setPhysical(1, -80.f); setPhysical(2, -10.f); p.addListener(&host);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        CHECK(linkCompGain(*ed), 80, "follower test links comp+gain");
        auto* lead = child<juce::Slider>(*ed, k == 7 ? "compression" : "gain");
        const auto before = host.c;
        if (k == 0 || k == 1 || k == 7) {
            const float x = k == 7 ? 51.f : 16.f;
            lead->mouseDown(event(*lead, {x, 90.f}, {x, 90.f}, false));
            lead->mouseDrag(event(*lead, {x, 110.f}, {x, 90.f}, true));
            lead->mouseDrag(event(*lead, {x, 130.f}, {x, 90.f}, true));
            if (k == 0) lead->mouseUp(event(*lead, {x, 130.f}, {x, 90.f}, true));
        } else if (k == 2) {
            CHECK(lead->keyPressed(juce::KeyPress(juce::KeyPress::upKey)), 81, "follower key handled");
        } else if (k == 3) {
            juce::MouseWheelDetails wheel{}; wheel.deltaY = 0.1f;
            lead->mouseWheelMove(event(*lead, {16.f, 90.f}, {16.f, 90.f}, false, juce::ModifierKeys()), wheel);
        } else if (k == 4) {
            lead->mouseDoubleClick(event(*lead, {16.f, 90.f}, {16.f, 90.f}, false));
        } else if (k == 5) {
            lead->mouseDown(event(*lead, {16.f, 90.f}, {16.f, 90.f}, false, alt));
            lead->mouseUp(event(*lead, {16.f, 90.f}, {16.f, 90.f}, false, alt));
        } else {
            child<juce::Label>(*ed, "gain_value")->setText("-6.5", juce::sendNotificationSync);
        }
        // The follower gesture closes with the user's action, not only on editor close.
        const bool live = k == 1 || k == 7;
        CHECK(host.open[fi] == (live ? 1 : 0) && (live || host.begins[fi] == host.ends[fi]), 92, "follower gesture ends with the user action");
        ed.reset(); // k == 1 and 7 close while the linked drag is live
        const int followerChanges = host.changes[fi] - before.changes[fi];
        std::cout << "FOLLOWER " << paths[k] << " changes=" << followerChanges
                  << " unbracketed=" << host.loose[fi] - before.loose[fi] << " begins=" << host.begins[fi] - before.begins[fi]
                  << " ends=" << host.ends[fi] - before.ends[fi] << " open=" << host.open[fi]
                  << " offset=" << physical(2) - physical(0) << "\n";
        CHECK(followerChanges > 0 && approx(physical(2) - physical(0), 10.f), 82, "linked follower moves by leader offset");
        CHECK(host.loose[fi] == before.loose[fi], 83, "follower changes bracketed by host gestures");
        CHECK(host.begins[fi] == before.begins[fi] + 1, 84, "one follower host gesture per user action");
        CHECK(!host.invalid && (host.open == std::array<int, 3>{}) && host.begins == host.ends, 85, "follower gestures balanced");
        CHECK(host.changes[1] == before.changes[1] && host.begins[1] == before.begins[1], 86, "unlinked gate untouched");
        leaderCheck(paths[k], k == 7 ? 0 : 2, before);
    }
    {
        // Unlinked leaders: own gesture only, no write or gesture on the other fader.
        p.removeListener(&host); setPhysical(0, -20.f); setPhysical(1, -80.f); setPhysical(2, -10.f); p.addListener(&host);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto* g = child<juce::Slider>(*ed, "gain");
        auto* c = child<juce::Slider>(*ed, "compression");
        auto unlinked = [&](const char* what, size_t li, auto&& action) {
            const auto before = host.c;
            action();
            leaderCheck(what, li, before);
            const size_t other = li == 2 ? 0 : 2;
            if (host.changes[other] != before.changes[other] || host.begins[other] != before.begins[other] ||
                !(host.open == std::array<int, 3>{}) || host.invalid)
                leaderFailures.push_back(std::string(what) + " isolation/balance");
        };
        unlinked("unlinked gain key", 2, [&] { g->keyPressed(juce::KeyPress(juce::KeyPress::downKey)); });
        unlinked("unlinked comp key", 0, [&] { c->keyPressed(juce::KeyPress(juce::KeyPress::upKey)); });
        unlinked("unlinked gain value entry", 2, [&] { child<juce::Label>(*ed, "gain_value")->setText("-4.5", juce::sendNotificationSync); });
        unlinked("unlinked comp value entry", 0, [&] { child<juce::Label>(*ed, "comp_value")->setText("-25", juce::sendNotificationSync); });
        // Inputs that change nothing open no gesture: unhandled key, modified arrow, same value.
        const auto quiet = host.c;
        CHECK(!g->keyPressed(juce::KeyPress('x')), 94, "unhandled key ignored");
        CHECK(!g->keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)), 94, "modified arrow ignored");
        child<juce::Label>(*ed, "gain_value")->setText("-4.5", juce::sendNotificationSync);
        CHECK(host.begins == quiet.begins && host.changes == quiet.changes, 95, "no gesture without a change");
        // A key during a live drag joins that drag's gesture rather than nesting one.
        const auto dragBefore = host.c;
        g->mouseDown(event(*g, {16.f, 90.f}, {16.f, 90.f}, false));
        g->mouseDrag(event(*g, {16.f, 110.f}, {16.f, 90.f}, true));
        CHECK(g->keyPressed(juce::KeyPress(juce::KeyPress::upKey)), 96, "key during drag handled");
        g->mouseUp(event(*g, {16.f, 110.f}, {16.f, 90.f}, true));
        CHECK(!host.invalid && host.begins[2] == dragBefore.begins[2] + 1 && host.loose[2] == dragBefore.loose[2] &&
              (host.open == std::array<int, 3>{}), 96, "key during drag does not nest a gesture");
    }
    for (const auto& what : leaderFailures) std::cerr << "LEADER UNBRACKETED " << what << "\n";
    CHECK(leaderFailures.empty(), 93, "leader key and value-entry writes bracketed by host gestures");
    {
        // Host automation and state recall reach linked faders without user origin:
        // no follower write and no editor-originated gesture.
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        CHECK(linkCompGain(*ed), 87, "isolation test links comp+gain");
        setPhysical(0, -12.f); setPhysical(2, -3.f);
        juce::MemoryBlock saved; p.getStateInformation(saved);
        const auto before = host.c;
        setPhysical(2, -9.f);
        CHECK(approx(float(child<juce::Slider>(*ed, "gain")->getValue()), -9.f), 88, "automation reaches the linked fader");
        CHECK(approx(physical(0), -12.f) && host.changes[0] == before.changes[0], 88, "automation writes no follower");
        setPhysical(0, -30.f);
        CHECK(approx(physical(2), -9.f), 89, "automation of comp writes no follower");
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        CHECK(approx(physical(0), -12.f) && approx(physical(2), -3.f), 90, "state recall exact while linked");
        CHECK(host.begins == before.begins && host.ends == before.ends && !host.invalid, 91, "no gestures from automation/recall");
    }
    p.removeListener(&host);
    std::cout << "PASS rvox editor\n";
    return 0;
}
