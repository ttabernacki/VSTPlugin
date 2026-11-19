#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <functional>
#include <map>

// Controller setup: one step per control (pitch slider, mod slider, expression pedal, aftertouch,
// velocity). Each step listens to everything the keyboard sends and keeps the control moved furthest,
// with the range it really reaches.
class SetupWizard : public juce::Component
{
public:
    SetupWizard (ScrapeCelloProcessor&, std::function<void()> onClose);
    void begin();
    void tick() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    int currentStep() const { return step; }

private:
    ScrapeCelloProcessor& proc;
    std::function<void()> close;
    int step = 0;
    std::array<int, ScrapeCelloProcessor::kWizSteps> found {};
    juce::TextButton back { "Back" }, skip { "Skip" }, next { "Next" }, done { "Close" }, reset { "Reset to defaults" };
    juce::Rectangle<int> panel;
    void go (int s);
    std::vector<int> taken() const;
    juce::String sourceName (int src) const;
};

// The Play page: the bows, live (each string, where its bow is, and its pressure against the edge: light,
// clean, the gritty edge, the grind) and the knobs that shape it. Everything else is behind SETTINGS.
// Presets, favourites, A/B, audition and MIDI learn live in the header. Drawn at 1000 x 640, scaled to fit.
class ScrapeCelloEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kW = 1000, kH = 640;
    explicit ScrapeCelloEditor (ScrapeCelloProcessor&);
    ~ScrapeCelloEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (bassui::kBg); }
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;
    enum Page { PlayPage, SettingsPage };
    void showPage (int page);
    void showSetup (bool show);

private:
    struct Canvas : juce::Component
    {
        explicit Canvas (ScrapeCelloEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics& g) override { ed.paintCanvas (g); }
        void paintOverChildren (juce::Graphics& g) override { ed.paintOverCanvas (g); }
        ScrapeCelloEditor& ed;
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        juce::String paramId;
        void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& text);
        void show (bool v) { box.setVisible (v); label.setVisible (v); }
    };
    using Knob = bassui::Knob;
    static void show (Knob& k, bool v) { k.slider.setVisible (v); k.label.setVisible (v); }

    void paintCanvas (juce::Graphics&);
    void paintOverCanvas (juce::Graphics&);
    void drawBows (juce::Graphics&, juce::Rectangle<int>);
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

    bassui::KnobLookAndFeel laf;
    ScrapeCelloProcessor& proc;
    Canvas canvas { *this };

    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetButton, favButton, abA { "A" }, abB { "B" }, abCopy { "A>B" },
        auditionButton { "PLAY IT" }, learnButton { "MIDI LEARN" }, clearLearn { "Clear all" }, playTab { "PLAY" }, settingsTab { "SETTINGS" },
        setupButton { "CONTROLLER SETUP..." };
    juce::Rectangle<int> statusArea, bowArea, legendArea;

    // Play page
    Knob pressure, reach, position, speed, grit, brightness, body, vibDepth, glide, level;
    // Settings
    Knob vibRate, vibDelay, ring, velCurve;
    Choice mode, pedalCc, mpe, memberRange, masterRange;
    std::array<juce::Rectangle<int>, 4> setAreas;

    SetupWizard wizard;
    juce::TooltipWindow tooltips { this, 300 };
    bool learning = false;
    std::map<juce::Component*, juce::String> controlParam;
    int page = PlayPage;
    float phase = 0; // animates the strings
};
