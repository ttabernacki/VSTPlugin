#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class LedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LedLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

class LowEndDefinitionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LowEndDefinitionEditor (LowEndDefinitionProcessor&);
    ~LowEndDefinitionEditor() override;
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

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void addKnob (Knob&, const juce::String& id, const juce::String& text);

    LedLookAndFeel laf;
    LowEndDefinitionProcessor& proc;
    Knob contrast, punch, sustain, range;
    juce::Rectangle<int> meterArea, chartArea;
};
