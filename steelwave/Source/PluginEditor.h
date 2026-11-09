#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>

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
    enum Page { PlayPage, ControlsPage, AdvancedPage };
    void showPage (int page);

private:
    void timerCallback() override
    {
        if (page == PlayPage)
            repaint (neckArea);
        else if (page == ControlsPage)
            repaint (pedalStrip);
        if (learning)
            repaint (); // CC labels appear as they're learned
        if (presetBox.getSelectedItemIndex() != proc.getCurrentProgram())
            presetBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    }
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
    };

    bassui::KnobLookAndFeel laf;
    SteelwaveProcessor& proc;
    Choice set, key, pickup, volcc, voltype;
    bassui::Knob glide, strum, pick, bright, sustain, noise, behind, vibRate, vibDepth, sym, level;
    juce::Rectangle<int> neckArea;

    // Advanced page: the model's calibration, grouped
    juce::TextButton playTab { "PLAY" }, ctlTab { "CONTROLS" }, advTab { "ADVANCED" }, resetAdv { "Reset to calibrated" };
    std::vector<std::unique_ptr<bassui::Knob>> advKnobs; // in swadv::params() order
    std::array<juce::Rectangle<int>, swadv::kGroups> groupAreas;
    juce::TooltipWindow tooltips { this, 300 };

    // Controls page: how it's played
    Choice pedalKeys, modWheel, aftertouch, timing, mpe, bendRange;
    bassui::Knob pedalSpeed, swellBelow, swellTime, autoVib, vibDelay, slideIn, slideFrom, slideTime, glideFeel, barRange;
    std::array<juce::Rectangle<int>, 5> ctlAreas;

    // header: presets and MIDI learn
    juce::ComboBox presetBox;
    juce::TextButton learnButton { "MIDI LEARN" }, clearLearn { "Clear all" };
    bool learning = false;
    std::map<juce::Component*, juce::String> controlParam; // every knob and menu -> its parameter ID
    void registerControl (juce::Component& c, const juce::String& id)
    {
        controlParam[&c] = id;
        c.addMouseListener (this, false);
    }
    juce::Rectangle<int> pedalStrip;
    void drawPedals (juce::Graphics&, juce::Rectangle<int>) const;

    int page = PlayPage;
};
