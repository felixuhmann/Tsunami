// SPDX-License-Identifier: GPL-3.0-or-later
// Native test of the product wrapper: the real shared Adapter built from plugins/deesser
// (generated metadata + Processor.hpp + Editor.hpp), as shipped in the VST3/CLAP targets.
// Checks identity/metadata, that the product editor (not the fixture fallback) is attached
// and bound through APVTS, state round trip into DSP and UI, rendering identical to the
// portable core, and the meters it feeds (output peaks; Energy/Atten current post-block values).
#include <juce_audio_utils/juce_audio_utils.h>
#include "Generated.hpp"
#include "Editor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
using namespace tsunami::deesser;
using Product = tsunami::Adapter<tsunami::PluginSpec>;
#define CHECK(c, code, what) do { if (!(c)) { std::cerr << "FAIL " << what << "\n"; return code; } } while (0)

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
template<class T> static T* child(juce::Component& root, const char* id) { return dynamic_cast<T*>(root.findChildWithID(id)); }
static void setPhysical(juce::AudioProcessor& p, const char* id, float v) {
    for (auto* candidate : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(candidate); r && r->getParameterID() == id)
            return r->setValueNotifyingHost(r->convertTo0to1(v));
    std::cerr << "FAIL unknown parameter " << id << "\n"; std::exit(90);
}
static float physical(juce::AudioProcessor& p, const char* id) {
    for (auto* candidate : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(candidate); r && r->getParameterID() == id)
            return r->convertFrom0to1(r->getValue());
    return NAN;
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    CHECK(dynamic_cast<Product*>(p.get()), 1, "createPluginFilter builds the shared Adapter for PluginSpec");
    CHECK(p->getName() == "Tsunami De-Esser" && juce::String(tsunami::PluginSpec::id) == "deesser", 2, "product identity");
    const auto& params = p->getParameters();
    CHECK(params.size() == int(ui::parameterCount), 3, "parameter count");
    for (unsigned i = 0; i < ui::parameterCount; ++i) {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(params[int(i)]);
        CHECK(r && r->getParameterID() == ui::parameterIds[i] && r->getNormalisableRange().start == ui::minimum[i]
              && r->getNormalisableRange().end == ui::maximum[i] && std::abs(physical(*p, ui::parameterIds[i]) - ui::defaults[i]) < .5f,
              4, "parameter order/range/default match plugin metadata");
    }
    CHECK(params[2]->getCurrentValueAsText() == "HighPass" && params[3]->getCurrentValueAsText() == "Split"
          && params[4]->getCurrentValueAsText() == "Output", 5, "choice labels exposed to hosts");

    // CLAP text round trip as clap-validator 0.4.1 param-conversions runs it through clap-juce-extensions
    // (normalised value -> float -> getText(v, size) -> getValueForText -> getText): grid of
    // N = clamp(ceil(4000 / parameterCount), 5, 100) evenly spaced values per parameter (6 params: 600 cases).
    // Native run 40555152920 failed Freq at i=49: '5585.6484375' -> 0.49494943 -> '5585.6474609'.
    {
        std::vector<float> before; for (auto* q : params) before.push_back(q->getValue());
        const int n = std::clamp(int(std::ceil(4000.0 / params.size())), 5, 100);
        CHECK(n == 100, 28, "validator grid size");
        int cases = 0;
        for (auto* q : params)
            for (int i = 0; i < n; ++i, ++cases) {
                const float v = float(double(i) / double(n - 1));
                const auto text = q->getText(v, 256);
                const float parsed = q->getValueForText(text);
                const auto again = q->getText(parsed, 256);
                const float reparsed = q->getValueForText(again);
                if (text != again || parsed != reparsed || !std::isfinite(parsed) || parsed < 0.f || parsed > 1.f) {
                    std::cerr << "FAIL text round trip " << q->getName(64) << " i=" << i << " '" << text << "' -> " << parsed
                              << " -> '" << again << "' -> " << reparsed << "\n";
                    return 29;
                }
            }
        CHECK(cases == 600, 30, "600 validator cases");
        std::vector<float> after; for (auto* q : params) after.push_back(q->getValue());
        CHECK(before == after, 31, "text queries do not change parameter state");
        // Text only: Freq stays continuous (interval 0, same skew/law); non-integral Hz are still values.
        auto* freqParam = dynamic_cast<juce::RangedAudioParameter*>(params[0]);
        const auto& range = freqParam->getNormalisableRange();
        CHECK(range.interval == 0.f && range.skew == tsunami::PluginSpec::parameters[0].skew
              && std::abs(range.convertFrom0to1(range.convertTo0to1(5585.65f)) - 5585.65f) < 1e-2f, 32, "Freq physical law unchanged");
        CHECK(freqParam->getText(range.convertTo0to1(5585.65f), 256) == "5586" && freqParam->getText(range.convertTo0to1(2000.f), 256) == "2000"
              && freqParam->getText(range.convertTo0to1(16000.f), 256) == "16000"
              && freqParam->getText(range.convertTo0to1(5585.65f), 3) == "558", // positive length truncates, as JUCE default
              33, "Freq text is whole Hz (requested length respected)");
        for (int hz = 2000; hz <= 16000; ++hz) {
            const auto text = juce::String(hz);
            const float parsed = freqParam->getValueForText(text);
            if (freqParam->getText(parsed, 256) != text || freqParam->getText(freqParam->getValueForText(freqParam->getText(parsed, 256)), 256) != text) {
                std::cerr << "FAIL whole-Hz text " << hz << " -> '" << freqParam->getText(parsed, 256) << "'\n"; return 34;
            }
        }
        // Unspecified precision keeps JUCE's default (interval 0: 7 decimals); choices keep labels.
        auto* thresholdParam = dynamic_cast<juce::RangedAudioParameter*>(params[1]);
        CHECK(thresholdParam->getText(thresholdParam->convertTo0to1(-43.6f), 256)
              == juce::String(thresholdParam->convertFrom0to1(thresholdParam->convertTo0to1(-43.6f)), 7)
              && params[2]->getText(0.f, 256) == "BandPass", 35, "default formatting and choice labels unchanged");
    }

    p->setPlayConfigDetails(2, 2, 48000, 512);
    p->prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditorAndMakeActive());
    auto* editor = dynamic_cast<Editor*>(ed.get());
    CHECK(editor, 6, "product DeEsser editor attached (not the fixture fallback)");
    auto* audio = child<juce::TextButton>(*ed, "audio");
    auto* freq = child<juce::Slider>(*ed, "freq");
    auto* threshold = child<juce::Slider>(*ed, "threshold");
    auto* sidechain = child<juce::ComboBox>(*ed, "sidechain");
    auto* monA = child<juce::TextButton>(*ed, "monitor_audio");
    auto* monS = child<juce::TextButton>(*ed, "monitor_sidechain");
    CHECK(audio && freq && threshold && sidechain && monA && monS, 7, "panel controls present");
    CHECK(audio->getButtonText() == "Split" && std::lround(freq->getValue()) == 5506 && sidechain->getText() == "HighPass"
          && monA->getToggleState() && !monS->getToggleState(), 8, "default display from Adapter state");
    // Value boxes show the editor's own text from the start (native Windows c99d1b2 showed Threshold
    // '0.000...': the text function was installed after the attachment's first text update).
    auto boxText = [](juce::Slider& sl) {
        for (auto* c : sl.getChildren()) if (auto* l = dynamic_cast<juce::Label*>(c)) return l->getText();
        return juce::String("<no value box>"); };
    CHECK(boxText(*threshold) == "0.0" && boxText(*freq) == "5506", 36, "initial Threshold/Freq value-box text");
    // Before the first meter poll (hosted capture c99d1b2+fixes caught this frame) bars must not show
    // full scale and readouts must already show their idle text, not the '--' placeholder for Atten.
    {
        auto* energyBar = dynamic_cast<BarMeter*>(ed->findChildWithID("energy_meter"));
        auto* outBar = dynamic_cast<BarMeter*>(ed->findChildWithID("output_left"));
        auto* outBarR = dynamic_cast<BarMeter*>(ed->findChildWithID("output_right"));
        auto* attenText = dynamic_cast<Readout*>(ed->findChildWithID("atten_readout"));
        auto* energyText = dynamic_cast<Readout*>(ed->findChildWithID("energy_readout"));
        CHECK(energyBar && outBar && outBarR && attenText && energyText && energyBar->fraction(energyBar->value) == 1.f
              && outBar->fraction(outBar->value) == 1.f && outBarR->fraction(outBarR->value) == 1.f
              && attenText->text == "0.0" && energyText->text == "--", 41, "idle meters before the first poll (empty bars, Atten 0.0)");
    }
    // SideChain shows both whole labels inside the left column (native captures showed 'B...'/'Hi...').
    {
        const auto box = sidechain->getBounds();
        CHECK(box.getX() >= 0 && box.getRight() <= 90 && box.getY() == 140 && box.getHeight() == 24, 37, "SideChain stays in the left column row");
        const int selected = sidechain->getSelectedItemIndex();
        for (int item = 0; item < 2; ++item) {
            sidechain->setSelectedItemIndex(item, juce::dontSendNotification);
            juce::Label* text = nullptr;
            for (auto* c : sidechain->getChildren()) if (auto* l = dynamic_cast<juce::Label*>(c)) text = l;
            CHECK(text != nullptr, 38, "SideChain text label");
            const auto area = text->getBorderSize().subtractedFrom(text->getLocalBounds());
            const float width = juce::GlyphArrangement::getStringWidth(text->getFont(), text->getText());
            if (!(text->getText() == (item ? "HighPass" : "BandPass") && width <= float(area.getWidth()) && text->getRight() <= box.getWidth() - 14)) {
                std::cerr << "FAIL SideChain '" << text->getText() << "' needs " << width << " px, has " << area.getWidth() << "\n"; return 39;
            }
        }
        sidechain->setSelectedItemIndex(selected, juce::dontSendNotification);
    }
    // UI -> host parameter.
    audio->setToggleState(true, juce::sendNotificationSync);
    freq->setValue(4500, juce::sendNotificationSync);
    sidechain->setSelectedItemIndex(0, juce::sendNotificationSync);
    monS->setToggleState(true, juce::sendNotificationSync); // click path: Monitor pair -> one parameter
    CHECK(physical(*p, "audio") == 1.f && audio->getButtonText() == "Wideband", 9, "Audio button binds");
    CHECK(std::abs(physical(*p, "freq") - 4500.f) < 1.f, 10, "Freq box binds");
    CHECK(physical(*p, "sidechain") == 0.f && physical(*p, "monitor") == 1.f, 11, "SideChain/Monitor bind");
    // Host automation -> UI.
    setPhysical(*p, "monitor", 0.f); setPhysical(*p, "threshold", -20.f);
    CHECK(monA->getToggleState() && !monS->getToggleState() && std::abs(threshold->getValue() + 20.0) < .06, 12, "host changes reach the UI");
    // State round trip through the Adapter's schema.
    juce::MemoryBlock saved; p->getStateInformation(saved);
    setPhysical(*p, "audio", 0.f); setPhysical(*p, "freq", 12000.f);
    p->setStateInformation(saved.getData(), int(saved.getSize()));
    CHECK(physical(*p, "audio") == 1.f && std::abs(physical(*p, "freq") - 4500.f) < 1.f && audio->getButtonText() == "Wideband"
          && std::abs(freq->getValue() - 4500.0) < 1.0, 13, "state restore reaches parameters and UI");

    // Frequency numeric window, SideChain filter-shape pictogram and typed entry (49-55).
    {
        struct Gestures final : juce::AudioProcessorListener {
            int begins = 0, ends = 0;
            void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
            void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
            void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
            void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
        } gestures;
        p->addListener(&gestures);
        auto others = [&](const char* except) { std::vector<float> v; for (auto* q : params)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(q); r->getParameterID() != except) v.push_back(r->getValue()); return v; };
        // 49: one value window; no value-proportional fill bar behind the number (hosted capture showed overlap).
        auto snap = [&](double hz) { freq->setValue(hz, juce::sendNotificationSync); return freq->createComponentSnapshot(freq->getLocalBounds()); };
        const auto lo = snap(2000), hi = snap(16000);
        bool sameWindow = true;
        for (int y = 3; y < freq->getHeight() - 3; ++y) for (int x : {3, 4, freq->getWidth() - 5, freq->getWidth() - 4})
            sameWindow = sameWindow && lo.getPixelAt(x, y) == hi.getPixelAt(x, y);
        juce::Label* box = nullptr; for (auto* c : freq->getChildren()) if (auto* l = dynamic_cast<juce::Label*>(c)) box = l;
        CHECK(sameWindow && box && box->getBounds() == freq->getLocalBounds() && box->getText() == "16000", 49, "Frequency value window without fill bar");
        // 50: vertical drag on the window moves Freq along the taper, one balanced gesture, nonintegral host value kept.
        setPhysical(*p, "freq", 5585.65f);
        const auto before = others("freq"); const int b0 = gestures.begins, e0 = gestures.ends;
        CHECK(std::abs(physical(*p, "freq") - 5585.65f) < .01f && box->getText() == "5586", 50, "nonintegral host Freq kept, whole-Hz display");
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime(); const juce::Point<float> down(30.f, 12.f);
        auto ev = [&](juce::Point<float> at, bool dragged) { return juce::MouseEvent(src, at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            1.f, 0.f, 0.f, 0.f, 0.f, freq, freq, now, down, now, 1, dragged); };
        freq->mouseDown(ev(down, false)); freq->mouseDrag(ev(down.translated(0.f, -40.f), true)); freq->mouseUp(ev(down.translated(0.f, -40.f), true));
        CHECK(physical(*p, "freq") > 5700.f && physical(*p, "freq") <= 16000.f && gestures.begins == b0 + 1 && gestures.ends == e0 + 1
              && others("freq") == before, 51, "Frequency drag: up raises, one gesture, other parameters unchanged");
        // 52/53: typed entry; nonfinite/overflow/garbage text never reaches the host parameter.
        for (auto* slider : {freq, threshold}) {
            juce::Label* entry = nullptr; for (auto* c : slider->getChildren()) if (auto* l = dynamic_cast<juce::Label*>(c)) entry = l;
            const char* id = slider == freq ? "freq" : "threshold";
            for (const char* text : {"nan", "inf", "-inf", "1e999", "-1e999", "abc"}) {
                entry->setText(text, juce::sendNotificationSync);
                const float v = physical(*p, id);
                if (!(std::isfinite(v) && std::isfinite(slider->getValue()) && v >= (slider == freq ? 2000.f : -80.f) && v <= (slider == freq ? 16000.f : 0.f))) {
                    std::cerr << "FAIL typed '" << text << "' into " << id << " -> " << v << " (slider " << slider->getValue() << ")\n"; return 52;
                }
            }
        }
        box->setText("7000", juce::sendNotificationSync);
        CHECK(std::abs(physical(*p, "freq") - 7000.f) < 1.f && box->getText() == "7000", 53, "typed Frequency entry");
        // 54: SideChain pictogram follows the parameter (host and state recall while open) with a semantic title.
        auto* shape = child<FilterShapeButton>(*ed, "sidechain_shape");
        CHECK(shape != nullptr, 54, "SideChain filter-shape pictogram present");
        setPhysical(*p, "sidechain", 0.f);
        CHECK(!shape->isHighPass() && shape->getTitle() == "SideChain filter: BandPass" && sidechain->getText() == "BandPass", 54, "pictogram BandPass");
        juce::MemoryBlock hpState; setPhysical(*p, "sidechain", 1.f); p->getStateInformation(hpState); setPhysical(*p, "sidechain", 0.f);
        p->setStateInformation(hpState.getData(), int(hpState.getSize()));
        CHECK(shape->isHighPass() && shape->getTitle() == "SideChain filter: HighPass" && shape->getTooltip().contains("HighPass"), 54, "pictogram follows state recall");
        // 55: clicking the pictogram selects the other shape as one complete gesture; combo and other parameters follow.
        const auto keep = others("sidechain"); const int b1 = gestures.begins, e1 = gestures.ends;
        auto click = [&] { auto at = shape->getLocalBounds().getCentre().toFloat();
            auto sev = [&](bool up) { return juce::MouseEvent(src, at, juce::ModifierKeys(up ? 0 : juce::ModifierKeys::leftButtonModifier), 1.f, 0.f, 0.f, 0.f, 0.f,
                shape, shape, now, at, now, 1, false); };
            auto* c = static_cast<juce::Component*>(shape); c->mouseDown(sev(false)); c->mouseUp(sev(true)); };
        click();
        CHECK(physical(*p, "sidechain") == 0.f && !shape->isHighPass() && sidechain->getText() == "BandPass" && gestures.begins == b1 + 1
              && gestures.ends == e1 + 1 && others("sidechain") == keep, 55, "pictogram click selects BandPass");
        click();
        CHECK(physical(*p, "sidechain") == 1.f && shape->isHighPass() && gestures.begins == gestures.ends, 55, "pictogram click selects HighPass");
        p->removeListener(&gestures);
        // Restore the values the later sections expect.
        setPhysical(*p, "freq", 4500.f);
    }

    // Rendering: the Adapter passes physical values to the core unchanged (bit-identical to the portable core).
    setPhysical(*p, "threshold", -40.f); setPhysical(*p, "audio", 0.f); setPhysical(*p, "sidechain", 1.f);
    Processor reference;
    for (unsigned i = 0; i < ui::parameterCount; ++i) reference.setParameter(i, physical(*p, ui::parameterIds[i]));
    reference.prepare(48000, 2, 512);
    p->reset();
    juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
    std::vector<float> rl(512), rr(512);
    double inEnergy = 0, outEnergy = 0, maxDiff = 0;
    std::uint64_t audioHash = 1469598103934665603ull; // FNV-1a over every output float (complete buffers)
    for (int k = 0; k < 16; ++k) {
        for (int i = 0; i < 512; ++i) {
            const float x = .5f * std::sin(2.f * 3.14159265f * 7000.f * float(k * 512 + i) / 48000.f);
            b.setSample(0, i, x); b.setSample(1, i, .5f * x); rl[size_t(i)] = x; rr[size_t(i)] = .5f * x; inEnergy += double(x) * x;
        }
        const float* in[]{rl.data(), rr.data()}; float* out[]{rl.data(), rr.data()};
        { juce::ScopedNoDenormals sameFloatModeAsAdapter; reference.process(in, out, 512); }
        p->processBlock(b, m);
        for (int i = 0; i < 512; ++i) {
            maxDiff = std::max({maxDiff, double(std::abs(b.getSample(0, i) - rl[size_t(i)])), double(std::abs(b.getSample(1, i) - rr[size_t(i)]))});
            outEnergy += double(b.getSample(0, i)) * b.getSample(0, i);
            for (int c = 0; c < 2; ++c) {
                const float v = b.getSample(c, i); unsigned char bytes[sizeof v]; std::memcpy(bytes, &v, sizeof v);
                for (auto byte : bytes) audioHash = (audioHash ^ byte) * 1099511628211ull;
            }
        }
    }
    std::cout << "AUDIO_FNV1A64 " << std::hex << audioHash << std::dec << "\n";
    CHECK(maxDiff == 0.0, 14, "Adapter render bit-identical to tsunami::deesser::Processor");
    CHECK(outEnergy < .5 * inEnergy, 15, "7 kHz sibilant attenuated at Threshold -40 dB through the wrapper");

    // Meters: output peaks from the shared MeterSource; Energy/Atten from the core's current
    // post-block meter(0/1) published through the shared CoreMeterSource (observation only).
    auto* coreMeters = dynamic_cast<tsunami::CoreMeterSource*>(p.get());
    CHECK(coreMeters && coreMeters->coreMeterCount() == 2, 16, "core Energy/Atten meters exposed through the shared Adapter");
    CHECK(coreMeters->coreMeter(0) == reference.meter(0) && coreMeters->coreMeter(1) == reference.meter(1)
          && coreMeters->coreMeter(1) >= 3.f && coreMeters->coreMeter(2) == 0.f, 17, "published values equal the core's post-block meters");
    editor->pollMetersForTest();
    CHECK(editor->meterModel().outputPeakDb[0] > -20.f && editor->meterModel().outputPeakDb[1] > editor->meterModel().outputPeakDb[0] - 7.f,
          18, "Output meters fed by the Adapter's MeterSource");
    CHECK(editor->meterModel().energyDb == coreMeters->coreMeter(0) && editor->meterModel().attenDb == coreMeters->coreMeter(1), 19,
          "Energy/Atten bars fed from the Adapter's CoreMeterSource");
    for (const char* id : {"energy", "atten"}) {
        auto* readout = dynamic_cast<Readout*>(ed->findChildWithID(juce::String(id) + "_readout"));
        auto* bar = dynamic_cast<BarMeter*>(ed->findChildWithID(juce::String(id) + "_meter"));
        CHECK(readout && readout->text != "--" && bar && bar->available, 20, "Energy/Atten readouts and bars show data");
    }
    // Current post-block values, not a lifetime maximum: after 2 s of silence the published meters
    // fall while the core's held take*() peaks (separate contract) still report the loud passage.
    const float heldReduction = reference.gainReductionDb(), heldDetector = reference.detectorLevelDb();
    for (int k = 0; k < 188; ++k) {
        b.clear(); std::fill(rl.begin(), rl.end(), 0.f); std::fill(rr.begin(), rr.end(), 0.f);
        const float* in[]{rl.data(), rr.data()}; float* out[]{rl.data(), rr.data()};
        { juce::ScopedNoDenormals sameFloatModeAsAdapter; reference.process(in, out, 512); }
        p->processBlock(b, m);
    }
    CHECK(coreMeters->coreMeter(0) == reference.meter(0) && coreMeters->coreMeter(1) == reference.meter(1)
          && coreMeters->coreMeter(1) < heldReduction - 1.f && coreMeters->coreMeter(0) < heldDetector - 10.f
          && reference.gainReductionDb() == heldReduction, 21, "published meters are current, not held maxima");
    editor->pollMetersForTest();
    CHECK(editor->meterModel().attenDb == coreMeters->coreMeter(1) && editor->meterModel().attenPeakDb >= 3.f, 22,
          "Atten bar follows the current value; the editor's own peak readout holds");
    // Paint the product editor: non-uniform image of the expected size.
    auto image = ed->createComponentSnapshot(ed->getLocalBounds());
    CHECK(image.getWidth() == 380 && image.getHeight() == 380 && image.getPixelAt(5, 5) != image.getPixelAt(158, 200), 23, "editor renders");
    // Readouts never show '-0.0' (native captures showed Output '-0.0' for peaks just below 0 dBFS);
    // real negative tenths still show. Display only: the meter values themselves are unchanged.
    {
        setPhysical(*p, "bypass", 1.f);
        auto outReadout = [&](int x) -> Readout* { // by layout position (the unattached editor is not visible)
            for (auto* c : ed->getChildren()) if (auto* r = dynamic_cast<Readout*>(c); r && r->getBounds().contains(x, 270)) return r;
            return nullptr; };
        for (auto [peak, expected] : {std::pair{.99f, "-0.1"}, std::pair{.9995f, "0.0"}}) {
            for (int i = 0; i < 512; ++i) for (int c = 0; c < 2; ++c) b.setSample(c, i, i == 100 ? peak : 0.f);
            p->processBlock(b, m);
            editor->pollMetersForTest();
            auto* left = outReadout(300); auto* right = outReadout(344);
            if (!(left && right && left->text == expected && right->text == expected)) {
                std::cerr << "FAIL output readout for peak " << peak << ": '" << (left ? left->text : "<none>") << "' expected '" << expected << "'\n";
                return 40;
            }
        }
        setPhysical(*p, "bypass", 0.f);
    }
    // Host-rate policy (not vendor parity): hosts/validators may prepare outside the accepted 8-384 kHz
    // DSP domain (clap-validator 0.4.1 process-varying-sample-rates aborted on the core's thrown
    // std::invalid_argument, run 40555152920). The core must not throw into the host; there it passes
    // audio through unchanged (as Bypass) and resumes de-essing once prepared at a supported rate.
    for (double rate : {1000.0, 768000.0}) {
        try { p->prepareToPlay(rate, 512); }
        catch (const std::exception& e) { std::cerr << "FAIL prepareToPlay(" << rate << ") threw: " << e.what() << "\n"; return 24; }
        juce::AudioBuffer<float> dry(2, 512);
        for (int i = 0; i < 512; ++i) for (int c = 0; c < 2; ++c) dry.setSample(c, i, .5f * std::sin(.3f * float(i + c)));
        b.makeCopyOf(dry); p->processBlock(b, m);
        for (int i = 0; i < 512; ++i) for (int c = 0; c < 2; ++c)
            CHECK(b.getSample(c, i) == dry.getSample(c, i), 25, "unsupported host rate passes audio through unchanged");
        CHECK(coreMeters->coreMeter(1) == 0.f && coreMeters->coreMeter(0) == -120.f, 26, "idle Energy/Atten at an unsupported rate");
    }
    p->prepareToPlay(48000, 512);
    for (int k = 0; k < 8; ++k) {
        for (int i = 0; i < 512; ++i) for (int c = 0; c < 2; ++c)
            b.setSample(c, i, .5f * std::sin(2.f * 3.14159265f * 7000.f * float(k * 512 + i) / 48000.f));
        p->processBlock(b, m);
    }
    CHECK(coreMeters->coreMeter(1) >= 3.f, 27, "de-essing resumes after re-prepare at a supported rate");
    // Mono 1-in/1-out product layout (opt-in supports_mono; the core supports prepare(rate, 1, block)).
    // Mono is proven by the 1-in/1-out bus layout (43); matching the 1-channel core checks processing only
    // (with L = R a stereo core would give the same samples).
    {
        using Layout = juce::AudioProcessor::BusesLayout;
        const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
        auto layout = [](juce::AudioChannelSet in, juce::AudioChannelSet out) { Layout l; l.inputBuses.add(in); l.outputBuses.add(out); return l; };
        CHECK(!p->checkBusesLayoutSupported(layout(mono, stereo)) && !p->checkBusesLayoutSupported(layout(stereo, mono)), 42,
              "asymmetric mono/stereo layouts rejected");
        p->releaseResources();
        CHECK(p->checkBusesLayoutSupported(layout(mono, mono)) && p->setBusesLayout(layout(mono, mono))
              && p->getTotalNumInputChannels() == 1 && p->getTotalNumOutputChannels() == 1, 43, "mono 1-in/1-out layout accepted");
        setPhysical(*p, "threshold", -40.f); setPhysical(*p, "audio", 0.f); setPhysical(*p, "sidechain", 1.f); setPhysical(*p, "bypass", 0.f);
        p->prepareToPlay(48000, 512);
        Processor monoCore;
        for (unsigned i = 0; i < ui::parameterCount; ++i) monoCore.setParameter(i, physical(*p, ui::parameterIds[i]));
        monoCore.prepare(48000, 1, 512);
        juce::AudioBuffer<float> mb(1, 512); std::vector<float> mr(512);
        double inE = 0, outE = 0, diff = 0; bool finite = true;
        for (int k = 0; k < 16; ++k) {
            for (int i = 0; i < 512; ++i) { const float x = .5f * std::sin(2.f * 3.14159265f * 7000.f * float(k * 512 + i) / 48000.f);
                                             mb.setSample(0, i, x); mr[size_t(i)] = x; inE += double(x) * x; }
            const float* in[]{mr.data()}; float* out[]{mr.data()};
            { juce::ScopedNoDenormals sameFloatModeAsAdapter; monoCore.process(in, out, 512); }
            p->processBlock(mb, m);
            for (int i = 0; i < 512; ++i) { const float y = mb.getSample(0, i); finite = finite && std::isfinite(y);
                                             diff = std::max(diff, double(std::abs(y - mr[size_t(i)]))); outE += double(y) * y; }
        }
        CHECK(finite && diff == 0.0, 44, "mono render bit-identical to the 1-channel core, finite");
        CHECK(outE < .5 * inE && coreMeters->coreMeter(1) >= 3.f && coreMeters->coreMeter(1) == monoCore.meter(1)
              && coreMeters->coreMeter(0) == monoCore.meter(0), 45, "active de-essing and Energy/Atten meters in mono");
        juce::MemoryBlock monoState; p->getStateInformation(monoState);
        setPhysical(*p, "threshold", -10.f);
        p->setStateInformation(monoState.getData(), int(monoState.getSize()));
        CHECK(std::abs(physical(*p, "threshold") + 40.f) < .06f && p->getTotalNumOutputChannels() == 1, 46, "mono control readback and state round trip");
        // Back to stereo: full stereo function restored.
        p->releaseResources();
        CHECK(p->setBusesLayout(layout(stereo, stereo)) && p->getTotalNumOutputChannels() == 2, 47, "stereo layout restored");
        p->prepareToPlay(48000, 512);
        Processor stereoCore;
        for (unsigned i = 0; i < ui::parameterCount; ++i) stereoCore.setParameter(i, physical(*p, ui::parameterIds[i]));
        stereoCore.prepare(48000, 2, 512);
        diff = 0;
        for (int k = 0; k < 8; ++k) {
            for (int i = 0; i < 512; ++i) { const float x = .5f * std::sin(2.f * 3.14159265f * 7000.f * float(k * 512 + i) / 48000.f);
                                             b.setSample(0, i, x); b.setSample(1, i, .5f * x); rl[size_t(i)] = x; rr[size_t(i)] = .5f * x; }
            const float* in[]{rl.data(), rr.data()}; float* out[]{rl.data(), rr.data()};
            { juce::ScopedNoDenormals sameFloatModeAsAdapter; stereoCore.process(in, out, 512); }
            p->processBlock(b, m);
            for (int i = 0; i < 512; ++i) diff = std::max({diff, double(std::abs(b.getSample(0, i) - rl[size_t(i)])), double(std::abs(b.getSample(1, i) - rr[size_t(i)]))});
        }
        CHECK(diff == 0.0 && coreMeters->coreMeter(1) >= 3.f, 48, "stereo render and de-essing after returning from mono");
    }
    // 56: closing the editor during a Frequency drag leaves no open host gesture.
    {
        struct Gestures final : juce::AudioProcessorListener {
            int begins = 0, ends = 0;
            void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
            void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
            void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
            void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
        } gestures;
        p->addListener(&gestures);
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime(); const juce::Point<float> down(30.f, 12.f);
        freq->mouseDown(juce::MouseEvent(src, down, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.f, 0.f, 0.f, 0.f, 0.f,
                                         freq, freq, now, down, now, 1, false));
        const int begun = gestures.begins;
        ed.reset();
        p->removeListener(&gestures);
        if (begun != 1 || gestures.ends != gestures.begins) {
            std::cerr << "FAIL editor closed mid-drag: begins " << gestures.begins << " ends " << gestures.ends << "\n"; return 56;
        }
    }
    p->releaseResources();
    std::cout << "PASS DeEsser real Adapter: identity, editor bindings, state, render parity, meters\n";
    return 0;
}
