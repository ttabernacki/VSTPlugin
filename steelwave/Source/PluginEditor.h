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

private:
    void timerCallback() override { repaint (neckArea); }
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
};
