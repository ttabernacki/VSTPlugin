#pragma once

#include "PluginProcessor.h"

#include "../../bass-common/BassUi.h"

#include <juce_audio_utils/juce_audio_utils.h>

class NoteSpaceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NoteSpaceEditor (NoteSpaceProcessor&);
    ~NoteSpaceEditor() override;
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
    NoteSpaceProcessor& proc;
    Knob contrast, tone, fund, repair, translate, range, punch, sustain, kick;
    juce::Rectangle<int> profileArea, meterArea;
};
