#pragma once

// Look and feel shared by the bass plug-ins' editors: palette, rotary knob (bipolar knobs fill from the middle), the
// label + slider + attachment bundle every knob needs.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace bassui
{
const juce::Colour kBg (0xff101418), kPanel (0xff171d23), kPanel2 (0xff1d252d), kLine (0xff2c3640), kAccent (0xff4fd1c5),
    kWarm (0xfff6ad55), kText (0xffdde6ee), kDim (0xff7b8a97);

class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, kText);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, kPanel2);
        setColour (juce::ComboBox::textColourId, kText);
        setColour (juce::ComboBox::outlineColourId, kLine);
        setColour (juce::ComboBox::arrowColourId, kDim);
        setColour (juce::PopupMenu::backgroundColourId, kPanel2);
        setColour (juce::PopupMenu::textColourId, kText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.darker (0.5f));
        setColour (juce::TextButton::textColourOffId, kText);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float r = 0.5f * std::min (b.getWidth(), b.getHeight());
        const auto c = b.getCentre();
        const float arcR = r - 3.0f, angle = startAngle + pos * (endAngle - startAngle);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
        juce::Path track, value;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, from, angle, true);
        g.setColour (kLine);
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kAccent);
        g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ir = arcR - 7.0f;
        g.setColour (kPanel2);
        g.fillEllipse (c.x - ir, c.y - ir, 2 * ir, 2 * ir);
        g.setColour (kText);
        g.drawLine (c.x + ir * 0.35f * std::sin (angle), c.y - ir * 0.35f * std::cos (angle), c.x + ir * 0.9f * std::sin (angle),
                    c.y - ir * 0.9f * std::cos (angle), 2.0f);
    }
};

struct Knob
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& text)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        auto* prm = apvts.getParameter (id);
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        parent.addAndMakeVisible (slider);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, kDim);
        label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        parent.addAndMakeVisible (label);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
    }
};
} // namespace bassui
