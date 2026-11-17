#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <map>

// Controller setup: one step per control (pitch slider, mod slider, expression pedal, aftertouch,
// velocity). Each step listens to everything the keyboard sends and keeps the control moved furthest,
// with the range it really reaches.
class SetupWizard : public juce::Component
{
public:
    SetupWizard (SteelwaveProcessor&, std::function<void()> onClose);
    void begin();
    void tick() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    int currentStep() const { return step; }

private:
    SteelwaveProcessor& proc;
    std::function<void()> close;
    int step = 0; // 0 .. kWizSteps (the last: a summary)
    std::array<int, SteelwaveProcessor::kWizSteps> found {};
    juce::TextButton back { "Back" }, skip { "Skip" }, next { "Next" }, done { "Close" }, reset { "Reset to defaults" };
    juce::Rectangle<int> panel;
    void go (int s);
    std::vector<int> taken() const;
    juce::String sourceName (int src) const;
};

// One simple page to play from (mode, key, pattern, nine knobs, the neck and the keyboard's chord map),
// with everything else behind SETTINGS: Sound, Playing and Model. Presets, A/B, favourites, audition and
// MIDI learn live in the header. The whole layout is drawn at 1000 x 660 and scaled to the window.
class SteelwaveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kW = 1000, kH = 660;
    explicit SteelwaveEditor (SteelwaveProcessor&);
    ~SteelwaveEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (bassui::kBg); }
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;
    void parentHierarchyChanged() override { useSoftwareRendering(); }
    void visibilityChanged() override { useSoftwareRendering(); }
    enum Page { PlayPage, SoundPage, ControlsPage, AdvancedPage };
    void showPage (int page);
    void showSetup (bool show);
    bool setupShowing() const { return wizard.isVisible(); }

private:
    struct Canvas : juce::Component
    {
        explicit Canvas (SteelwaveEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics& g) override { ed.paintCanvas (g); }
        void paintOverChildren (juce::Graphics& g) override { ed.paintOverCanvas (g); }
        SteelwaveEditor& ed;
    };
    void paintCanvas (juce::Graphics&);
    void paintOverCanvas (juce::Graphics&); // MIDI learn: which control has which CC
    void layout();
    void timerCallback() override;
    void useSoftwareRendering()
    {
        if (auto* peer = getPeer())
            if (peer->getCurrentRenderingEngine() != 0)
                peer->setCurrentRenderingEngine (0);
    }

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

    bassui::KnobLookAndFeel laf;
    SteelwaveProcessor& proc;
    Canvas canvas { *this };

    // header
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetButton, favButton, abA { "A" }, abB { "B" }, abCopy { "A>B" },
        auditionButton { "PLAY IT" }, learnButton { "MIDI LEARN" }, clearLearn { "Clear all" };
    juce::TextButton playTab { "PLAY" }, settingsTab { "SETTINGS" };
    juce::TextButton soundTab { "SOUND" }, ctlTab { "PLAYING" }, advTab { "MODEL" }, resetAdv { "Reset to calibrated" }, setupButton { "CONTROLLER SETUP..." };
    juce::Rectangle<int> statusArea;
    void presetMenu();
    void stepPreset (int dir);
    void savePresetDialog();
    juce::StringArray allPresets() const; // factory, then the user's
    void updateHeader();

    // Play page
    Choice mode, voicing, scale, key, pattern, patRate;
    Knob tone, pick, sustain, vibDepth, glide, melody, swellMain, sweeten, level;
    juce::Rectangle<int> neckArea, mapArea, modeHint;
    void drawNeck (juce::Graphics&);
    void drawChordMap (juce::Graphics&, juce::Rectangle<int>);

    // Settings > Sound
    Choice pickup, volcc, voltype;
    Knob strum, noise, behind, vibRate, sym;
    std::array<juce::Rectangle<int>, 2> soundAreas;

    // Settings > Playing
    Choice pedalKeys, modWheel, aftertouch, timing, mpe, bendRange, autoBend, legatoSlide, wheelMode, chordMove, tempSync;
    Knob pedalSpeed, swellBelow, swellTime, autoVib, vibDelay, slideIn, slideFrom, slideTime, glideFeel, barRange, velCurve;
    std::array<juce::Rectangle<int>, 6> ctlAreas;
    juce::Rectangle<int> pedalStrip;
    void drawPedals (juce::Graphics&, juce::Rectangle<int>) const;

    // Settings > Model: the physical model's calibration, grouped
    std::vector<std::unique_ptr<Knob>> advKnobs; // in swadv::params() order
    std::array<juce::Rectangle<int>, swadv::kGroups> groupAreas;

    SetupWizard wizard;

    juce::TooltipWindow tooltips { this, 300 };
    bool learning = false;
    std::map<juce::Component*, juce::String> controlParam; // every knob and menu -> its parameter ID
    void registerControl (juce::Component& c, const juce::String& id)
    {
        controlParam[&c] = id;
        c.addMouseListener (this, false);
    }
    int page = PlayPage, lastMode = -1;
};
