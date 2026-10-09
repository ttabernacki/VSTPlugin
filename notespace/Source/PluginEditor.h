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
    void timerCallback() override;
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
    // what is on screen: the last note held (and dimmed) for a while, so fast playing does not make it flash
    float shownPitch = 0.0f, shownIn[NoteSpaceProcessor::kH] {}, shownOut[NoteSpaceProcessor::kH] {};
    double lastLiveMs = -1.0e9;
    juce::Rectangle<int> profileArea, meterArea;
};
