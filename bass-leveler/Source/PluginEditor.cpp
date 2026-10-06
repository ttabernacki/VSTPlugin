#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff101418), kPanel (0xff171d23), kPanel2 (0xff1d252d), kLine (0xff2c3640), kAccent (0xff4fd1c5),
    kWarm (0xfff6ad55), kRec (0xffe5484d), kText (0xffdde6ee), kDim (0xff7b8a97);

juce::String noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

BnlLookAndFeel::BnlLookAndFeel()
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

void BnlLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                                       juce::Slider&)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = 0.5f * std::min (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float arcR = r - 3.0f, angle = startAngle + pos * (endAngle - startAngle);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
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

void BnlLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour fill = on ? (b.getButtonText() == "LEARN" ? kRec : kAccent.darker (0.4f)) : kPanel2;
    if (highlighted)
        fill = fill.brighter (0.1f);
    if (down)
        fill = fill.brighter (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? fill.brighter (0.3f) : kLine);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void BassLevelerEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    auto* prm = proc.apvts.getParameter (id);
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kDim);
    k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

BassLevelerEditor::BassLevelerEditor (BassLevelerProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    learn.setClickingTogglesState (true);
    focus.setClickingTogglesState (true);
    addAndMakeVisible (learn);
    addAndMakeVisible (focus);
    addAndMakeVisible (clear);
    mode.addItemList ({ "Balance", "Level" }, 1);
    addAndMakeVisible (mode);
    learnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "learn", learn);
    focusAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "focus", focus);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", mode);
    clear.onClick = [this] { proc.clearTable(); };
    clear.setTooltip ("Forget everything that was learned");
    addKnob (strength, "strength", "STRENGTH");
    addKnob (rider, "rider", "RIDER");
    addKnob (boost, "boost", "MAX BOOST");
    addKnob (cut, "cut", "MAX CUT");
    addKnob (speed, "speed", "SPEED");
    strength.slider.setNumDecimalPlacesToDisplay (2);
    rider.slider.setNumDecimalPlacesToDisplay (2);
    boost.slider.setNumDecimalPlacesToDisplay (1);
    boost.slider.setTextValueSuffix (" dB");
    cut.slider.setNumDecimalPlacesToDisplay (1);
    cut.slider.setTextValueSuffix (" dB");
    speed.slider.setNumDecimalPlacesToDisplay (0);
    speed.slider.setTextValueSuffix (" ms");
    setResizable (true, true);
    setResizeLimits (560, 380, 1100, 700);
    getConstrainer()->setFixedAspectRatio (680.0 / 440.0);
    setSize (680, 440);
    startTimerHz (15);
}

BassLevelerEditor::~BassLevelerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BassLevelerEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto head = r.removeFromTop (34);
    head.removeFromLeft (176); // title
    learn.setBounds (head.removeFromLeft (84));
    head.removeFromLeft (8);
    mode.setBounds (head.removeFromLeft (104));
    head.removeFromLeft (8);
    focus.setBounds (head.removeFromLeft (118));
    clear.setBounds (head.removeFromRight (100));
    r.removeFromTop (8);
    auto knobs = r.removeFromTop (118);
    const int w = knobs.getWidth() / 5;
    for (auto* k : { &strength, &rider, &boost, &cut, &speed })
    {
        auto cell = knobs.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
    r.removeFromTop (6);
    chart = r;
}

void BassLevelerEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("BASS NOTE LEVELER", 14, 14, 170, 26, juce::Justification::centredLeft);

    BassLevelerProcessor::View v;
    proc.getView (v);
    g.setColour (kPanel);
    g.fillRoundedRectangle (chart.toFloat(), 8.0f);
    auto area = chart.reduced (12, 10);
    auto footer = area.removeFromBottom (18);
    auto labels = area.removeFromBottom (14);

    int lo = 128, hi = -1;
    for (int p = 0; p < 128; ++p)
        if (v.count[p] > 0)
        {
            lo = std::min (lo, p);
            hi = std::max (hi, p);
        }
    if (hi < lo)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Turn LEARN on and play the whole bass part once, then turn it off.\n"
                          "The plugin learns how loud each note comes out and evens them toward the middle.",
                          area, juce::Justification::centred, 3);
    }
    else
    {
        lo = std::max (0, std::min (lo, 28) - 1);
        hi = std::min (127, std::max (hi, 43) + 1);
        float mn = 1e9f, mx = -1e9f;
        for (int p = lo; p <= hi; ++p)
            if (v.count[p] > 0)
            {
                mn = std::min (mn, v.measured[p]);
                mx = std::max (mx, v.measured[p]);
            }
        mn = std::min (mn, v.target) - 3.0f;
        mx = std::max (mx, v.target) + 3.0f;
        const float bw = (float) area.getWidth() / (float) (hi - lo + 1);
        auto Y = [&] (float db) { return (float) area.getBottom() - (db - mn) / (mx - mn) * (float) area.getHeight(); };
        g.setFont (juce::FontOptions (10.0f));
        for (int p = lo; p <= hi; ++p)
        {
            const float x = (float) area.getX() + (float) (p - lo) * bw;
            if (v.count[p] > 0)
            {
                const float y = Y (v.measured[p]);
                g.setColour (kAccent.withAlpha (juce::jlimit (0.35f, 1.0f, 0.3f + 0.12f * (float) v.count[p])));
                g.fillRoundedRectangle (x + 1.5f, y, std::max (1.0f, bw - 3.0f), (float) area.getBottom() - y, 2.0f);
            }
            if (p % 12 == 4 || p % 12 == 9 || p % 12 == 2 || p % 12 == 7 || bw > 22.0f)
            {
                g.setColour (kDim);
                g.drawText (noteName (p), (int) x - 6, labels.getY(), (int) bw + 12, labels.getHeight(), juce::Justification::centred);
            }
        }
        // target level (what every note is moved toward)
        g.setColour (kWarm);
        const float ty = Y (v.target);
        for (float x = (float) area.getX(); x < (float) area.getRight(); x += 8.0f)
            g.drawLine (x, ty, std::min (x + 4.0f, (float) area.getRight()), ty, 1.5f);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("target", area.getRight() - 44, (int) ty - 13, 44, 12, juce::Justification::right);
    }

    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    const double latMs = proc.latencySeconds.load() * 1000.0;
    g.drawText (juce::String (v.total) + " notes learned on " + juce::String (v.observedPitches) + " pitches    |    now "
                    + (proc.pitchHz.load() > 20.0f ? juce::String (proc.pitchHz.load(), 1) + " Hz" : juce::String ("-")) + ", "
                    + juce::String (proc.gainDb.load(), 1) + " dB    |    latency " + juce::String ((int) std::lround (latMs)) + " ms",
                footer, juce::Justification::centredLeft);
}
