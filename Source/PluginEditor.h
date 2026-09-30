#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Top-down view of the listener: drag the dot to set azimuth (angle) and distance (radius).
class RadarPad : public juce::Component, private juce::Timer
{
public:
    explicit RadarPad (OrbitPanAudioProcessor&);
    ~RadarPad() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    void applyMouse (juce::Point<float>);

    static constexpr float kInner = 0.12f; // radius fraction at distance = 0

    OrbitPanAudioProcessor& proc;
    juce::RangedAudioParameter* azParam;
    juce::RangedAudioParameter* distParam;
};

// Side view: shows and sets elevation.
class ElevationPad : public juce::Component, private juce::Timer
{
public:
    explicit ElevationPad (OrbitPanAudioProcessor&);
    ~ElevationPad() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    void applyMouse (juce::Point<float>);

    OrbitPanAudioProcessor& proc;
    juce::RangedAudioParameter* elParam;
};

class OrbitPanEditor : public juce::AudioProcessorEditor
{
public:
    explicit OrbitPanEditor (OrbitPanAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& text);

    OrbitPanAudioProcessor& proc;
    RadarPad radar;
    ElevationPad elevation;
    Knob azimuth, elev, distance, focus, room, decay, orbit;
    juce::Label title;
};
