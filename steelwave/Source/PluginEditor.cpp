#include "PluginEditor.h"

namespace
{
using namespace bassui;

const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* kSlotNames[] = { "I", "I (alt)", "ii", "II", "iii", "IV", "iv", "V", "#V dim", "vi", "bVII", "vii dim" };

juce::String chordName (sw::ChordName c, int keyPc)
{
    const char* q = c.q == sw::Maj ? "" : c.q == sw::Min ? "m" : "dim";
    return juce::String (kNoteNames[(keyPc + c.root) % 12]) + q;
}
} // namespace

void SteelwaveEditor::Choice::setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                                     const juce::String& text)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        box.addItemList (p->choices, 1);
    parent.addAndMakeVisible (box);
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, kDim);
    label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    parent.addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, box);
}

SteelwaveEditor::SteelwaveEditor (SteelwaveProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    set.setup (*this, proc.apvts, "set", "SET  (keyswitch C1-B1)");
    key.setup (*this, proc.apvts, "key", "KEY");
    pickup.setup (*this, proc.apvts, "pickup", "PICKUP");
    volcc.setup (*this, proc.apvts, "volcc", "VOLUME PEDAL");
    glide.setup (*this, proc.apvts, "glide", "GLIDE");
    strum.setup (*this, proc.apvts, "strum", "STRUM");
    pick.setup (*this, proc.apvts, "hardness", "PICK");
    bright.setup (*this, proc.apvts, "brightness", "BRIGHTNESS");
    sustain.setup (*this, proc.apvts, "sustain", "SUSTAIN");
    noise.setup (*this, proc.apvts, "barnoise", "BAR NOISE");
    vibRate.setup (*this, proc.apvts, "vibrate", "VIB RATE");
    vibDepth.setup (*this, proc.apvts, "vibdepth", "VIB DEPTH");
    sym.setup (*this, proc.apvts, "sympathetic", "SYMPATHETIC");
    level.setup (*this, proc.apvts, "level", "LEVEL");
    setResizable (true, true);
    setResizeLimits (820, 420, 1400, 716);
    getConstrainer()->setFixedAspectRatio (900.0 / 460.0);
    setSize (900, 460);
    startTimerHz (20);
}

SteelwaveEditor::~SteelwaveEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SteelwaveEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (34);
    r.removeFromTop (6);
    auto row = r.removeFromTop (42);
    auto place = [] (Choice& c, juce::Rectangle<int> cell) {
        c.label.setBounds (cell.removeFromTop (14));
        c.box.setBounds (cell.reduced (0, 1));
    };
    place (set, row.removeFromLeft (row.getWidth() * 2 / 5).withTrimmedRight (10));
    const int w = row.getWidth() / 3;
    place (key, row.removeFromLeft (w).withTrimmedRight (10));
    place (pickup, row.removeFromLeft (w).withTrimmedRight (10));
    place (volcc, row);
    r.removeFromTop (10);
    auto knobs = r.removeFromTop (112);
    const int kw = knobs.getWidth() / 10;
    for (auto* k : { &glide, &strum, &pick, &bright, &sustain, &noise, &vibRate, &vibDepth, &sym, &level })
    {
        auto cell = knobs.removeFromLeft (kw);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
    r.removeFromTop (8);
    neckArea = r;
}

void SteelwaveEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("STEELWAVE", 14, 14, 200, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("physically modelled E9 pedal steel  |  no samples", 130, 14, getWidth() - 144, 26, juce::Justification::centredRight);

    // ---- the neck: 10 strings, the bar, what is ringing ------------------------------------
    g.setColour (kPanel);
    g.fillRoundedRectangle (neckArea.toFloat(), 8.0f);
    auto a = neckArea.reduced (14, 10);
    const int setIdx = proc.uiSet.load(), slot = proc.uiSlot.load(), bar = proc.uiBar.load();
    const int keyPc = (int) proc.apvts.getRawParameterValue ("key")->load();
    const bool chords = setIdx < sw::kChordSets;

    auto info = a.removeFromRight (std::min (300, a.getWidth() / 3));
    a.removeFromRight (12);
    const int sounding = proc.uiSounding.load();
    const float rowH = (float) a.getHeight() / (float) sw::kStrings;
    const float x0 = (float) a.getX() + 64.0f, x1 = (float) a.getRight() - 40.0f;
    auto fretX = [&] (float semis) { // fret spacing as on the instrument (24.5" scale, 24 frets shown)
        const float L = 1.0f - std::pow (2.0f, -semis / 12.0f);
        const float Lmax = 1.0f - std::pow (2.0f, -24.0f / 12.0f);
        return x0 + (x1 - x0) * juce::jlimit (0.0f, 1.0f, L / Lmax);
    };
    g.setColour (kLine);
    for (int f = 0; f <= 24; ++f)
        g.drawVerticalLine ((int) fretX ((float) f), (float) a.getY(), (float) a.getBottom());
    g.setFont (juce::FontOptions (9.0f));
    for (int f : { 3, 5, 7, 9, 12, 15, 17, 19, 21, 24 })
    {
        g.setColour (kDim);
        g.drawText (juce::String (f), (int) fretX ((float) f) - 10, a.getBottom() - 10, 20, 10, juce::Justification::centred);
    }
    for (int s = 0; s < sw::kStrings; ++s)
    {
        const float y = (float) a.getY() + rowH * ((float) s + 0.5f);
        const bool on = (sounding >> s) & 1;
        const float semis = proc.uiSemis[(size_t) s].load();
        const int midi = (int) std::lround (sw::kE9Open[(size_t) s] + semis);
        g.setColour (on ? kText : kDim);
        g.setFont (juce::FontOptions (10.0f, on ? juce::Font::bold : juce::Font::plain));
        g.drawText (juce::String (s + 1) + "  " + kNoteNames[sw::kE9Open[(size_t) s] % 12], a.getX(), (int) (y - 7), 40, 14,
                    juce::Justification::centredLeft);
        g.setColour (on ? kAccent.withAlpha (0.9f) : kLine.brighter (0.3f));
        g.drawLine (x0, y, x1, y, s >= 5 ? 2.0f : 1.2f);
        if (on)
        {
            g.setColour (kText);
            g.drawText (juce::String (kNoteNames[((midi % 12) + 12) % 12]), (int) x1 + 8, (int) (y - 7), 30, 14, juce::Justification::centredLeft);
        }
    }
    // the bar
    const float bx = fretX ((float) bar);
    g.setColour (kWarm.withAlpha (0.85f));
    g.fillRoundedRectangle (bx - 4.0f, (float) a.getY() - 2.0f, 8.0f, (float) a.getHeight() - 8.0f, 4.0f);

    // ---- what is playing ---------------------------------------------------------------------
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (chords ? "CHORD" : "SINGLE NOTES", info.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (kText);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    juce::String title = "-", sub, how;
    if (chords && slot >= 0)
    {
        const auto& sl = sw::chordSlots()[(size_t) slot];
        const auto& v = sw::chordSets()[(size_t) setIdx][(size_t) slot];
        title = chordName (sl.rest, keyPc);
        sub = juce::String (kSlotNames[slot]) + "  |  bar " + juce::String (bar) + "  |  " + juce::String (sw::comboName (v.restCombo));
        how = "wheel up: " + chordName (sl.up, keyPc) + " (" + juce::String (v.upHow) + ")\nwheel down: " + chordName (sl.down, keyPc)
              + " (" + juce::String (v.downHow) + ")";
    }
    else if (! chords)
    {
        title = juce::String (kNoteNames[keyPc]) + " major";
        sub = "bar " + juce::String (bar);
        how = "white keys: the scale, bending with the wheel\nblack keys: the note below, held still";
    }
    g.drawText (title, info.removeFromTop (32), juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (sub, info.removeFromTop (16), juce::Justification::centredLeft);
    info.removeFromTop (6);
    g.setColour (kText);
    g.drawFittedText (how, info.removeFromTop (34), juce::Justification::topLeft, 3);
    info.removeFromTop (6);
    // wheel and volume pedal
    auto meter = [&] (const juce::String& name, float v, bool bipolar) {
        auto rr = info.removeFromTop (20);
        g.setColour (kDim);
        g.drawText (name, rr.removeFromLeft (56), juce::Justification::centredLeft);
        const auto t = rr.toFloat().reduced (0, 6);
        g.setColour (kLine);
        g.fillRoundedRectangle (t, 3.0f);
        g.setColour (kAccent);
        if (bipolar)
        {
            const float c = t.getCentreX(), e = c + v * t.getWidth() * 0.5f;
            g.fillRoundedRectangle (juce::Rectangle<float> (std::min (c, e), t.getY(), std::max (2.0f, std::abs (e - c)), t.getHeight()), 3.0f);
        }
        else
            g.fillRoundedRectangle (t.withWidth (std::max (2.0f, t.getWidth() * v)), 3.0f);
    };
    meter ("wheel", proc.uiWheel.load(), true);
    meter ("volume", proc.uiVolume.load(), false);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText ("mod wheel: vibrato  |  CC64: let ring  |  CC20-26: A B C pedals, LKL LKR RKL RKR", info,
                      juce::Justification::bottomLeft, 3);
}
