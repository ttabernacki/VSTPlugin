#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <juce_audio_utils/juce_audio_utils.h>

class BassLevelerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BassLevelerEditor (BassLevelerProcessor&);
    ~BassLevelerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override { useSoftwareRendering(); }
    void visibilityChanged() override { useSoftwareRendering(); }

private:
    void timerCallback() override { repaint(); }
    void useSoftwareRendering()
    {
        if (auto* peer = getPeer())
            if (peer->getCurrentRenderingEngine() != 0)
                peer->setCurrentRenderingEngine (0);
    }

    using Knob = bassui::Knob;

    bassui::KnobLookAndFeel laf;
    BassLevelerProcessor& proc;
    Knob amount, boost, cut, speed;
    juce::Rectangle<int> chart;
};
