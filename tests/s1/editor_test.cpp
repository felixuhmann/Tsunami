// SPDX-License-Identifier: GPL-3.0-or-later
// Native JUCE test of the actual S1 editor: real components, APVTS attachments,
// state sync, reset-image semantics and process-derived output meters.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"

#define CHECK(cond, code, what) do { if (!(cond)) { std::cerr << "FAIL " << what << "\n"; return code; } } while (0)

template<class T> static T* child(juce::Component& root, const char* id) {
    return dynamic_cast<T*>(root.findChildWithID(id));
}

static juce::MouseEvent mouseAt(juce::Component& c, juce::ModifierKeys mods, int clicks = 1) {
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, mods, 0.f, 0.f, 0.f, 0.f, 0.f,
                            &c, &c, juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), clicks, false);
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    tsunami::Adapter<tsunami::PluginSpec> p;
    p.prepareToPlay(48000, 64);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor, 1, "editor created");
    auto* s1 = dynamic_cast<tsunami::s1::Editor*>(editor.get());
    CHECK(s1, 2, "actual S1 editor (not fixture fallback)");
    // Official body ratio from the independent reference spec: 1048x665, display x244 y32 w560 h321.
    CHECK(editor->getWidth() == 1048 && editor->getHeight() == 665, 18, "1048x665 body");
    CHECK(editor->findChildWithID("vector_display")->getBounds() == juce::Rectangle<int>(244, 32, 560, 321), 19, "display geometry");
    for (const char* id : {"gain", "width", "rotation", "asymmetry"})
        CHECK(child<juce::Slider>(*editor, id), 3, std::string("slider ") + id);
    for (const char* id : {"swap", "invert_left", "invert_right", "input_mode", "bypass", "meter_mode"})
        CHECK(child<juce::Button>(*editor, id), 4, std::string("button ") + id);
    auto& params = p.getParameters();
    auto ranged = [&](int i) { return static_cast<juce::RangedAudioParameter*>(params[i]); };
    auto physical = [&](int i) { return ranged(i)->convertFrom0to1(params[i]->getValue()); };
    auto setPhysical = [&](int i, float v) { ranged(i)->setValueNotifyingHost(ranged(i)->convertTo0to1(v)); };

    // UI -> parameter through attachments.
    child<juce::Slider>(*editor, "width")->setValue(2.0, juce::sendNotificationSync);
    CHECK(std::abs(physical(1) - 2.f) < 1e-4, 5, "width slider drives parameter");
    { auto* bi = child<juce::Button>(*editor, "input_mode"); bi->setToggleState(!bi->getToggleState(), juce::sendNotificationSync); }
    CHECK(physical(7) > .5f, 6, "M/S input toggle drives parameter");
    // Host automation -> UI.
    setPhysical(2, 30.f);
    CHECK(std::abs(child<juce::Slider>(*editor, "rotation")->getValue() - 30.) < 1e-3, 7, "rotation automation reaches slider");

    // Vector display model agrees with the DSP matrix (left input maps to output (a, d)).
    params[7]->setValueNotifyingHost(0.f);
    auto vec = tsunami::s1::vectorModel({-6.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
    // Ordinary M/S polar coordinates (x=(R-L)/sqrt2, y=(R+L)/sqrt2): unity L/R at -45/+45 deg.
    CHECK(std::abs(vec.left.angleDegrees + 45.f) < 1e-3 && std::abs(vec.right.angleDegrees - 45.f) < 1e-3, 8, "default L/R at +-45 deg (M/S coordinates)");
    CHECK(std::abs(vec.center.angleDegrees) < 1e-3 && std::abs(vec.left.levelDb + 6.f) < 1e-3, 9, "centre upright, gain shown as level");
    auto rotated = tsunami::s1::vectorModel({0.f, 1.f, 20.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
    CHECK(std::abs(rotated.center.angleDegrees - 20.f) < 1e-3, 10, "rotation 20 turns centre ray by 20 deg");

    // Reset image: width/asymmetry/rotation to defaults; gain and input settings preserved.
    setPhysical(0, -6.f);
    setPhysical(3, 40.f);
    params[4]->setValueNotifyingHost(1.f);
    s1->resetImage();
    CHECK(std::abs(physical(1) - 1.f) < 1e-4 && std::abs(physical(2)) < 1e-4 && std::abs(physical(3)) < 1e-4, 11, "image reset");
    CHECK(std::abs(physical(0) + 6.f) < 1e-3 && physical(4) > .5f, 12, "gain/swap preserved by reset image");
    params[4]->setValueNotifyingHost(0.f); params[0]->setValueNotifyingHost(1.f);

    // Plain drag: X rotation / Y gain; Alt-drag: X asymmetry / Y width (bounded).
    auto plain = tsunami::s1::applyVectorDrag({0.f, 1.f, 0.f, 0.f}, 50.f, -1000.f, false);
    CHECK(plain[2] > 0.f && plain[2] <= 45.f && plain[0] == 0.f && plain[1] == 1.f, 13, "plain drag bounds");
    auto alt = tsunami::s1::applyVectorDrag({0.f, 1.f, 0.f, 0.f}, 1000.f, -100.f, true);
    CHECK(alt[3] == 90.f && alt[1] > 1.f && alt[2] == 0.f, 14, "alt drag asymmetry/width");

    // Process-derived meters; LR/MS switch changes display only, never audio.
    p.prepareToPlay(48000, 64);
    juce::AudioBuffer<float> b(2, 64); b.clear();
    for (int n = 0; n < 64; ++n) { b.setSample(0, n, .5f); b.setSample(1, n, .5f); }
    juce::MidiBuffer midi; p.processBlock(b, midi);
    const float before = b.getSample(0, 63);
    s1->pollMeters();
    auto lr = s1->meterReadings();
    CHECK(lr[0] > -9.f && lr[0] < 0.f && std::abs(lr[0] - lr[1]) < .05f, 15, "LR meters from processed output");
    { auto* bm = child<juce::Button>(*editor, "meter_mode"); bm->setToggleState(!bm->getToggleState(), juce::sendNotificationSync); }
    for (int n = 0; n < 64; ++n) { b.setSample(0, n, .5f); b.setSample(1, n, .5f); }
    p.processBlock(b, midi);
    CHECK(std::abs(b.getSample(0, 63) - before) < 1e-6, 16, "meter mode does not alter audio");
    s1->pollMeters();
    auto ms = s1->meterReadings();
    CHECK(ms[1] < -60.f && std::abs(ms[0] - lr[0]) < .05f, 17, "MS meters show mid, silent side");
    // Whole-buffer bit identity across a meter-mode switch (display-only).
    {
        auto render = [&] { juce::AudioBuffer<float> x(2, 64);
            for (int n = 0; n < 64; ++n) { x.setSample(0, n, std::sin(.1f * n)); x.setSample(1, n, .3f * std::cos(.07f * n)); }
            p.processBlock(x, midi); return x; };
        p.reset(); auto a = render();
        { auto* bm = child<juce::Button>(*editor, "meter_mode"); bm->setToggleState(!bm->getToggleState(), juce::sendNotificationSync); }
        p.reset(); auto c = render();
        for (int ch = 0; ch < 2; ++ch)
            CHECK(std::memcmp(a.getReadPointer(ch), c.getReadPointer(ch), sizeof(float) * 64) == 0, 20, "meter mode switch is bit-identical over whole buffer");
    }
    // Live state restore while the editor stays open: controls follow restored values.
    {
        const float target[]{-9.5f, 2.25f, -12.5f, 33.f, 1.f, 1.f, 0.f, 1.f, 1.f};
        for (int i = 0; i < 9; ++i) setPhysical(i, target[i]);
        juce::MemoryBlock saved; p.getStateInformation(saved);
        const float other[]{-1.f, .5f, 10.f, -20.f, 0.f, 0.f, 1.f, 0.f, 0.f};
        for (int i = 0; i < 9; ++i) setPhysical(i, other[i]);
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        const char* sl[]{"gain", "width", "rotation", "asymmetry"};
        for (int i = 0; i < 4; ++i)
            CHECK(std::abs(child<juce::Slider>(*editor, sl[i])->getValue() - target[i]) < 1e-3, 21, std::string("restored slider ") + sl[i]);
        const char* bt[]{"swap", "invert_left", "invert_right", "input_mode", "bypass"};
        for (int i = 0; i < 5; ++i)
            CHECK(child<juce::Button>(*editor, bt[i])->getToggleState() == (target[4 + i] > .5f), 22, std::string("restored toggle ") + bt[i]);
        for (int i = 0; i < 9; ++i) setPhysical(i, ranged(i)->convertFrom0to1(ranged(i)->getDefaultValue()));
    }
    // Peak hold / clip lamps driven by processed audio; click lamp or readout resets that channel.
    {
        { auto* bm = child<juce::Button>(*editor, "meter_mode"); if (bm->getToggleState()) bm->setToggleState(false, juce::sendNotificationSync); }
        juce::AudioBuffer<float> x(2, 64); x.clear();
        for (int n = 0; n < 64; ++n) { x.setSample(0, n, n == 10 ? 1.2f : .1f); x.setSample(1, n, .25f); }
        s1->pollMeters(); s1->resetHold(0); s1->resetHold(1); // drain peaks from earlier unpolled renders
        p.reset(); p.processBlock(x, midi); s1->pollMeters();
        CHECK(s1->clipped(0) && !s1->clipped(1), 23, "clip lamp latches only on |x|>=1 channel");
        CHECK(std::abs(s1->heldPeakDb(0) - 20.f * std::log10(1.2f)) < .05f && std::abs(s1->heldPeakDb(1) + 12.04f) < .05f, 24, "held peaks");
        for (int i = 0; i < 5; ++i) { x.clear(); p.processBlock(x, midi); s1->pollMeters(); }
        CHECK(s1->clipped(0) && std::abs(s1->heldPeakDb(0) - 20.f * std::log10(1.2f)) < .05f, 25, "hold survives silence");
        CHECK(s1->meterReadings()[0] < s1->heldPeakDb(0) - 5.f, 26, "bar falls in silence while hold remains");
        auto* lamp = editor->findChildWithID("peak_lamp_0"); CHECK(lamp, 27, "lamp component");
        lamp->mouseDown(mouseAt(*lamp, {}));
        CHECK(!s1->clipped(0) && s1->heldPeakDb(0) <= -99.f && std::abs(s1->heldPeakDb(1) + 12.04f) < .05f, 28, "lamp click resets that channel only");
        auto* readout = editor->findChildWithID("peak_readout_1"); CHECK(readout, 29, "readout component");
        readout->mouseDown(mouseAt(*readout, {}));
        CHECK(s1->heldPeakDb(1) <= -99.f, 30, "readout click resets hold");
    }
    // Alt-click on an individual fader resets it to default (manual p8); plain click does not.
    {
        auto* w = child<juce::Slider>(*editor, "width");
        setPhysical(1, 2.5f); setPhysical(0, -10.f);
        w->mouseDown(mouseAt(*w, juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier)));
        w->mouseUp(mouseAt(*w, juce::ModifierKeys(juce::ModifierKeys::altModifier)));
        CHECK(std::abs(physical(1) - 1.f) < 1e-4, 31, "alt-click resets width to default");
        CHECK(std::abs(physical(0) + 10.f) < 1e-3, 32, "alt-click on width leaves gain");
        setPhysical(0, 0.f);
    }
    // Runtime-drawn fader caps: broad dark grooved rectangle with pale index at the
    // actual value position (gain 0 -> y82 travel top; width 1 -> one third up from y370).
    {
        auto* g = child<juce::Slider>(*editor, "gain");
        CHECK(g->getBounds().getX() <= 41 && g->getBounds().getRight() >= 103, 33, "gain fader column");
        auto shot = editor->createComponentSnapshot(editor->getLocalBounds());
        auto capStats = [&](int cx, int cy, int hw, int hh) {
            int dark = 0, pale = 0, total = 0;
            for (int y = cy - hh; y <= cy + hh; ++y) for (int x = cx - hw; x <= cx + hw; ++x) {
                const auto c = shot.getPixelAt(x, y); const float b = c.getBrightness(); ++total;
                dark += b < .35f; pale += (b > .8f && x >= cx - hw + 3 && x <= cx + hw - 3 && std::abs(y - cy) <= 1);
            }
            return std::pair<float,int>(float(dark) / float(total), pale);
        };
        auto gainCap = capStats(72, 82, 18, 11);
        CHECK(gainCap.first > .55f && gainCap.second >= 20, 34, "gain cap broad dark with pale index line");
        auto widthCap = capStats(160, 274, 18, 11);
        CHECK(widthCap.first > .55f && widthCap.second >= 20, 35, "width cap at value 1");
        auto rotationCap = capStats(523, 512, 11, 18);
        CHECK(rotationCap.first > .5f, 36, "rotation cap centred");
    }
    // Channel labels must clear the meter tick columns (hosted Linux 9862ef0 showed R over the right ticks).
    {
        const juce::Rectangle<int> ticks[]{{903, 74, 14, 326}, {957, 74, 14, 326}, {872, 74, 28, 326}, {974, 74, 28, 326}};
        for (int k = 0; k < 2; ++k) {
            const auto label = s1->channelLabelBounds(k);
            for (auto& t : ticks) CHECK(!label.intersects(t), 37, "channel label clear of ticks/bars");
            CHECK(std::abs(label.getCentreX() - (k ? 988 : 886)) <= 2, 38, "channel label centred on its bar");
        }
    }
    // Host-style automation to non-round values (as pluginval Editor Automation did on 9862ef0):
    // readouts use spec precision; the parameter keeps its continuous physical value.
    {
        const float values[]{-17.23f, 2.3141f, -4.3271f, -24.4035f};
        const char* ids[]{"gain", "width", "rotation", "asymmetry"};
        const char* expected[]{"-17.2", "2.31", "-4.3", "-24"};
        for (int i = 0; i < 4; ++i) {
            setPhysical(i, values[i]);
            auto* sl = child<juce::Slider>(*editor, ids[i]);
            const auto text = sl->getTextFromValue(sl->getValue());
            if (text != expected[i]) std::cerr << ids[i] << " readout '" << text << "'\n";
            CHECK(text == expected[i], 39, std::string("readout precision ") + ids[i]);
        }
        CHECK(std::abs(physical(3) + 24.4035f) < 1e-3, 40, "asymmetry parameter stays continuous");
        for (int i = 0; i < 4; ++i) setPhysical(i, ranged(i)->convertFrom0to1(ranged(i)->getDefaultValue()));
    }
    // Drawn two-position selectors: knob sits on the L-R side when off, M-S side when on.
    {
        for (const char* id : {"input_mode", "meter_mode"}) {
            auto* sw = dynamic_cast<tsunami::s1::ModeSwitch*>(editor->findChildWithID(id));
            CHECK(sw, 41, std::string("drawn selector ") + id);
            auto knobSide = [&] {
                auto img = sw->createComponentSnapshot(sw->getLocalBounds());
                const auto slot = sw->slotBounds();
                double left = 0, right = 0;
                for (int y = slot.getY(); y < slot.getBottom(); ++y)
                    for (int x = slot.getX(); x < slot.getRight(); ++x)
                        (x < slot.getCentreX() ? left : right) += img.getPixelAt(x, y).getBrightness();
                return left > right ? 0 : 1;
            };
            const bool start = sw->getToggleState();
            sw->setToggleState(false, juce::sendNotificationSync);
            CHECK(knobSide() == 0, 42, std::string("knob on L-R side ") + id);
            sw->setToggleState(true, juce::sendNotificationSync);
            CHECK(knobSide() == 1, 43, std::string("knob on M-S side ") + id);
            sw->setToggleState(start, juce::sendNotificationSync);
        }
        setPhysical(7, 0.f);
    }
    if (auto* dir = std::getenv("TSUNAMI_S1_SNAPSHOT_DIR")) { // in-process component renders, not host captures
        auto shot = [&](const char* name, float width, float rotation) {
            setPhysical(1, width); setPhysical(2, rotation);
            p.processBlock(b, midi); s1->pollMeters();
            juce::File file = juce::File(dir).getChildFile(name); file.deleteFile(); juce::FileOutputStream out(file);
            juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), out);
        };
        shot("candidate-default.png", 1.f, 0.f); shot("candidate-width2.png", 2.f, 0.f); shot("candidate-rotation20.png", 1.f, 20.f);
    }
    std::cout << "S1 editor controls, attachments, automation sync, vector model, reset image, drag, meters PASS\n";
    return 0;
}
