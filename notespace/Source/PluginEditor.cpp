#include "PluginEditor.h"

namespace
{
using namespace bassui;

juce::String noteName (float hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
} // namespace

NoteSpaceEditor::NoteSpaceEditor (NoteSpaceProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    contrast.setup (*this, proc.apvts, "contrast", "CONTRAST");
    tone.setup (*this, proc.apvts, "tonelock", "TONE LOCK");
    fund.setup (*this, proc.apvts, "fundamental", "FUNDAMENTAL");
    repair.setup (*this, proc.apvts, "repair", "REPAIR");
    translate.setup (*this, proc.apvts, "translate", "TRANSLATE");
    range.setup (*this, proc.apvts, "range", "RANGE");
    punch.setup (*this, proc.apvts, "punch", "PUNCH");
    sustain.setup (*this, proc.apvts, "sustain", "SUSTAIN");
    kick.setup (*this, proc.apvts, "kick", "KICK");
    setResizable (true, true);
    setResizeLimits (720, 400, 1300, 708);
    getConstrainer()->setFixedAspectRatio (860.0 / 468.0);
    setSize (860, 468);
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
    const int w = knobs.getWidth() / 9;
    for (auto* k : { &contrast, &tone, &fund, &repair, &translate, &range, &punch, &sustain, &kick })
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
        g.drawFittedText ("residual " + juce::String (proc.residualDb.load(), 1) + " dB  |  low band " + juce::String (proc.dynDb.load(), 1)
                              + " dB  |  duck " + juce::String (proc.scConnected.load() ? juce::String (-proc.duckDb.load(), 1) + " dB" : juce::String ("no sc"))
                              + "  |  latency "
                              + juce::String ((int) std::lround (proc.latencySeconds.load() * 1000.0)) + " ms",
                          foot, juce::Justification::centredLeft, 1, 0.7f);
    }
}
