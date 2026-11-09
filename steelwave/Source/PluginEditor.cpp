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
    voltype.setup (*this, proc.apvts, "voltype", "PEDAL TYPE");
    glide.setup (*this, proc.apvts, "glide", "GLIDE");
    strum.setup (*this, proc.apvts, "strum", "STRUM");
    pick.setup (*this, proc.apvts, "hardness", "PICK");
    bright.setup (*this, proc.apvts, "brightness", "BRIGHTNESS");
    sustain.setup (*this, proc.apvts, "sustain", "SUSTAIN");
    noise.setup (*this, proc.apvts, "barnoise", "BAR NOISE");
    behind.setup (*this, proc.apvts, "behindbar", "BEHIND BAR");
    vibRate.setup (*this, proc.apvts, "vibrate", "VIB RATE");
    vibDepth.setup (*this, proc.apvts, "vibdepth", "VIB DEPTH");
    sym.setup (*this, proc.apvts, "sympathetic", "SYMPATHETIC");
    level.setup (*this, proc.apvts, "level", "LEVEL");

    pedalKeys.setup (*this, proc.apvts, "pedalkeys", "PEDAL KEYS");
    modWheel.setup (*this, proc.apvts, "modwheel", "MOD WHEEL");
    aftertouch.setup (*this, proc.apvts, "aftertouch", "AFTERTOUCH");
    pedalSpeed.setup (*this, proc.apvts, "pedalspeed", "PEDAL SPEED");
    swellBelow.setup (*this, proc.apvts, "swellbelow", "AUTO SWELL");
    swellTime.setup (*this, proc.apvts, "swelltime", "SWELL TIME");
    autoVib.setup (*this, proc.apvts, "autovib", "AUTO VIBRATO");
    vibDelay.setup (*this, proc.apvts, "vibdelay", "VIB DELAY");
    pedalKeys.box.setTooltip ("The lowest 7 keys of a 61-key board (MIDI 36-42) play A, B, C, LKL, LKR, RKL, RKR: hold them like pedals.");
    pedalSpeed.slider.setTooltip ("How fast the pedal keys travel. Hit a pedal key harder for a faster pedal (about 80-250 ms at 1x).");
    modWheel.box.setTooltip ("What the mod wheel does: vibrato, the A+B pedals (a non-springing strip holds a pedal bend), or a bar slide of up to 2 frets.");
    aftertouch.box.setTooltip ("Pressing into the keys shakes the bar (vibrato depth).");
    swellBelow.slider.setTooltip ("Notes played softer than this fade in like a volume-pedal swell; harder notes pick as usual. Off at 0.");
    swellTime.slider.setTooltip ("How long an auto swell takes to open.");
    autoVib.slider.setTooltip ("Vibrato that comes in on its own once a note has been held (re Vib depth).");
    vibDelay.slider.setTooltip ("How long a note is held before auto vibrato starts.");

    for (const auto& a : swadv::params())
    {
        auto k = std::make_unique<bassui::Knob>();
        k->setup (*this, proc.apvts, a.id, a.label);
        k->slider.setTooltip (juce::String (a.name) + ": " + a.help);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
        advKnobs.push_back (std::move (k));
    }
    for (auto* b : { &playTab, &ctlTab, &advTab })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, kPanel2);
        b->setColour (juce::TextButton::buttonOnColourId, kAccent.darker (0.6f));
        addAndMakeVisible (*b);
    }
    playTab.onClick = [this] { showPage (PlayPage); };
    ctlTab.onClick = [this] { showPage (ControlsPage); };
    advTab.onClick = [this] { showPage (AdvancedPage); };
    resetAdv.setColour (juce::TextButton::buttonColourId, kPanel2);
    resetAdv.setTooltip ("Put every Advanced knob back to the calibrated value (12 o'clock).");
    resetAdv.onClick = [this] {
        for (const auto& a : swadv::params())
            if (auto* prm = proc.apvts.getParameter (a.id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->getDefaultValue());
                prm->endChangeGesture();
            }
    };
    addChildComponent (resetAdv);

    setResizable (true, true);
    setResizeLimits (840, 485, 1400, 808);
    getConstrainer()->setFixedAspectRatio (900.0 / 520.0);
    setSize (900, 520);
    showPage (PlayPage);
    startTimerHz (20);
}

SteelwaveEditor::~SteelwaveEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SteelwaveEditor::showPage (int p)
{
    page = p;
    const bool play = p == PlayPage, ctl = p == ControlsPage, adv = p == AdvancedPage;
    playTab.setToggleState (play, juce::dontSendNotification);
    ctlTab.setToggleState (ctl, juce::dontSendNotification);
    advTab.setToggleState (adv, juce::dontSendNotification);
    for (auto* c : std::initializer_list<Choice*> { &set, &key, &pickup, &volcc, &voltype })
    {
        c->box.setVisible (play);
        c->label.setVisible (play);
    }
    for (auto* k : { &glide, &strum, &pick, &bright, &sustain, &noise, &behind, &vibRate, &vibDepth, &sym, &level })
    {
        k->slider.setVisible (play);
        k->label.setVisible (play);
    }
    for (auto* c : std::initializer_list<Choice*> { &pedalKeys, &modWheel, &aftertouch })
    {
        c->box.setVisible (ctl);
        c->label.setVisible (ctl);
    }
    for (auto* k : { &pedalSpeed, &swellBelow, &swellTime, &autoVib, &vibDelay })
    {
        k->slider.setVisible (ctl);
        k->label.setVisible (ctl);
    }
    for (auto& k : advKnobs)
    {
        k->slider.setVisible (adv);
        k->label.setVisible (adv);
    }
    resetAdv.setVisible (adv);
    repaint();
}

// the pedals and levers, lit by how far down they are
void SteelwaveEditor::drawPedals (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const int n = sw::kControls;
    const float w = (float) area.getWidth() / (float) n;
    for (int k = 0; k < n; ++k)
    {
        const auto cell = juce::Rectangle<float> ((float) area.getX() + w * (float) k, (float) area.getY(), w, (float) area.getHeight()).reduced (3.0f, 0.0f);
        const float t = juce::jlimit (0.0f, 1.0f, proc.uiTravel[(size_t) k].load());
        g.setColour (kLine);
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (kWarm.withAlpha (0.25f + 0.75f * t));
        g.fillRoundedRectangle (cell.withTrimmedTop (cell.getHeight() * (1.0f - t)), 4.0f);
        g.setColour (t > 0.5f ? kBg : kText);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (sw::controlName (k), cell, juce::Justification::centred);
    }
}

void SteelwaveEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto header = r.removeFromTop (34);
    advTab.setBounds (header.removeFromRight (96).reduced (0, 4));
    header.removeFromRight (6);
    ctlTab.setBounds (header.removeFromRight (90).reduced (0, 4));
    header.removeFromRight (6);
    playTab.setBounds (header.removeFromRight (64).reduced (0, 4));
    r.removeFromTop (6);

    // --- Controls page: pedals & wheels, swell & vibrato, then a map of what does what
    {
        auto a = r;
        const int rowH = 112;
        for (int i = 0; i < 2; ++i)
        {
            auto row = a.removeFromTop (rowH).reduced (0, 3);
            ctlAreas[(size_t) i] = row;
            row.removeFromLeft (110);
            const int cw = row.getWidth() / 5;
            auto knobCell = [&] (bassui::Knob& k) {
                auto cell = row.removeFromLeft (cw).reduced (4, 4);
                k.label.setBounds (cell.removeFromTop (16));
                k.slider.setBounds (cell);
            };
            auto choiceCell = [&] (Choice& c) {
                auto cell = row.removeFromLeft (cw).reduced (6, 0).withSizeKeepingCentre (cw - 12, 44);
                c.label.setBounds (cell.removeFromTop (14));
                c.box.setBounds (cell.reduced (0, 1));
            };
            if (i == 0)
            {
                choiceCell (pedalKeys);
                knobCell (pedalSpeed);
                choiceCell (modWheel);
                choiceCell (aftertouch);
            }
            else
            {
                knobCell (swellBelow);
                knobCell (swellTime);
                knobCell (autoVib);
                knobCell (vibDelay);
            }
        }
        a.removeFromTop (3);
        ctlAreas[2] = a;
        pedalStrip = a.reduced (14, 0).removeFromTop (44).translated (0, 28);
    }


    // --- Advanced page: four groups, one row each
    {
        auto a = r;
        auto top = a.removeFromTop (26);
        resetAdv.setBounds (top.removeFromRight (150).reduced (0, 2));
        a.removeFromTop (6);
        const int rowH = a.getHeight() / swadv::kGroups;
        const int maxPerRow = 7;
        for (int g = 0; g < swadv::kGroups; ++g)
        {
            auto row = a.removeFromTop (rowH).reduced (0, 3);
            groupAreas[(size_t) g] = row;
            row.removeFromLeft (96); // group title
            const int kw = row.getWidth() / maxPerRow;
            const auto& ps = swadv::params();
            for (size_t i = 0; i < ps.size(); ++i)
                if (ps[i].group == g)
                {
                    auto cell = row.removeFromLeft (kw).reduced (2, 2);
                    advKnobs[i]->label.setBounds (cell.removeFromTop (14));
                    advKnobs[i]->slider.setBounds (cell);
                }
        }
    }

    auto row = r.removeFromTop (42);
    auto place = [] (Choice& c, juce::Rectangle<int> cell) {
        c.label.setBounds (cell.removeFromTop (14));
        c.box.setBounds (cell.reduced (0, 1));
    };
    place (set, row.removeFromLeft (row.getWidth() * 3 / 10).withTrimmedRight (10));
    const int w = row.getWidth() / 9;
    place (key, row.removeFromLeft (w).withTrimmedRight (10));
    place (pickup, row.removeFromLeft (w * 2).withTrimmedRight (10));
    place (volcc, row.removeFromLeft (w * 3).withTrimmedRight (10));
    place (voltype, row);
    r.removeFromTop (10);
    auto knobs = r.removeFromTop (112);
    const int kw = knobs.getWidth() / 11;
    for (auto* k : { &glide, &strum, &pick, &bright, &sustain, &noise, &behind, &vibRate, &vibDepth, &sym, &level })
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
    g.drawText ("physically modelled E9 pedal steel  |  no samples", 120, 14, playTab.getX() - 134, 26, juce::Justification::centredRight);

    if (page == ControlsPage)
    {
        const char* titles[] = { "PEDALS & WHEELS", "SWELL & VIBRATO", "WHAT DOES WHAT" };
        for (int i = 0; i < 3; ++i)
        {
            const auto ca = ctlAreas[(size_t) i];
            g.setColour (kPanel);
            g.fillRoundedRectangle (ca.toFloat(), 8.0f);
            g.setColour (kAccent);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            if (i < 2)
                g.drawFittedText (titles[i], ca.withWidth (106).reduced (12, 0), juce::Justification::centredLeft, 2);
            else
                g.drawText (titles[i], ca.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft);
        }
        drawPedals (g, pedalStrip);
        auto t = ctlAreas[2].reduced (14, 8);
        t.removeFromTop (16 + 12 + 44 + 8);
        const bool keys = (int) proc.apvts.getRawParameterValue ("pedalkeys")->load() == 1;
        const int mw = (int) proc.apvts.getRawParameterValue ("modwheel")->load();
        const char* mwText[] = { "vibrato", "the A+B pedals", "a bar slide (up to 2 frets)", "nothing" };
        const bool at = (int) proc.apvts.getRawParameterValue ("aftertouch")->load() == 0;
        juce::String txt;
        txt << (keys ? "Pedal keys: the lowest 7 keys of a 61-key board (MIDI 36-42) are A  B  C  LKL  LKR  RKL  RKR. Hold them like pedals; hit harder for a faster pedal.\n"
                     : "Pedal keys are off: MIDI 36-42 play notes. CC20-26 still work the pedals and levers.\n")
            << "Pitch wheel: bends the chord (up: a 4th up, down: a 3rd up).   Mod wheel: " << mwText[juce::jlimit (0, 3, mw)] << ".\n"
            << "Aftertouch: " << (at ? "vibrato (press into the keys)" : "off") << ".   Expression pedal: the volume pedal (Play page).   Sustain: let strings ring.\n"
            << "Keyswitches MIDI 24-35: chord sets 1-8, single-note sets 1-4.";
        g.setColour (kText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (txt, t, juce::Justification::topLeft, 6);
        return;
    }

    if (page == AdvancedPage)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("The physical model's calibration. 12 o'clock = the tuned value. Hover a knob for what it does.",
                    14, resetAdv.getY(), resetAdv.getX() - 24, resetAdv.getHeight(), juce::Justification::centredLeft);
        for (int gi = 0; gi < swadv::kGroups; ++gi)
        {
            const auto ga = groupAreas[(size_t) gi];
            g.setColour (kPanel);
            g.fillRoundedRectangle (ga.toFloat(), 8.0f);
            g.setColour (kAccent);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (swadv::groupName (gi), ga.withWidth (92).reduced (12, 0), juce::Justification::centredLeft, 2);
        }
        return;
    }

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
    drawPedals (g, info.removeFromTop (22)); // pedals and levers down, live
    info.removeFromTop (4);
    meter ("wheel", proc.uiWheel.load(), true);
    meter ("volume", proc.uiVolume.load(), false);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText ("mod wheel: vibrato  |  CC64: let ring  |  CC20-26: A B C pedals, LKL LKR RKL RKR", info,
                      juce::Justification::bottomLeft, 3);
}
