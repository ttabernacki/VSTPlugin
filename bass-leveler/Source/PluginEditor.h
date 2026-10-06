#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class BnlLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BnlLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
};

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

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void addKnob (Knob&, const juce::String& id, const juce::String& text);

    BnlLookAndFeel laf;
    BassLevelerProcessor& proc;
    juce::TextButton learn { "LEARN" }, focus { "+ 2nd harmonic" }, clear { "Clear table" };
    juce::ComboBox mode;
    Knob strength, rider, boost, cut, speed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> learnAtt, focusAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    juce::Rectangle<int> chart;
};
