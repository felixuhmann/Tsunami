// SPDX-License-Identifier: GPL-3.0-or-later
// Native JUCE test of the actual S1 editor: real components, APVTS attachments,
// state sync, reset-image semantics and process-derived output meters.
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Adapter.hpp"
#include "Generated.hpp"

#define CHECK(cond, code, what) do { if (!(cond)) { std::cerr << "FAIL " << what << "\n"; return code; } } while (0)

template<class T> static T* child(juce::Component& root, const char* id) {
    return dynamic_cast<T*>(root.findChildWithID(id));
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    tsunami::Adapter<tsunami::PluginSpec> p;
    p.prepareToPlay(48000, 64);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor, 1, "editor created");
    auto* s1 = dynamic_cast<tsunami::s1::Editor*>(editor.get());
    CHECK(s1, 2, "actual S1 editor (not fixture fallback)");
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
    CHECK(std::abs(vec.left.angleDegrees + 90.f) < 1e-3 && std::abs(vec.right.angleDegrees - 90.f) < 1e-3, 8, "default L/R at display edges");
    CHECK(std::abs(vec.center.angleDegrees) < 1e-3 && std::abs(vec.left.levelDb + 6.f) < 1e-3, 9, "centre upright, gain shown as level");
    auto rotated = tsunami::s1::vectorModel({0.f, 1.f, 20.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
    CHECK(std::abs(rotated.center.angleDegrees - 40.f) < 1e-3, 10, "rotation turns centre by twice the matrix angle");

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
    if (auto* path = std::getenv("TSUNAMI_S1_SNAPSHOT")) { // in-process component render, not a host capture
        p.processBlock(b, midi); s1->pollMeters();
        juce::File file(path); file.deleteFile(); juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), out);
    }
    std::cout << "S1 editor controls, attachments, automation sync, vector model, reset image, drag, meters PASS\n";
    return 0;
}
