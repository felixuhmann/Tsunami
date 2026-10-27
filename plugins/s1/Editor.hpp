// SPDX-License-Identifier: GPL-3.0-or-later
// Tsunami Stereo editor: independently drawn three-panel layout (input faders,
// control-transfer vector display, output meters). Controls bind via APVTS
// attachments; meters read the adapter's optional lock-free MeterSource.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Adapter.hpp"
#include "EditorModel.hpp"
#include <array>
#include <memory>

namespace tsunami::s1 {
namespace ui {
const juce::Colour panel{0xffc9c8c2}, panelEdge{0xff8f8e88}, ink{0xff1e1f22}, screen{0xff17303d},
    ring{0xff2f7fa6}, fill{0x996b5fb4}, trace{0xffd8d8d8}, meter{0xfff09a22}, strip{0xff2a2c30};
inline juce::Font font(float height, bool bold=false) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}
}

class VectorDisplay final : public juce::Component {
public:
    VectorDisplay(juce::AudioProcessorValueTreeState& s) : state(s) { setComponentID("vector_display"); }
    std::function<void()> onReset;
    void paint(juce::Graphics& g) override {
        auto area=getLocalBounds().toFloat();
        g.setColour(juce::Colours::black); g.fillRoundedRectangle(area, 4.f);
        auto inner=area.reduced(6.f); g.setColour(ui::screen); g.fillRect(inner.withTrimmedBottom(26.f));
        const float originX=inner.getCentreX(), originY=inner.getBottom()-30.f;
        const float radius=juce::jmin(inner.getWidth()*.5f-6.f, originY-inner.getY()-6.f);
        g.setColour(ui::ring);
        for(int i=1;i<=4;++i) { const float r=radius*float(i)/4.f;
            juce::Path arc; arc.addCentredArc(originX, originY, r, r, 0.f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath(arc, juce::PathStrokeType(1.2f)); }
        g.setFont(ui::font(13.f)); g.setColour(juce::Colours::white.withAlpha(.85f));
        const char* marks[]{"0","-6","-12","-18","-24","-18","-12","-6","0"};
        for(int i=0;i<9;++i) { const float x=originX-radius+radius*float(i)/4.f;
            g.drawText(marks[i], juce::Rectangle<float>(x-18.f, originY+6.f, 36.f, 16.f), juce::Justification::centred); }
        auto model=vectorModel(current());
        auto at=[&](VectorPoint p) {
            const float r=radius*juce::jlimit(0.f, 1.15f, (p.levelDb+24.f)/24.f);
            const float a=juce::degreesToRadians(p.angleDegrees);
            return juce::Point<float>(originX+r*std::sin(a), originY-r*std::cos(a));
        };
        const auto o=juce::Point<float>(originX, originY), l=at(model.left), c=at(model.center), r=at(model.right);
        juce::Path image; image.startNewSubPath(o); image.lineTo(l); image.lineTo(r); image.closeSubPath(); // origin-L-R triangle
        g.saveState(); g.reduceClipRegion(inner.withTrimmedBottom(26.f).toNearestInt());
        g.setColour(ui::fill); g.fillPath(image);
        g.setColour(ui::trace);
        g.drawLine({o, l}, 2.f); g.drawLine({o, r}, 2.f); g.drawLine({l, r}, 2.f);
        g.drawLine({o, c}, 2.f); // independent centre ray
        g.setFont(ui::font(12.f, true)); g.drawText("L", juce::Rectangle<float>(l.x-8.f, l.y-18.f, 16.f, 14.f), juce::Justification::centred);
        g.drawText("R", juce::Rectangle<float>(r.x-8.f, r.y-18.f, 16.f, 14.f), juce::Justification::centred);
        g.restoreState();
    }
    ~VectorDisplay() override { endDrag(); } // editor closed mid-drag: never leave host gestures open
    void mouseDown(const juce::MouseEvent&) override {
        if(dragging) return; // repeated press without release: keep the one open gesture, never nest
        for(auto* id : ids) state.getParameter(id)->beginChangeGesture();
        start=image(); dragging=true;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!dragging) return;
        const auto next=applyVectorDrag(start, float(e.getDistanceFromDragStartX()), float(e.getDistanceFromDragStartY()), e.mods.isAltDown());
        for(int i=0;i<4;++i) { auto* p=state.getParameter(ids[i]); p->setValueNotifyingHost(p->convertTo0to1(next[size_t(i)])); }
    }
    void mouseUp(const juce::MouseEvent&) override { endDrag(); }
    void endDrag() {
        if(!dragging) return;
        dragging=false;
        for(auto* id : ids) state.getParameter(id)->endChangeGesture();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { if(onReset) onReset(); }
private:
    static constexpr const char* ids[4]{"gain","width","rotation","asymmetry"};
    std::array<float,4> image() const {
        std::array<float,4> v{}; for(int i=0;i<4;++i) v[size_t(i)]=state.getRawParameterValue(ids[i])->load(); return v;
    }
    std::array<float,Processor::Count> current() const {
        static constexpr const char* all[Processor::Count]{"gain","width","rotation","asymmetry","swap","invert_left","invert_right","input_mode","bypass"};
        std::array<float,Processor::Count> v{};
        for(size_t i=0;i<v.size();++i) v[i]=state.getRawParameterValue(all[i])->load();
        return v;
    }
    juce::AudioProcessorValueTreeState& state;
    std::array<float,4> start{};
    bool dragging=false;
};

// Transparent click target over a painted clip lamp or held-peak readout.
class PeakTarget final : public juce::Component {
public:
    std::function<void()> onReset;
    void mouseDown(const juce::MouseEvent&) override { if(onReset) onReset(); }
};

// Independently drawn fader look: black groove, dark grooved rectangular cap with a
// pale centre index line and a drawn bevel. No bitmaps/textures.
class FaderLook final : public juce::LookAndFeel_V4 {
public:
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& s) override {
        juce::Slider::SliderLayout layout;
        if(s.isVertical()) { // column: travel y82..370 in editor space, value box at y414
            layout.sliderBounds={0, 82-s.getY(), s.getWidth(), 288};
            layout.textBoxBounds={0, s.getHeight()-25, s.getWidth(), 25};
        } else {             // row: track x365..682, value box at x713
            layout.sliderBounds={365-s.getX(), 0, 317, s.getHeight()-14};
            layout.textBoxBounds={713-s.getX(), 0, 62, 25};
        }
        return layout;
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider& s) override {
        const bool vertical=s.isVertical();
        const float cx=vertical ? float(x)+float(w)*.5f : pos, cy=vertical ? pos : float(y)+float(h)*.5f;
        g.setColour(juce::Colour(0x33000000));
        if(vertical) g.fillRoundedRectangle(cx-9.f, float(y)-6.f, 18.f, float(h)+12.f, 6.f);
        else g.fillRoundedRectangle(float(x)-6.f, cy-9.f, float(w)+12.f, 18.f, 6.f);
        g.setColour(juce::Colours::black);
        if(vertical) g.fillRoundedRectangle(cx-3.5f, float(y), 7.f, float(h), 3.f);
        else g.fillRoundedRectangle(float(x), cy-3.5f, float(w), 7.f, 3.f);
        const auto cap=vertical ? juce::Rectangle<float>(cx-20.f, cy-13.f, 40.f, 26.f)
                                : juce::Rectangle<float>(cx-14.f, cy-20.f, 28.f, 40.f);
        g.setColour(juce::Colour(0x55000000)); g.fillRoundedRectangle(cap.translated(3.f, 3.f), 2.f); // drop shadow
        g.setGradientFill(vertical ? juce::ColourGradient(juce::Colour(0xff5a5a5c), 0.f, cap.getY(), juce::Colour(0xff151517), 0.f, cap.getBottom(), false)
                                   : juce::ColourGradient(juce::Colour(0xff5a5a5c), cap.getX(), 0.f, juce::Colour(0xff151517), cap.getRight(), 0.f, false));
        g.fillRoundedRectangle(cap, 2.f);
        for(int i=-4;i<=4;++i) { // grooves
            if(i==0) continue;
            const float o=float(i)*2.6f;
            g.setColour(juce::Colour(0xff0a0a0b));
            if(vertical) g.drawLine(cap.getX()+2.f, cy+o, cap.getRight()-2.f, cy+o, 1.f);
            else g.drawLine(cx+o, cap.getY()+2.f, cx+o, cap.getBottom()-2.f, 1.f);
            g.setColour(juce::Colour(0xff6c6c6e));
            if(vertical) g.drawLine(cap.getX()+2.f, cy+o+1.f, cap.getRight()-2.f, cy+o+1.f, .6f);
            else g.drawLine(cx+o+1.f, cap.getY()+2.f, cx+o+1.f, cap.getBottom()-2.f, .6f);
        }
        g.setColour(juce::Colour(0xfff2ecd2)); // pale index line
        if(vertical) g.fillRect(cap.getX()+2.f, cy-1.f, cap.getWidth()-4.f, 2.f);
        else g.fillRect(cx-1.f, cap.getY()+2.f, 2.f, cap.getHeight()-4.f);
        g.setColour(juce::Colour(0xff8c8c8e)); g.drawLine(cap.getX(), cap.getY(), cap.getRight(), cap.getY(), 1.f); // bevel
        g.drawLine(cap.getX(), cap.getY(), cap.getX(), cap.getBottom(), 1.f);
        g.setColour(juce::Colours::black); g.drawRoundedRectangle(cap, 2.f, 1.f);
    }
};

// Drawn two-position selector (L-R | M-S). Real toggle state: off=L-R, on=M-S.
class ModeSwitch final : public juce::Button {
public:
    ModeSwitch() : juce::Button("mode") { setClickingTogglesState(true); }
    juce::Rectangle<int> slotBounds() const { return getLocalBounds().reduced(44, 3); }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        const bool ms=getToggleState();
        auto area=getLocalBounds();
        g.setFont(juce::Font(juce::FontOptions(15.f)));
        g.setColour(juce::Colour(ms ? 0xff6a6a66 : 0xff1e1f22)); g.drawText("L-R", area.removeFromLeft(42), juce::Justification::centred);
        g.setColour(juce::Colour(ms ? 0xff1e1f22 : 0xff6a6a66)); g.drawText("M-S", area.removeFromRight(42), juce::Justification::centred);
        const auto slot=slotBounds().toFloat();
        g.setColour(juce::Colour(0xff1a1b1d)); g.fillRoundedRectangle(slot, 4.f);
        g.setColour(juce::Colours::black); g.drawRoundedRectangle(slot, 4.f, 1.2f);
        auto knob=slot.reduced(2.f).withWidth(slot.getWidth()*.5f-2.f);
        if(ms) knob=knob.withX(slot.getCentreX());
        g.setGradientFill(juce::ColourGradient(juce::Colour(over ? 0xfff4f4f0 : 0xffe6e6e2), knob.getX(), knob.getY(),
                                               juce::Colour(0xff9a9a96), knob.getX(), knob.getBottom(), false));
        g.fillRoundedRectangle(knob, 3.f);
        g.setColour(juce::Colour(0xff7a7a76));
        for(int i=1;i<6;++i) { const float x=knob.getX()+knob.getWidth()*float(i)/6.f; g.drawLine(x, knob.getY()+3.f, x, knob.getBottom()-3.f, .8f); }
    }
};

// Manual p6: dragging the space between Asymmetry and Rotation links them like a balance.
// Replacement policy (not measured original sensitivity): one common normalized delta of
// dx / trackWidth for both; the delta is clamped so neither leaves its range, which keeps
// their normalized offset. Gestures cover only the two linked parameters.
class LinkStrip final : public juce::Component {
public:
    LinkStrip(juce::AudioProcessorValueTreeState& s) : state(s) {
        setComponentID("link_strip"); setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    std::function<void()> onLinked;
    static constexpr float trackWidth=317.f;
    ~LinkStrip() override { endDrag(); } // editor closed mid-drag: never leave host gestures open
    void mouseDown(const juce::MouseEvent&) override {
        if(dragging) return; // repeated press without release: keep the one open gesture, never nest
        for(size_t i=0;i<2;++i) { auto* p=state.getParameter(ids[i]); p->beginChangeGesture(); start[i]=p->getValue(); }
        dragging=true;
        if(onLinked) onLinked();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!dragging) return;
        float delta=(e.position.x-e.mouseDownPosition.x)/trackWidth; // sub-pixel (int distance rounds)
        for(size_t i=0;i<2;++i) delta=juce::jlimit(-start[i], 1.f-start[i], delta);
        for(size_t i=0;i<2;++i) state.getParameter(ids[i])->setValueNotifyingHost(start[i]+delta);
    }
    void mouseUp(const juce::MouseEvent&) override { endDrag(); }
    void endDrag() {
        if(!dragging) return;
        dragging=false;
        for(auto* id : ids) state.getParameter(id)->endChangeGesture();
    }
private:
    static constexpr const char* ids[2]{"asymmetry","rotation"};
    juce::AudioProcessorValueTreeState& state;
    std::array<float,2> start{};
    bool dragging=false;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    Editor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
        : AudioProcessorEditor(p), state(s), meters(dynamic_cast<MeterSource*>(&p)), display(s), linkStrip(s) {
        vertical(gain, "gain", 0.0, 1); vertical(width, "width", 1.0, 2);
        // Anchor-calibrated replacement taper (slider only, after attachment): the original 17.1
        // static capture shows Width 1.00 at mid-travel. 0 bottom / 1 middle / 3 top; the curve
        // between anchors is not the recovered original law. Parameter stays physical 0..3.
        // SliderAttachment installs a lambda range (ignores skew), so replace the slider's own
        // display range; the attachment still converts physical values via the parameter.
        { juce::NormalisableRange<double> taper(0.0, 3.0); taper.setSkewForCentre(1.0); width.setNormalisableRange(taper); }
        horizontal(asymmetry, "asymmetry", 0.0, 0); horizontal(rotation, "rotation", 0.0, 1);
        toggle(inputMode, "input_mode", "L-R | M-S"); toggle(invertLeft, "invert_left", "+/-");
        toggle(swap, "swap", "<->"); toggle(invertRight, "invert_right", "+/-"); toggle(bypass, "bypass", "Bypass");
        meterMode.setComponentID("meter_mode"); meterMode.setButtonText("L-R active");
        meterMode.onClick=[this] { displayed={-100.f,-100.f}; resetHold(0); resetHold(1); refreshPolarity(); repaint(); }; // display-only, never audio
        addAndMakeVisible(meterMode);
        display.onReset=[this] { resetImage(); };
        for(int k=0;k<2;++k) {
            lamps[size_t(k)].setComponentID("peak_lamp_"+juce::String(k)); readouts[size_t(k)].setComponentID("peak_readout_"+juce::String(k));
            lamps[size_t(k)].onReset=readouts[size_t(k)].onReset=[this,k] { resetHold(k); };
            addAndMakeVisible(lamps[size_t(k)]); addAndMakeVisible(readouts[size_t(k)]);
        }
        addAndMakeVisible(display);
        linkStrip.onLinked=[this] { setLinked(true); };
        addAndMakeVisible(linkStrip);
        setLinked(false);
        setWantsKeyboardFocus(true);
        refreshPolarity();
        // Uniform resizing 50..125 %: children keep logical 1048x665 bounds and receive one
        // scale transform; paint draws through the same transform (no bitmap scaling).
        setResizable(true, true);
        setResizeLimits(524, 333, 1310, 831);
        getConstrainer()->setFixedAspectRatio(1048.0/665.0);
        setSize(1048, 665);
        startTimerHz(30);
    }
    ~Editor() override {
        stopTimer();
        for(auto* sl : {&gain, &width, &asymmetry, &rotation}) sl->setLookAndFeel(nullptr);
    }
    // Manual: reset image restores width/asymmetry/rotation; gain and input options stay.
    void resetImage() {
        for(auto* id : {"width","rotation","asymmetry"}) {
            auto* p=state.getParameter(id);
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
        }
    }
    void pollMeters() {
        if(!meters) return;
        const auto peaks=meters->takeOutputPeaks();
        const bool ms=meterMode.getToggleState();
        for(int k=0;k<2;++k) {
            const float peak=peaks[size_t(ms ? 2+k : k)];
            const float db=peak>1e-5f ? juce::Decibels::gainToDecibels(peak) : -100.f;
            displayed[size_t(k)]=juce::jmax(db, displayed[size_t(k)]-1.5f);
            // Replacement policy (not measured original): infinite hold and latched clip
            // (|x|>=1 of the displayed LR or MS signal) until clicked or meter mode changes.
            held[size_t(k)]=juce::jmax(held[size_t(k)], db);
            if(peak>=1.f) clip[size_t(k)]=true;
        }
        repaint(meterArea.toFloat().transformedBy(scaleTransform()).getSmallestIntegerContainer());
    }
    std::array<float,2> meterReadings() const { return displayed; }
    // Channel label above its clip lamp, centred on the bar and clear of the shared tick scale.
    juce::Rectangle<int> channelLabelBounds(int k) const { return {(k ? 988 : 886)-10, 38, 20, 14}; }
    void setLinked(bool on) { getProperties().set("s1_linked", on); repaint(); }
    bool isLinked() const { return bool(getProperties()["s1_linked"]); }
    void mouseDown(const juce::MouseEvent&) override { setLinked(false); } // click elsewhere unlinks
    float heldPeakDb(int k) const { return held[size_t(k)]; }
    bool clipped(int k) const { return clip[size_t(k)]; }
    void resetHold(int k) { held[size_t(k)]=-100.f; clip[size_t(k)]=false; repaint(); }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key.getTextCharacter()=='r' || key.getTextCharacter()=='R') { resetImage(); return true; }
        return false;
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(ui::strip);
        g.addTransform(scaleTransform());
        for(auto r : {leftPanel, centrePanel, rightPanel}) {
            g.setGradientFill(juce::ColourGradient(ui::panel.brighter(.15f), 0.f, float(r.getY()), ui::panel.darker(.08f), 0.f, float(r.getBottom()), false));
            g.fillRect(r); g.setColour(ui::panelEdge); g.drawRect(r, 1);
        }
        g.setColour(ui::ink); g.setFont(ui::font(16.f));
        g.drawText("Gain", gain.getX()-10, 40, gain.getWidth()+20, 20, juce::Justification::centred);
        g.drawText("Width", width.getX()-10, 40, width.getWidth()+20, 20, juce::Justification::centred);
        g.drawText("Input Mode", leftPanel.getX(), inputMode.getY()-24, leftPanel.getWidth(), 20, juce::Justification::centred);
        if(isLinked()) { // visible selection outline around the linked pair
            g.setColour(juce::Colour(0xff6b5fb4)); g.drawRoundedRectangle(juce::Rectangle<float>(234.f, 406.f, 548.f, 154.f), 6.f, 2.f);
        }
        g.setColour(ui::ink); g.setFont(ui::font(16.f));
        g.drawText("Asymmetry", 240, 414, 110, 26, juce::Justification::centredLeft);
        g.drawText("Rotation", 240, 499, 110, 26, juce::Justification::centredLeft);
        // Scales: Gain 0/-12/-24, Width 3.0/0.0 (dense minor ticks), Asymmetry -90/0/90, Rotation -45/0/45.
        g.setFont(ui::font(14.f));
        auto vScale=[&](int trackX, std::initializer_list<std::pair<float,const char*>> labels) {
            for(int i=0;i<=48;++i) { const float y=82.f+288.f*float(i)/48.f; const bool major=i%12==0;
                g.drawLine(float(trackX-26), y, float(trackX-(major ? 14 : 20)), y, major ? 1.2f : .6f); }
            for(auto [f,text] : labels) g.drawText(text, trackX-64, int(82.f+288.f*f)-8, 36, 16, juce::Justification::centredRight);
        };
        vScale(72, {{0.f,"0"},{.5f,"-12"},{1.f,"-24"}});
        vScale(160, {{0.f,"3.0"},{1.f,"0.0"}});
        auto hScale=[&](int trackY, const char* lo, const char* hi) {
            for(int i=0;i<=40;++i) { const float x=365.f+317.f*float(i)/40.f; const bool major=i%20==0;
                g.drawLine(x, float(trackY+16), x, float(trackY+(major ? 30 : 24)), major ? 1.2f : .6f); }
            g.drawText(lo, 345, trackY+31, 40, 16, juce::Justification::centred);
            g.drawText("0", 503, trackY+31, 40, 16, juce::Justification::centred);
            g.drawText(hi, 662, trackY+31, 40, 16, juce::Justification::centred);
        };
        hScale(427, "-90", "90"); hScale(512, "-45", "45");
        g.drawText("Output", rightPanel.getX(), 10, rightPanel.getWidth(), 20, juce::Justification::centred);
        g.drawText("Meter Mode", rightPanel.getX(), meterMode.getY()-24, rightPanel.getWidth(), 20, juce::Justification::centred);
        const bool ms=meterMode.getToggleState();
        const char* names[2]{ms ? "M" : "L", ms ? "S" : "R"};
        const int barTop=74, barBottom=399;
        for(int k=0;k<2;++k) {
            auto bar=juce::Rectangle<int>(k ? 974 : 872, barTop, 28, barBottom-barTop);
            g.setColour(juce::Colours::black); g.fillRoundedRectangle(bar.toFloat(), 3.f);
            const float level=juce::jlimit(0.f, 1.f, (displayed[size_t(k)]+30.f)/30.f);
            auto lit=bar.reduced(3).toFloat(); lit=lit.withTrimmedTop(lit.getHeight()*(1.f-level));
            g.setGradientFill(juce::ColourGradient(ui::meter.brighter(.3f), lit.getX(), 0.f, ui::meter.darker(.3f), lit.getRight(), 0.f, false));
            g.fillRect(lit);
            auto lamp=lamps[size_t(k)].getBounds().toFloat();
            g.setColour(clip[size_t(k)] ? juce::Colour(0xffff3020) : juce::Colour(0xff3a3a3a)); g.fillRoundedRectangle(lamp, 3.f);
            g.setColour(juce::Colours::black); g.drawRoundedRectangle(lamp, 3.f, 1.f);
            g.setColour(ui::ink); g.setFont(ui::font(13.f));
            g.drawText(names[k], channelLabelBounds(k), juce::Justification::centred);
            auto value=readouts[size_t(k)].getBounds();
            g.setColour(juce::Colours::white); g.fillRect(value); g.setColour(ui::ink); g.drawRect(value);
            g.setFont(ui::font(15.f));
            g.drawText(held[size_t(k)]<=-99.f ? juce::String("-Inf") : juce::String(held[size_t(k)], 1), value, juce::Justification::centred);
        }
        g.setFont(ui::font(14.f)); g.setColour(ui::ink);
        for(int db=0;db>=-30;db-=6) {
            const float y=float(barTop+3)+float(barBottom-barTop-6)*float(-db)/30.f;
            g.drawText(juce::String(db), 917, int(y)-8, 40, 16, juce::Justification::centred);
            g.drawLine(905.f, y, 915.f, y, 1.2f); g.drawLine(959.f, y, 969.f, y, 1.2f);
        }
        for(int i=0;i<=60;++i) { const float y=float(barTop+3)+float(barBottom-barTop-6)*float(i)/60.f;
            g.drawLine(905.f, y, 910.f, y, .6f); g.drawLine(964.f, y, 969.f, y, .6f); }
        g.setColour(juce::Colours::white.withAlpha(.9f)); g.setFont(ui::font(24.f, true));
        g.drawText("Tsunami Stereo", logicalBounds().removeFromBottom(stripHeight).reduced(24, 0), juce::Justification::centredRight);
    }
    static juce::Rectangle<int> logicalBounds() { return {0, 0, 1048, 665}; }
    float scale() const { return float(getWidth())/1048.f; }
    juce::AffineTransform scaleTransform() const { return juce::AffineTransform::scale(scale()); }
    void resized() override { // logical 1048x665 body: panels 222 / centre / 222, then uniform scale
        for(auto* c : getChildren())
            if(c!=resizableCorner.get()) c->setTransform(scaleTransform());
        auto area=logicalBounds(); auto strip=area.removeFromBottom(stripHeight);
        leftPanel=area.removeFromLeft(222); rightPanel=area.removeFromRight(222); centrePanel=area;
        gain.setBounds(41, 60, 62, 379); width.setBounds(129, 60, 62, 379);
        inputMode.setBounds(leftPanel.getX()+36, 497, 150, 30); // original 17.1 static capture: shared selector row, centre y512
        invertLeft.setBounds(leftPanel.getX()+36, 556, 44, 28); swap.setBounds(leftPanel.getX()+86, 556, 50, 28);
        invertRight.setBounds(leftPanel.getX()+142, 556, 44, 28);
        display.setBounds(244, 32, 560, 321);
        linkStrip.setBounds(365, 474, 317, 22);
        asymmetry.setBounds(340, 414, 435, 40);
        rotation.setBounds(340, 499, 435, 40);
        meterArea=juce::Rectangle<int>(850, 50, 176, 395);
        for(int k=0;k<2;++k) {
            lamps[size_t(k)].setBounds(k ? 976 : 874, 56, 24, 12);
            readouts[size_t(k)].setBounds(k ? 957 : 855, 414, 62, 25);
        }
        meterMode.setBounds(rightPanel.getX()+36, 497, 150, 30);
        bypass.setBounds(strip.getX()+16, strip.getY()+14, 90, strip.getHeight()-28);
    }
private:
    // Message-thread polling only: no parameter listener runs on host/audio threads.
    void refreshPolarity() {
        invertLeft.setButtonText(shown[5]>=.5f ? "-" : "+"); invertRight.setButtonText(shown[6]>=.5f ? "-" : "+");
        inputMode.setButtonText(shown[7]>=.5f ? "M-S active" : "L-R active");
        meterMode.setButtonText(meterMode.getToggleState() ? "M-S active" : "L-R active");
    }
    void timerCallback() override {
        pollMeters();
        std::array<float,Processor::Count> now{};
        static constexpr const char* all[Processor::Count]{"gain","width","rotation","asymmetry","swap","invert_left","invert_right","input_mode","bypass"};
        for(size_t i=0;i<now.size();++i) now[i]=state.getRawParameterValue(all[i])->load(std::memory_order_relaxed);
        if(now!=shown) { shown=now; display.repaint(); refreshPolarity(); }
    }
    void vertical(juce::Slider& s, const char* id, double reset, int decimals) {
        configure(s, id, reset, decimals); s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 22);
    }
    void horizontal(juce::Slider& s, const char* id, double reset, int decimals) {
        configure(s, id, reset, decimals); s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    }
    void configure(juce::Slider& s, const char* id, double reset, int decimals) {
        s.setComponentID(id);
        s.setLookAndFeel(&faderLook);
        s.setColour(juce::Slider::textBoxTextColourId, ui::ink); s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxOutlineColourId, ui::ink); s.setColour(juce::Slider::trackColourId, ui::ink);
        s.setColour(juce::Slider::backgroundColourId, ui::ink); s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff8a8a86));
        addAndMakeVisible(s);
        sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, s));
        // juce::String(double, 0) prints full precision, so integers are rounded explicitly.
        // Display only: the parameter keeps its continuous physical value.
        s.textFromValueFunction=[decimals](double v) { return decimals>0 ? juce::String(v, decimals) : juce::String(juce::roundToInt(v)); };
        s.updateText();
        s.setDoubleClickReturnValue(true, reset);
    }
    void toggle(juce::Button& b, const char* id, const char* text) {
        b.setComponentID(id); b.setButtonText(text); b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6b5fb4));
        addAndMakeVisible(b);
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, b));
    }
    static constexpr int stripHeight=70;
    juce::AudioProcessorValueTreeState& state;
    MeterSource* meters;
    FaderLook faderLook; // declared before the sliders that reference it
    juce::Slider gain, width, asymmetry, rotation;
    juce::TextButton invertLeft, swap, invertRight, bypass;
    ModeSwitch inputMode, meterMode;
    VectorDisplay display;
    LinkStrip linkStrip;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    juce::Rectangle<int> leftPanel, centrePanel, rightPanel, meterArea;
    std::array<float,2> displayed{{-100.f,-100.f}};
    std::array<float,Processor::Count> shown{};
    std::array<float,2> held{{-100.f,-100.f}};
    std::array<bool,2> clip{};
    std::array<PeakTarget,2> lamps, readouts;
};
} // namespace tsunami::s1

namespace tsunami {
inline juce::AudioProcessorEditor* createPluginEditor(juce::AudioProcessor& processor,
                                                      juce::AudioProcessorValueTreeState& state) {
    return new s1::Editor(processor, state);
}
}
