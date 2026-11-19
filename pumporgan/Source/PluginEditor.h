#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>

// The look of an old American reed organ: walnut case, ivory stop tablets on a felt rail, Bakelite knobs
// with nickel skirts, a brass-bezelled wind gauge, the swell shutters, and the keyboard lit by what the reeds
// are doing.
namespace organui
{
const juce::Colour kWalnutDark (0xff23150c), kWalnut (0xff3e2515), kWalnutLight (0xff6a4126), kIvory (0xfff1e7cf), kIvoryAged (0xffe2d0a4),
    kBrass (0xffb48f55), kBrassLight (0xffe3c98f), kBrassDark (0xff6e5229), kFelt (0xff5b1b20), kInk (0xff2a1c12), kRedInk (0xff8e2220),
    kGlow (0xffffb44a), kStruggle (0xffe2513a), kSage (0xff9cc58c), kLilac (0xffc9b5e6), kEbony (0xff17110d);

juce::Font serif (float size, bool bold = false, bool italic = false);

class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return serif (14.0f, false, true); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return serif (15.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return serif (std::min (14.0f, h * 0.55f), true); }
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

// An ivory stop tablet on the rail: the stop's name and footage engraved, red ink for the reeds and the
// mechanical stops, black for the rest. Drawn up (on) or pushed back (off).
class StopTab : public juce::Button
{
public:
    StopTab (const juce::String& name, const juce::String& footage, bool redInk);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::String footage;
    bool red;
};
} // namespace organui

// Controller setup: one step per control (pitch slider, mod slider, expression pedal, aftertouch,
// velocity). Each step listens to everything the keyboard sends and keeps the control moved furthest,
// with the range it really reaches.
class SetupWizard : public juce::Component
{
public:
    SetupWizard (PumpOrganProcessor&, std::function<void()> onClose);
    void begin();
    void tick() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    int currentStep() const { return step; }

private:
    PumpOrganProcessor& proc;
    std::function<void()> close;
    int step = 0;
    std::array<int, PumpOrganProcessor::kWizSteps> found {};
    juce::TextButton back { "Back" }, skip { "Skip" }, next { "Next" }, done { "Close" }, reset { "Reset to defaults" };
    juce::Rectangle<int> panel;
    void go (int s);
    std::vector<int> taken() const;
    juce::String sourceName (int src) const;
};

// The front: the stop rail, the wind gauge and blower, the knobs, the swell shutters and the keyboard,
// live. Behind the panel: how it's pumped and played (MPE, the pedal, velocity), its character, controller
// setup. Presets, favourites, A/B, audition and MIDI learn live in the header. Drawn at 1100 x 720, scaled.
class PumpOrganEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kW = 1100, kH = 720;
    explicit PumpOrganEditor (PumpOrganProcessor&);
    ~PumpOrganEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (organui::kWalnutDark); }
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;
    enum Page { FrontPage, BackPage };
    void showPage (int page);
    void showSetup (bool show);

private:
    struct Canvas : juce::Component
    {
        explicit Canvas (PumpOrganEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics& g) override { ed.paintCanvas (g); }
        void paintOverChildren (juce::Graphics& g) override { ed.paintOverCanvas (g); }
        PumpOrganEditor& ed;
    };
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String paramId;
        void setup (juce::Component& parent, juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& text, const juce::String& tip);
        void show (bool v) { slider.setVisible (v); label.setVisible (v); }
        void place (juce::Rectangle<int> cell);
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        juce::String paramId;
        void setup (juce::Component& parent, juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& text, const juce::String& tip);
        void show (bool v) { box.setVisible (v); label.setVisible (v); }
        void place (juce::Rectangle<int> cell);
    };
    struct Tab
    {
        std::unique_ptr<organui::StopTab> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        juce::String paramId;
    };

    void paintCanvas (juce::Graphics&);
    void paintOverCanvas (juce::Graphics&);
    void paintFront (juce::Graphics&);
    void paintBack (juce::Graphics&);
    void drawGauge (juce::Graphics&, juce::Rectangle<float>);
    void drawBlower (juce::Graphics&, juce::Rectangle<float>);
    void drawTube (juce::Graphics&, juce::Rectangle<float>);
    void drawShutters (juce::Graphics&, juce::Rectangle<float>);
    void drawKeyboard (juce::Graphics&, juce::Rectangle<float>);
    void drawPlate (juce::Graphics&, juce::Rectangle<float>, const juce::String& title);
    void makeWood();
    void layout();
    void timerCallback() override;
    void updateHeader();
    void presetMenu();
    void stepPreset (int dir);
    void savePresetDialog();
    juce::StringArray allPresets() const;
    void registerControl (juce::Component& c, const juce::String& id)
    {
        controlParam[&c] = id;
        c.addMouseListener (this, false);
    }

    organui::VintageLookAndFeel laf;
    PumpOrganProcessor& proc;
    Canvas canvas { *this };
    juce::Image wood, backWood;

    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, favButton, abA { "A" }, abB { "B" }, abCopy { "A>B" }, auditionButton { "Play it" },
        learnButton { "MIDI learn" }, clearLearn { "Clear all" }, pageButton { "Behind the panel" }, setupButton { "Controller setup..." };
    struct ClickTarget : juce::Component, juce::SettableTooltipClient {};
    ClickTarget presetPlate; // a click target over the nameplate (drawn on the canvas)
    juce::Rectangle<int> plateArea, statusArea, railArea, gaugeArea, blowerArea, tubeArea, knobArea, shutterArea, sideArea, keysArea, holdLamp, pumpLamp;

    // the front
    std::vector<Tab> tabs;
    Knob wind, swell, chiff, level, pumpDepth, cutout, warble, neglect;
    Choice model, mic, pumpRate;
    // behind the panel
    Knob age, air, vibDepth, vibRate, velCurve;
    Choice windMode, pedalCc, mpe, memberRange, masterRange, pressure, slide;
    std::array<juce::Rectangle<int>, 4> backAreas;

    SetupWizard wizard;
    juce::TooltipWindow tooltips { this, 400 };
    bool learning = false;
    std::map<juce::Component*, juce::String> controlParam;
    int page = FrontPage;
    float phase = 0, fanAngle = 0, needle = -1;
    float gaugeTarget() const;
    juce::String shownPreset;
};
