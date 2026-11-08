#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <juce_audio_utils/juce_audio_utils.h>

class SteelwaveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SteelwaveEditor (SteelwaveProcessor&);
    ~SteelwaveEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override { useSoftwareRendering(); }
    void visibilityChanged() override { useSoftwareRendering(); }
    void showPage (bool adv); // false = PLAY, true = ADVANCED

private:
    void timerCallback() override
    {
        if (! advanced)
            repaint (neckArea);
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
        void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& text);
    };

    bassui::KnobLookAndFeel laf;
    SteelwaveProcessor& proc;
    Choice set, key, pickup, volcc, voltype;
    bassui::Knob glide, strum, pick, bright, sustain, noise, behind, vibRate, vibDepth, sym, level;
    juce::Rectangle<int> neckArea;

    // Advanced page: the model's calibration, grouped
    juce::TextButton playTab { "PLAY" }, advTab { "ADVANCED" }, resetAdv { "Reset to calibrated" };
    std::vector<std::unique_ptr<bassui::Knob>> advKnobs; // in swadv::params() order
    std::array<juce::Rectangle<int>, swadv::kGroups> groupAreas;
    juce::TooltipWindow tooltips { this, 300 };
    bool advanced = false;
};
