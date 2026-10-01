#pragma once

#include "PluginProcessor.h"

#include <deque>
#include <juce_audio_utils/juce_audio_utils.h>

// Shared look: dark panel, teal accent, arcs for knobs, bipolar fill for the depth slider.
class OrbitLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OrbitLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

// Top-down view of the listener. The dot is the sound source: drag it anywhere in the plane.
//   angle from "up" = azimuth (left/right/front/back), distance from the head = Distance.
// Double-click resets, mouse wheel changes distance.
class RadarPad : public juce::Component, private juce::Timer
{
public:
    explicit RadarPad (OrbitPanAudioProcessor&);
    ~RadarPad() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override { repaint(); }
    void applyMouse (juce::Point<float>);
    juce::Point<float> toPoint (juce::Point<float> centre, float radius, float azDeg, float d01) const;

    static constexpr float kInner = 0.12f; // radius fraction at distance = 0

    OrbitPanAudioProcessor& proc;
    juce::RangedAudioParameter *azParam, *distParam, *depthParam;
    std::deque<juce::Point<float>> trail;
};

// Side view: shows and sets elevation (up/down).
class ElevationPad : public juce::Component, private juce::Timer
{
public:
    explicit ElevationPad (OrbitPanAudioProcessor&);
    ~ElevationPad() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override { repaint(); }
    void applyMouse (juce::Point<float>);
    float yOf (float elDeg, juce::Rectangle<float> track) const;

    OrbitPanAudioProcessor& proc;
    juce::RangedAudioParameter* elParam;
};

class OrbitPanEditor : public juce::AudioProcessorEditor
{
public:
    explicit OrbitPanEditor (OrbitPanAudioProcessor&);
    ~OrbitPanEditor() override;
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

    OrbitLookAndFeel laf;
    OrbitPanAudioProcessor& proc;
    RadarPad radar;
    ElevationPad elevation;
    juce::Slider depthSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    Knob focus, rear, room, decay, orbit;

    // section header rectangles, filled in resized() and drawn in paint()
    juce::Rectangle<int> depthArea, tuningArea, spaceArea, motionArea, padArea, elevArea;
};
