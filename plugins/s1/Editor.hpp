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
    void mouseDown(const juce::MouseEvent&) override {
        for(auto* id : ids) state.getParameter(id)->beginChangeGesture();
        start=image(); dragging=true;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!dragging) return;
        const auto next=applyVectorDrag(start, float(e.getDistanceFromDragStartX()), float(e.getDistanceFromDragStartY()), e.mods.isAltDown());
        for(int i=0;i<4;++i) { auto* p=state.getParameter(ids[i]); p->setValueNotifyingHost(p->convertTo0to1(next[size_t(i)])); }
    }
    void mouseUp(const juce::MouseEvent&) override {
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

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    Editor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
        : AudioProcessorEditor(p), state(s), meters(dynamic_cast<MeterSource*>(&p)), display(s) {
        vertical(gain, "gain", 0.0, 1); vertical(width, "width", 1.0, 2);
        horizontal(asymmetry, "asymmetry", 0.0, 0); horizontal(rotation, "rotation", 0.0, 1);
        toggle(inputMode, "input_mode", "L-R | M-S"); toggle(invertLeft, "invert_left", "+/-");
        toggle(swap, "swap", "<->"); toggle(invertRight, "invert_right", "+/-"); toggle(bypass, "bypass", "Bypass");
        meterMode.setComponentID("meter_mode"); meterMode.setButtonText("L-R | M-S"); meterMode.setClickingTogglesState(true);
        meterMode.onClick=[this] { displayed={-100.f,-100.f}; refreshPolarity(); repaint(); }; // display-only, never audio
        addAndMakeVisible(meterMode);
        display.onReset=[this] { resetImage(); };
        addAndMakeVisible(display);
        setWantsKeyboardFocus(true);
        refreshPolarity();
        setSize(780, 470);
        startTimerHz(30);
    }
    ~Editor() override {
        stopTimer();
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
        }
        repaint(meterArea);
    }
    std::array<float,2> meterReadings() const { return displayed; }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key.getTextCharacter()=='r' || key.getTextCharacter()=='R') { resetImage(); return true; }
        return false;
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(ui::strip);
        for(auto r : {leftPanel, centrePanel, rightPanel}) {
            g.setGradientFill(juce::ColourGradient(ui::panel.brighter(.15f), 0.f, float(r.getY()), ui::panel.darker(.08f), 0.f, float(r.getBottom()), false));
            g.fillRect(r); g.setColour(ui::panelEdge); g.drawRect(r, 1);
        }
        g.setColour(ui::ink); g.setFont(ui::font(16.f));
        g.drawText("Gain", gain.getX()-10, 10, gain.getWidth()+20, 20, juce::Justification::centred);
        g.drawText("Width", width.getX()-10, 10, width.getWidth()+20, 20, juce::Justification::centred);
        g.drawText("Input Mode", leftPanel.getX(), inputMode.getY()-24, leftPanel.getWidth(), 20, juce::Justification::centred);
        g.drawText("Asymmetry", centrePanel.getX()+12, asymmetry.getY(), 110, asymmetry.getHeight(), juce::Justification::centredLeft);
        g.drawText("Rotation", centrePanel.getX()+12, rotation.getY(), 110, rotation.getHeight(), juce::Justification::centredLeft);
        g.drawText("Output", rightPanel.getX(), 10, rightPanel.getWidth(), 20, juce::Justification::centred);
        g.drawText("Meter Mode", rightPanel.getX(), meterMode.getY()-24, rightPanel.getWidth(), 20, juce::Justification::centred);
        const bool ms=meterMode.getToggleState();
        const char* names[2]{ms ? "M" : "L", ms ? "S" : "R"};
        for(int k=0;k<2;++k) {
            auto bar=meterArea.withWidth(26).translated(k*58+10, 0).withTrimmedBottom(40);
            g.setColour(juce::Colours::black); g.fillRoundedRectangle(bar.toFloat(), 3.f);
            const float level=juce::jlimit(0.f, 1.f, (displayed[size_t(k)]+30.f)/30.f);
            auto lit=bar.reduced(3).toFloat(); lit=lit.withTrimmedTop(lit.getHeight()*(1.f-level));
            g.setColour(ui::meter); g.fillRect(lit);
            g.setColour(ui::ink); g.setFont(ui::font(13.f));
            g.drawText(names[k], bar.getX(), bar.getY()-18, bar.getWidth(), 16, juce::Justification::centred);
            auto value=juce::Rectangle<int>(bar.getX()-12, bar.getBottom()+8, 50, 22);
            g.setColour(juce::Colours::white); g.fillRect(value); g.setColour(ui::ink); g.drawRect(value);
            g.drawText(displayed[size_t(k)]<=-99.f ? juce::String("-inf") : juce::String(displayed[size_t(k)], 1), value, juce::Justification::centred);
        }
        g.setFont(ui::font(12.f));
        for(int db=0;db>=-30;db-=6) {
            const int y=meterArea.getY()+3+int((meterArea.getHeight()-46)*float(-db)/30.f);
            g.drawText(juce::String(db), meterArea.getX()+36, y-7, 30, 14, juce::Justification::centred);
        }
        g.setColour(juce::Colours::white.withAlpha(.9f)); g.setFont(ui::font(24.f, true));
        g.drawText("Tsunami Stereo", getLocalBounds().removeFromBottom(stripHeight).reduced(24, 0), juce::Justification::centredRight);
    }
    void resized() override {
        auto area=getLocalBounds(); auto strip=area.removeFromBottom(stripHeight);
        leftPanel=area.removeFromLeft(170); rightPanel=area.removeFromRight(170); centrePanel=area;
        gain.setBounds(leftPanel.getX()+20, 34, 60, 300); width.setBounds(leftPanel.getX()+92, 34, 60, 300);
        inputMode.setBounds(leftPanel.getX()+25, 362, 120, 26);
        invertLeft.setBounds(leftPanel.getX()+20, 394, 40, 24); swap.setBounds(leftPanel.getX()+62, 394, 46, 24);
        invertRight.setBounds(leftPanel.getX()+110, 394, 40, 24);
        display.setBounds(centrePanel.getX()+16, 14, centrePanel.getWidth()-32, 286);
        asymmetry.setBounds(centrePanel.getX()+110, 318, centrePanel.getWidth()-130, 40);
        rotation.setBounds(centrePanel.getX()+110, 368, centrePanel.getWidth()-130, 40);
        meterArea=juce::Rectangle<int>(rightPanel.getX()+30, 50, 120, 300);
        meterMode.setBounds(rightPanel.getX()+25, 394, 120, 24);
        bypass.setBounds(strip.getX()+16, strip.getY()+10, 80, strip.getHeight()-20);
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
        s.setColour(juce::Slider::textBoxTextColourId, ui::ink); s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxOutlineColourId, ui::ink); s.setColour(juce::Slider::trackColourId, ui::ink);
        s.setColour(juce::Slider::backgroundColourId, ui::ink); s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff8a8a86));
        addAndMakeVisible(s);
        sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, s));
        s.textFromValueFunction=[decimals](double v) { return juce::String(v, decimals); };
        s.updateText();
        s.setDoubleClickReturnValue(true, reset);
    }
    void toggle(juce::TextButton& b, const char* id, const char* text) {
        b.setComponentID(id); b.setButtonText(text); b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6b5fb4));
        addAndMakeVisible(b);
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, b));
    }
    static constexpr int stripHeight=46;
    juce::AudioProcessorValueTreeState& state;
    MeterSource* meters;
    juce::Slider gain, width, asymmetry, rotation;
    juce::TextButton inputMode, invertLeft, swap, invertRight, bypass, meterMode;
    VectorDisplay display;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    juce::Rectangle<int> leftPanel, centrePanel, rightPanel, meterArea;
    std::array<float,2> displayed{{-100.f,-100.f}};
    std::array<float,Processor::Count> shown{};
};
} // namespace tsunami::s1

namespace tsunami {
inline juce::AudioProcessorEditor* createPluginEditor(juce::AudioProcessor& processor,
                                                      juce::AudioProcessorValueTreeState& state) {
    return new s1::Editor(processor, state);
}
}
