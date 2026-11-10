#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>

// One simple page to play from (mode, key, eight knobs, the neck), with everything else behind SETTINGS:
// Sound, Playing and Model. Presets and MIDI learn live in the header.
class SteelwaveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SteelwaveEditor (SteelwaveProcessor&);
    ~SteelwaveEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override; // MIDI learn: which control has which CC
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;
    void parentHierarchyChanged() override { useSoftwareRendering(); }
    void visibilityChanged() override { useSoftwareRendering(); }
    enum Page { PlayPage, SoundPage, ControlsPage, AdvancedPage };
    void showPage (int page);

private:
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

    // header
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetButton, learnButton { "MIDI LEARN" }, clearLearn { "Clear all" };
    juce::TextButton playTab { "PLAY" }, settingsTab { "SETTINGS" };
    juce::TextButton soundTab { "SOUND" }, ctlTab { "PLAYING" }, advTab { "MODEL" }, resetAdv { "Reset to calibrated" };
    void presetMenu();
    void stepPreset (int dir);
    void savePresetDialog();
    juce::StringArray allPresets() const; // factory, then the user's

    // Play page
    Choice set, key;
    Knob tone, pick, sustain, vibDepth, glide, swellMain, sweeten, level;
    juce::Rectangle<int> neckArea;

    // Settings > Sound
    Choice pickup, volcc, voltype;
    Knob strum, noise, behind, vibRate, sym;
    std::array<juce::Rectangle<int>, 2> soundAreas;

    // Settings > Playing
    Choice pedalKeys, modWheel, aftertouch, timing, mpe, bendRange, autoBend, legatoSlide, wheelMode;
    Knob pedalSpeed, swellBelow, swellTime, autoVib, vibDelay, slideIn, slideFrom, slideTime, glideFeel, barRange;
    std::array<juce::Rectangle<int>, 5> ctlAreas;
    juce::Rectangle<int> pedalStrip;
    void drawPedals (juce::Graphics&, juce::Rectangle<int>) const;

    // Settings > Model: the physical model's calibration, grouped
    std::vector<std::unique_ptr<Knob>> advKnobs; // in swadv::params() order
    std::array<juce::Rectangle<int>, swadv::kGroups> groupAreas;

    juce::TooltipWindow tooltips { this, 300 };
    bool learning = false;
    std::map<juce::Component*, juce::String> controlParam; // every knob and menu -> its parameter ID
    void registerControl (juce::Component& c, const juce::String& id)
    {
        controlParam[&c] = id;
        c.addMouseListener (this, false);
    }
    int page = PlayPage;
};
