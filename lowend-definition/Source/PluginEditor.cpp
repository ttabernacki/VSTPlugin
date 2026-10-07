#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff101418), kPanel (0xff171d23), kPanel2 (0xff1d252d), kLine (0xff2c3640), kAccent (0xff4fd1c5),
    kWarm (0xfff6ad55), kText (0xffdde6ee), kDim (0xff7b8a97);

juce::String noteName (float hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

LedLookAndFeel::LedLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void LedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    juce::Colour fill = on ? kAccent.darker (0.4f) : kPanel2;
    if (highlighted)
        fill = fill.brighter (0.1f);
    if (down)
        fill = fill.brighter (0.2f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? fill.brighter (0.3f) : kLine);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                                       juce::Slider& s)
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

void LowEndDefinitionEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
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

LowEndDefinitionEditor::LowEndDefinitionEditor (LowEndDefinitionProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    addKnob (contrast, "contrast", "CONTRAST");
    addKnob (punch, "punch", "PUNCH");
    addKnob (sustain, "sustain", "SUSTAIN");
    addKnob (kick, "kick", "KICK");
    addKnob (range, "range", "RANGE");
    match.setClickingTogglesState (true);
    align.setClickingTogglesState (true);
    addAndMakeVisible (align);
    alignAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "align", align);
    addAndMakeVisible (match);
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "match", match);
    setResizable (true, true);
    setResizeLimits (560, 380, 1100, 700);
    getConstrainer()->setFixedAspectRatio (680.0 / 440.0);
    setSize (680, 440);
    startTimerHz (15);
}

LowEndDefinitionEditor::~LowEndDefinitionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LowEndDefinitionEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto head = r.removeFromTop (34); // title
    match.setBounds (head.removeFromRight (130).reduced (0, 3));
    head.removeFromRight (6);
    align.setBounds (head.removeFromRight (120).reduced (0, 3));
    r.removeFromTop (8);
    auto knobs = r.removeFromTop (118);
    const int w = knobs.getWidth() / 5;
    for (auto* k : { &contrast, &punch, &sustain, &kick, &range })
    {
        auto cell = knobs.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
    r.removeFromTop (6);
    auto panel = r;
    meterArea = panel.removeFromLeft (panel.getWidth() / 3);
    panel.removeFromLeft (8);
    chartArea = panel;
}

void LowEndDefinitionEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("LOW-END DEFINITION", 14, 14, 220, 26, juce::Justification::centredLeft);

    const float dIn = proc.defIn.load(), dOut = proc.defOut.load();
    const float pitch = proc.pitchHz.load();

    // ---- definition meter -------------------------------------------------------------
    g.setColour (kPanel);
    g.fillRoundedRectangle (meterArea.toFloat(), 8.0f);
    {
        auto a = meterArea.reduced (14, 12);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("DEFINITION", a.removeFromTop (14), juce::Justification::centredLeft);
        a.removeFromTop (6);
        auto bar = [&] (juce::Rectangle<int> row, const juce::String& name, float v, juce::Colour c) {
            g.setColour (kDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (name, row.removeFromLeft (44), juce::Justification::centredLeft);
            const auto slot = row.removeFromLeft (row.getWidth() - 46).toFloat();
            const auto track = juce::Rectangle<float> (slot.getX(), slot.getCentreY() - 5.0f, slot.getWidth() - 6.0f, 10.0f);
            g.setColour (kLine);
            g.fillRoundedRectangle (track, 4.0f);
            if (v >= 0.0f)
            {
                g.setColour (c);
                g.fillRoundedRectangle (track.withWidth (std::max (4.0f, track.getWidth() * std::min (1.0f, v))), 4.0f);
            }
            g.setColour (kText);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (v >= 0.0f ? juce::String (juce::roundToInt (v * 100.0f)) + " %" : juce::String ("-"), row, juce::Justification::centredRight);
        };
        bar (a.removeFromTop (26), "in", dIn, kDim);
        bar (a.removeFromTop (26), "out", dOut, kAccent);
        a.removeFromTop (8);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Share of the low band's energy that sits on the note's own fundamental.", a, juce::Justification::topLeft, 4);
    }

    // ---- history ----------------------------------------------------------------------
    g.setColour (kPanel);
    g.fillRoundedRectangle (chartArea.toFloat(), 8.0f);
    {
        auto a = chartArea.reduced (12, 10);
        auto footer = a.removeFromBottom (16);
        auto strip = a.removeFromBottom (a.getHeight() / 3);
        a.removeFromBottom (6);
        LowEndDefinitionProcessor::Point pts[LowEndDefinitionProcessor::kHist];
        const int n = proc.getHistory (pts);
        const float dx = (float) a.getWidth() / (float) (n - 1);
        auto line = [&] (bool out, juce::Colour c) {
            juce::Path path;
            bool pen = false;
            for (int i = 0; i < n; ++i)
            {
                const float v = out ? pts[i].defOut : pts[i].defIn;
                if (v < 0.0f)
                {
                    pen = false;
                    continue;
                }
                const float x = (float) a.getX() + dx * (float) i, y = (float) a.getBottom() - (float) a.getHeight() * std::min (1.0f, v);
                if (pen)
                    path.lineTo (x, y);
                else
                    path.startNewSubPath (x, y), pen = true;
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (out ? 2.0f : 1.5f));
        };
        g.setColour (kLine);
        for (int q = 1; q < 4; ++q)
            g.drawHorizontalLine (a.getY() + a.getHeight() * q / 4, (float) a.getX(), (float) a.getRight());
        line (false, kDim);
        line (true, kAccent);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("definition, last 5 s", a.getX() + 2, a.getY(), 160, 12, juce::Justification::left);

        // punch / sustain gain
        g.setColour (kLine);
        const float midY = (float) strip.getCentreY();
        g.drawHorizontalLine ((int) midY, (float) strip.getX(), (float) strip.getRight());
        juce::Path gp;
        gp.startNewSubPath ((float) strip.getX(), midY);
        for (int i = 0; i < n; ++i)
            gp.lineTo ((float) strip.getX() + dx * (float) i, midY - juce::jlimit (-12.0f, 12.0f, pts[i].trans) / 12.0f * 0.5f * (float) strip.getHeight());
        gp.lineTo ((float) strip.getRight(), midY);
        gp.closeSubPath();
        g.setColour (kWarm.withAlpha (0.55f));
        g.fillPath (gp);
        juce::Path dp;
        dp.startNewSubPath ((float) strip.getX(), midY);
        for (int i = 0; i < n; ++i)
            dp.lineTo ((float) strip.getX() + dx * (float) i, midY + juce::jlimit (0.0f, 12.0f, pts[i].duck) / 12.0f * 0.5f * (float) strip.getHeight());
        dp.lineTo ((float) strip.getRight(), midY);
        dp.closeSubPath();
        g.setColour (juce::Colour (0xff63a4ff).withAlpha (0.6f));
        g.fillPath (dp);
        g.setColour (kDim);
        g.drawText ("punch / sustain gain (up)   kick duck (down)", strip.getX() + 2, strip.getY(), 300, 12, juce::Justification::left);

        g.setFont (juce::FontOptions (11.0f));
        const double latMs = proc.latencySeconds.load() * 1000.0;
        juce::String kickText = "kick: no sidechain";
        if (proc.scConnected.load())
        {
            kickText = "kick: duck " + juce::String (proc.duckDb.load(), 1) + " dB";
            if (proc.alignKnown.load())
                kickText += ", sum " + juce::String (proc.alignDb.load(), 1) + " dB" + (proc.flipped.load() ? " (flipped)" : "")
                            + ", " + juce::String (proc.alignLagMs.load(), 1) + " ms";
        }
        g.drawText ((pitch > 20.0f ? noteName (pitch) + "  " + juce::String (pitch, 1) + " Hz, note bell " + juce::String (proc.bellDb.load(), 1) + " dB"
                                   : juce::String ("no note"))
                        + "    |    " + kickText + "    |    latency " + juce::String ((int) std::lround (latMs)) + " ms",
                    footer, juce::Justification::centredLeft);
    }
}
