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

NspLookAndFeel::NspLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void NspLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
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

void NoteSpaceEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text)
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

NoteSpaceEditor::NoteSpaceEditor (NoteSpaceProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    addKnob (contrast, "contrast", "CONTRAST");
    addKnob (tone, "tonelock", "TONE LOCK");
    addKnob (fund, "fundamental", "FUNDAMENTAL");
    addKnob (repair, "repair", "REPAIR");
    addKnob (translate, "translate", "TRANSLATE");
    addKnob (range, "range", "RANGE");
    setResizable (true, true);
    setResizeLimits (600, 390, 1100, 715);
    getConstrainer()->setFixedAspectRatio (720.0 / 468.0);
    setSize (720, 468);
    startTimerHz (15);
}

NoteSpaceEditor::~NoteSpaceEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NoteSpaceEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (34);
    r.removeFromTop (8);
    auto knobs = r.removeFromTop (118);
    const int w = knobs.getWidth() / 6;
    for (auto* k : { &contrast, &tone, &fund, &repair, &translate, &range })
    {
        auto cell = knobs.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
    r.removeFromTop (6);
    meterArea = r.removeFromRight (r.getWidth() / 3);
    r.removeFromRight (8);
    profileArea = r;
}

void NoteSpaceEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("NOTE SPACE", 14, 14, 200, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("the note and everything that is not the note, handled separately", 130, 14, getWidth() - 144, 26,
                juce::Justification::centredRight);

    const float pitch = proc.pitchHz.load();
    const bool live = pitch > 20.0f;

    // ---- harmonic profile -----------------------------------------------------------------
    g.setColour (kPanel);
    g.fillRoundedRectangle (profileArea.toFloat(), 8.0f);
    {
        auto a = profileArea.reduced (14, 10);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("HARMONICS (dB re the fundamental)", a.removeFromTop (14), juce::Justification::centredLeft);
        auto labels = a.removeFromBottom (14);
        a.removeFromTop (6);
        const float top = 6.0f, bottom = -42.0f;
        auto Y = [&] (float db) { return (float) a.getY() + (top - juce::jlimit (bottom, top, db)) / (top - bottom) * (float) a.getHeight(); };
        g.setColour (kLine);
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f })
        {
            g.drawHorizontalLine ((int) Y (db), (float) a.getX(), (float) a.getRight());
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String ((int) db), a.getRight() - 24, (int) Y (db) - 10, 24, 10, juce::Justification::right);
        }
        const int H = NoteSpaceProcessor::kH;
        const float cw = (float) (a.getWidth() - 28) / (float) H;
        for (int h = 0; h < H; ++h)
        {
            const float x0 = (float) a.getX() + cw * (float) h;
            const float vin = proc.hIn[h].load(), vout = proc.hOut[h].load();
            auto bar = [&] (float v, juce::Colour c, float x, float bw) {
                if (! live || v < -90.0f)
                    return;
                const float y = Y (v);
                g.setColour (c);
                g.fillRoundedRectangle (x, y, bw, std::max (2.0f, (float) a.getBottom() - y), 2.0f);
            };
            bar (vin, kDim.withAlpha (0.7f), x0 + cw * 0.12f, cw * 0.36f);
            bar (vout, kAccent, x0 + cw * 0.52f, cw * 0.36f);
            g.setColour (kDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (h == 0 ? juce::String ("f0") : juce::String (h + 1), (int) x0, labels.getY(), (int) cw, labels.getHeight(), juce::Justification::centred);
        }
        if (! live)
        {
            g.setColour (kDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("Waiting for a note. With no clear pitch nothing is processed: the input passes through.", a.reduced (20),
                              juce::Justification::centred, 3);
        }
    }

    // ---- note vs residual ----------------------------------------------------------------
    g.setColour (kPanel);
    g.fillRoundedRectangle (meterArea.toFloat(), 8.0f);
    {
        auto a = meterArea.reduced (14, 10);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("NOTE vs EVERYTHING ELSE", a.removeFromTop (14), juce::Justification::centredLeft);
        const auto foot = a.removeFromBottom (16);
        a.removeFromTop (8);
        g.setColour (kText);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (live ? noteName (pitch) : juce::String ("-"), a.removeFromTop (32), juce::Justification::centredLeft);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (live ? juce::String (pitch, 1) + " Hz" : juce::String ("no pitch"), a.removeFromTop (14), juce::Justification::centredLeft);
        a.removeFromTop (10);
        auto row = [&] (const juce::String& name, float db, juce::Colour c) {
            auto rr = a.removeFromTop (24);
            g.setColour (kDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (name, rr.removeFromLeft (34), juce::Justification::centredLeft);
            const auto slot = rr.removeFromLeft (rr.getWidth() - 56).toFloat();
            const auto track = juce::Rectangle<float> (slot.getX(), slot.getCentreY() - 5.0f, slot.getWidth() - 6.0f, 10.0f);
            g.setColour (kLine);
            g.fillRoundedRectangle (track, 4.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 10.0f) / 40.0f);
            g.setColour (c);
            g.fillRoundedRectangle (track.withWidth (std::max (4.0f, track.getWidth() * frac)), 4.0f);
            g.setColour (kText);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (juce::String (db, 1) + " dB", rr, juce::Justification::centredRight);
        };
        row ("in", proc.noteResIn.load(), kDim);
        row ("out", proc.noteResOut.load(), kAccent);
        a.removeFromTop (6);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("How far the note stands above the mud, rumble and noise around it (partials vs residual energy).", a,
                          juce::Justification::topLeft, 4);
        g.drawFittedText ("residual " + juce::String (proc.residualDb.load(), 1) + " dB  |  latency "
                              + juce::String ((int) std::lround (proc.latencySeconds.load() * 1000.0)) + " ms",
                          foot, juce::Justification::centredLeft, 1, 0.7f);
    }
}
