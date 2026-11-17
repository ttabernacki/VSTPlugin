#include "PluginEditor.h"

namespace
{
using namespace bassui;

const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* kSlotNames[] = { "I", "I alt", "ii", "II", "iii", "IV", "iv", "V", "#V dim", "vi", "bVII", "vii dim" };
const int kMajor[] = { 0, 2, 4, 5, 7, 9, 11 };

juce::String chordName (sw::ChordName c, int keyPc)
{
    const char* q = c.q == sw::Maj ? "" : c.q == sw::Min ? "m" : "dim";
    return juce::String (kNoteNames[(keyPc + c.root) % 12]) + q;
}

juce::String moveText (int combo, int frets)
{
    if (combo < 0)
        return "picked in its own voicing";
    juce::String pedals = combo == 0 ? juce::String ("pedals off") : juce::String (sw::comboName ((uint32_t) combo));
    if (frets == 0)
        return "bend: " + pedals + ", same fret";
    return pedals + ", slide " + (frets > 0 ? "up " : "down ") + juce::String (std::abs (frets)) + (std::abs (frets) == 1 ? " fret" : " frets");
}

void styleButton (juce::TextButton& b, juce::Colour on)
{
    b.setColour (juce::TextButton::buttonColourId, kPanel2);
    b.setColour (juce::TextButton::buttonOnColourId, on);
}
} // namespace

// ==== controller setup ===========================================================================
SetupWizard::SetupWizard (SteelwaveProcessor& p, std::function<void()> onClose) : proc (p), close (std::move (onClose))
{
    found.fill (-1);
    for (auto* b : { &back, &skip, &next, &done, &reset })
    {
        styleButton (*b, kAccent.darker (0.6f));
        addAndMakeVisible (*b);
    }
    next.setColour (juce::TextButton::buttonColourId, kAccent.darker (0.5f));
    back.onClick = [this] { go (std::max (0, step - 1)); };
    skip.onClick = [this] {
        proc.finishStep (step, -1);
        found[(size_t) step] = -1;
        go (step + 1);
    };
    next.onClick = [this] {
        if (step >= SteelwaveProcessor::kWizSteps)
            return;
        const int src = proc.bestSource (step, taken());
        proc.finishStep (step, src);
        found[(size_t) step] = src;
        go (step + 1);
    };
    done.onClick = [this] {
        proc.wizardStep = -1;
        auto c = proc.calibration();
        if (! c.asked)
        {
            c.asked = true;
            proc.setCalibration (c);
        }
        if (close)
            close();
    };
    reset.onClick = [this] {
        SteelwaveProcessor::ControllerCal c;
        c.asked = true;
        proc.setCalibration (c);
        found.fill (-1);
        go (SteelwaveProcessor::kWizSteps);
    };
}

void SetupWizard::begin()
{
    found.fill (-1);
    go (0);
}

void SetupWizard::go (int s)
{
    step = juce::jlimit (0, (int) SteelwaveProcessor::kWizSteps, s);
    if (step < SteelwaveProcessor::kWizSteps)
        proc.startCapture (step);
    else
        proc.wizardStep = -1;
    const bool summary = step == SteelwaveProcessor::kWizSteps;
    skip.setVisible (! summary);
    next.setVisible (! summary);
    reset.setVisible (summary);
    back.setEnabled (step > 0);
    next.setButtonText (step == SteelwaveProcessor::kWizSteps - 1 ? "Finish" : "Next");
    done.setButtonText (summary ? "Done" : "Close");
    repaint();
}

std::vector<int> SetupWizard::taken() const
{
    std::vector<int> t;
    for (int i = 0; i < step; ++i)
        if (found[(size_t) i] >= 0 && found[(size_t) i] < 128)
            t.push_back (found[(size_t) i]);
    return t;
}

juce::String SetupWizard::sourceName (int src) const
{
    if (src == SteelwaveProcessor::kSrcPitch)
        return "Pitch wheel";
    if (src == SteelwaveProcessor::kSrcPressure)
        return "Aftertouch";
    if (src == SteelwaveProcessor::kSrcVelocity)
        return "Velocity";
    if (src < 0)
        return "-";
    const juce::String known = src == 1 ? " (mod wheel)" : src == 11 ? " (expression)" : src == 7 ? " (volume)" : src == 4 ? " (foot)" : src == 2 ? " (breath)" : "";
    return "CC " + juce::String (src) + known;
}

void SetupWizard::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (600, 380);
    auto row = panel.reduced (24, 20).removeFromBottom (32);
    done.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    next.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    skip.setBounds (row.removeFromRight (80));
    back.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    reset.setBounds (row.removeFromLeft (150));
}

void SetupWizard::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    g.setColour (kPanel);
    g.fillRoundedRectangle (panel.toFloat(), 10.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (panel.toFloat(), 10.0f, 1.0f);
    auto a = panel.reduced (24, 20);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    const int nSteps = SteelwaveProcessor::kWizSteps;
    g.drawText (step < nSteps ? "CONTROLLER SETUP   " + juce::String (step + 1) + " / " + juce::String (nSteps) : juce::String ("CONTROLLER SETUP   DONE"),
                a.removeFromTop (24), juce::Justification::centredLeft);
    a.removeFromTop (10);
    static const char* titles[] = { "Pitch slider", "Mod slider", "Expression pedal", "Aftertouch", "Velocity" };
    static const char* what[] = {
        "Move your pitch slider (or wheel) all the way up and all the way down, then let it go. Steelwave bends to its real ends, "
        "with a little dead zone where it rests.",
        "Move your mod slider (or wheel) from the bottom to the top. Whatever CC it sends becomes the mod wheel.",
        "Rock your expression pedal from heel to toe. It becomes the volume pedal, over the travel it really has.",
        "Hold a key and press into it as hard as you comfortably would. That pressure becomes full vibrato.",
        "Play a few notes, from your softest to your hardest. That range becomes the whole pick range; the Velocity curve "
        "(Settings > Playing) shapes it." };
    if (step < nSteps)
    {
        g.setColour (kText);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (titles[step], a.removeFromTop (28), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (kDim);
        g.drawFittedText (what[step], a.removeFromTop (54), juce::Justification::topLeft, 3);
        a.removeFromTop (14);
        const int src = proc.bestSource (step, taken());
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        if (src < 0)
        {
            g.setColour (kWarm);
            g.drawText (step == SteelwaveProcessor::WizVelocity ? "Listening... (" + juce::String (proc.capNotes.load()) + " notes so far)"
                                                                : juce::String ("Listening... move it all the way."),
                        a.removeFromTop (20), juce::Justification::centredLeft);
            return;
        }
        const bool pitchWheel = src == SteelwaveProcessor::kSrcPitch;
        auto norm = [&] (float v) { return pitchWheel ? 0.5f * (v + 1.0f) : v; };
        const float lo = norm (proc.capLo[(size_t) src].load()), hi = norm (proc.capHi[(size_t) src].load()),
                    last = norm (proc.capLast[(size_t) src].load());
        g.setColour (kAccent);
        g.drawText ("Found: " + sourceName (src), a.removeFromTop (20), juce::Justification::centredLeft);
        a.removeFromTop (8);
        auto bar = a.removeFromTop (22).toFloat();
        g.setColour (kLine);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (kAccent.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, lo), bar.getY(),
                                                        std::max (3.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, hi - lo)), bar.getHeight()),
                                4.0f);
        g.setColour (kWarm);
        g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, last) - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f));
        a.removeFromTop (6);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("reached " + juce::String (juce::roundToInt (lo * 100.0f)) + " % to " + juce::String (juce::roundToInt (hi * 100.0f))
                        + " %   (the shaded part is used as the whole range)",
                    a.removeFromTop (18), juce::Justification::centredLeft);
        return;
    }
    // summary
    const auto c = proc.calibration();
    g.setFont (juce::FontOptions (13.0f));
    auto line = [&] (const juce::String& k, const juce::String& v) {
        auto r = a.removeFromTop (24);
        g.setColour (kDim);
        g.drawText (k, r.removeFromLeft (170), juce::Justification::centredLeft);
        g.setColour (kText);
        g.drawText (v, r, juce::Justification::centredLeft);
    };
    auto pc = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    line ("Pitch slider", (c.pitchCc >= 0 ? sourceName (c.pitchCc) : juce::String ("Pitch wheel")) + ",  " + juce::String (c.pitchLo, 2) + " to "
                              + juce::String (c.pitchHi, 2));
    line ("Mod slider", sourceName (c.modCc) + ",  " + pc (c.modLo) + " to " + pc (c.modHi));
    line ("Expression pedal", (c.exprCc >= 0 ? sourceName (c.exprCc) : proc.apvts.getParameter ("volcc")->getCurrentValueAsText()) + ",  " + pc (c.exprLo)
                                  + " to " + pc (c.exprHi));
    line ("Aftertouch", "full vibrato at " + pc (c.atHi));
    line ("Velocity", pc (c.velLo) + " to " + pc (c.velHi) + " is the whole range");
    a.removeFromTop (8);
    g.setColour (kDim);
    g.drawFittedText ("Saved for every Steelwave on this computer. Run it again any time from Settings > Playing.", a.removeFromTop (36),
                      juce::Justification::topLeft, 2);
}

// ==== editor =======================================================================================
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
    paramId = id;
}

SteelwaveEditor::SteelwaveEditor (SteelwaveProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), wizard (p, [this] { showSetup (false); })
{
    setLookAndFeel (&laf);
    addAndMakeVisible (canvas);
    auto& s = proc.apvts;
    auto& c = canvas;

    // --- Play page ---
    mode.setup (c, s, "mode", "MODE  (keyswitch B0: smart notes)");
    voicing.setup (c, s, "voicing", "CHORD VOICING  (C1-G1)");
    scale.setup (c, s, "scale", "SINGLE-NOTE SET  (G#1-B1)");
    key.setup (c, s, "key", "KEY");
    pattern.setup (c, s, "pattern", "PICKING PATTERN");
    patRate.setup (c, s, "patrate", "RATE");
    tone.setup (c, s, "brightness", "TONE");
    pick.setup (c, s, "hardness", "PICK");
    sustain.setup (c, s, "sustain", "SUSTAIN");
    vibDepth.setup (c, s, "vibdepth", "VIBRATO");
    glide.setup (c, s, "glide", "GLIDE");
    melody.setup (c, s, "melody", "MELODY");
    swellMain.setup (c, s, "swellbelow", "SWELL");
    sweeten.setup (c, s, "sweeten", "SWEETEN");
    level.setup (c, s, "level", "LEVEL");
    mode.box.setTooltip ("Smart notes: play ordinary notes and chords and the steel finds them. Chords: one key per chord (the map below shows which). "
                         "Single notes: one key per scale note.");
    voicing.box.setTooltip ("Which strings the chords use: Classic 4-string grips, Full with the bass, High, Low, Close triads, an octave up, Wide, or a big Pad.");
    scale.box.setTooltip ("Single notes: the scale at one bar position (low or high), or every key bending a whole or half step.");
    key.box.setTooltip ("The key you're in: chords and scales are laid out in it, and the pitch wheel bends along its scale.");
    pattern.box.setTooltip ("A picking pattern plays what you hold, string by string, in time with the host: thumb-and-finger rolls, "
                            "pinches, alternating thumb, arpeggios, or a drifting ambient pick. Off: chords are strummed once.");
    patRate.box.setTooltip ("How fast the pattern steps, locked to the host's tempo (on its beat grid while it plays).");
    tone.slider.setTooltip ("How long the high partials ring: dull, old strings to bright, new ones.");
    pick.slider.setTooltip ("Soft thumb and flesh to hard metal fingerpicks.");
    sustain.slider.setTooltip ("How long notes ring.");
    vibDepth.slider.setTooltip ("Depth of the bar vibrato (aftertouch, mod wheel or auto vibrato bring it in).");
    melody.slider.setTooltip ("How much the melody stands out: the top note of what you hold (in Chords, the chord's highest note), "
                              "this much louder than the rest. Even at 0.");
    glide.slider.setTooltip ("How long a move takes: the bend or slide from one chord to the next, and legato slides between notes.");
    swellMain.slider.setTooltip ("Notes played softer than this fade in like a volume-pedal swell. Off at 0; all the way up, every note swells.");
    sweeten.slider.setTooltip ("Tunes thirds and fifths pure against the chord's root, the way steel players tune. 0 is equal temperament.");
    level.slider.setTooltip ("Output level.");

    // --- Settings > Sound ---
    pickup.setup (c, s, "pickup", "PICKUP");
    volcc.setup (c, s, "volcc", "VOLUME PEDAL");
    voltype.setup (c, s, "voltype", "PEDAL TYPE");
    strum.setup (c, s, "strum", "STRUM");
    noise.setup (c, s, "barnoise", "BAR NOISE");
    behind.setup (c, s, "behindbar", "BEHIND BAR");
    vibRate.setup (c, s, "vibrate", "VIB RATE");
    sym.setup (c, s, "sympathetic", "SYMPATHETIC");
    strum.slider.setTooltip ("Chords: the pick spread across a chord, low string first.");
    noise.slider.setTooltip ("Contact noise of the bar sliding over the strings.");
    behind.slider.setTooltip ("How freely the strings ring behind the bar (0: muted by the hand).");
    vibRate.slider.setTooltip ("Speed of the bar vibrato.");
    sym.slider.setTooltip ("Unpicked strings ringing along through the bridge.");
    voltype.box.setTooltip ("A passive volume pedal loses treble part-way down, like the real thing. Active: no tone change.");

    // --- Settings > Playing ---
    pedalKeys.setup (c, s, "pedalkeys", "PEDAL KEYS");
    modWheel.setup (c, s, "modwheel", "MOD WHEEL");
    aftertouch.setup (c, s, "aftertouch", "AFTERTOUCH");
    pedalSpeed.setup (c, s, "pedalspeed", "PEDAL SPEED");
    swellBelow.setup (c, s, "swellbelow", "AUTO SWELL");
    swellTime.setup (c, s, "swelltime", "SWELL TIME");
    autoVib.setup (c, s, "autovib", "AUTO VIBRATO");
    vibDelay.setup (c, s, "vibdelay", "VIB DELAY");
    slideIn.setup (c, s, "slidein", "SLIDE-IN");
    slideFrom.setup (c, s, "slidefrom", "SLIDE FROM");
    slideTime.setup (c, s, "slidetime", "SLIDE TIME");
    glideFeel.setup (c, s, "glidefeel", "GLIDE FEEL");
    barRange.setup (c, s, "barrange", "BAR RANGE");
    timing.setup (c, s, "timing", "TIMING");
    mpe.setup (c, s, "mpe", "MPE");
    bendRange.setup (c, s, "bendrange", "MPE BEND RANGE");
    autoBend.setup (c, s, "autobend", "AUTO BEND");
    legatoSlide.setup (c, s, "legatoslide", "LEGATO SLIDE");
    wheelMode.setup (c, s, "wheel", "PITCH WHEEL");
    chordMove.setup (c, s, "chordmove", "CHORD CHANGES");
    tempSync.setup (c, s, "tempsync", "TEMPO SYNC");
    velCurve.setup (c, s, "velcurve", "VELOCITY CURVE");
    chordMove.box.setTooltip ("How chords get from one to the next. Always move: like a player who keeps the bar down, it bends (pedals and levers) and/or slides "
                              "into the next chord, whichever is the most natural move (I to IV is A+B, I to vi the A pedal, I to II a two-fret slide); a chord "
                              "played soon after the last is picked where the last one was, then pushed into place. Move when legato: only when the keys "
                              "overlap. Re-pick: every chord picked fresh in its own voicing. The Glide knob sets how long a move takes.");
    wheelMode.box.setTooltip ("Melody note: the wheel moves only the top note of what you hold, a scale step up or down, while the rest holds. Chord move: it moves the chord (up I to IV, down I to iii).");
    pedalKeys.box.setTooltip ("The lowest 7 keys of a 61-key board (MIDI 36-42) play A, B, C, LKL, LKR, RKL, RKR: hold them like pedals.");
    pedalSpeed.slider.setTooltip ("How fast the pedal keys and auto bends travel. Hit harder for a faster pedal (about 80-250 ms at 1x).");
    modWheel.box.setTooltip ("What the mod wheel does: vibrato, the A+B pedals (a non-springing strip holds a pedal bend), a bar slide, or nothing.");
    aftertouch.box.setTooltip ("Pressing into the keys shakes the bar (vibrato depth).");
    swellBelow.slider.setTooltip ("Notes played softer than this fade in like a volume-pedal swell; harder notes pick as usual. Off at 0.");
    swellTime.slider.setTooltip ("How long an auto swell takes to open.");
    autoVib.slider.setTooltip ("Vibrato that comes in on its own once a note has been held.");
    vibDelay.slider.setTooltip ("How long a note is held before auto vibrato starts.");
    slideIn.slider.setTooltip ("Notes softer than this slide up into pitch after they're picked (all the way up: every note). Off at 0.");
    slideFrom.slider.setTooltip ("How far below the note a slide-in starts, in semitones.");
    slideTime.slider.setTooltip ("How long a slide-in takes.");
    glideFeel.slider.setTooltip ("Legato slides take longer for bigger jumps and less for harder playing (0: always the Glide time).");
    barRange.slider.setTooltip ("How far the mod wheel slides the bar when Mod wheel is set to Bar slide.");
    timing.box.setTooltip ("Tight: the picks let go sooner and chords strum tighter, for playing live. Natural: as modelled.");
    mpe.box.setTooltip ("MPE controllers: on channels 2-16 each finger's pitch slide, pressure and slide (CC74) act on its own note.");
    bendRange.box.setTooltip ("Pitch bend range of the MPE member channels: match your controller (48 is the MPE default).");
    autoBend.box.setTooltip ("Smart notes: a note played a half or whole step from one you're holding bends that string to it, like a pedal. Let it go and it bends back.");
    legatoSlide.box.setTooltip ("Smart notes: a single line played legato slides along its string instead of picking each note.");
    tempSync.box.setTooltip ("On: vibrato rate, swell time, glide, slide time and vibrato delay snap to the nearest note length at the host's tempo.");
    velCurve.slider.setTooltip ("How your keys' velocity becomes pick strength: Light, a soft touch picks hard; Heavy, it takes a hard hit. "
                                "Controller setup finds the range your keys really reach.");
    styleButton (setupButton, kAccent.darker (0.6f));
    setupButton.setTooltip ("Find your pitch slider, mod slider, expression pedal, aftertouch and velocity range, and fit Steelwave to them.");
    setupButton.onClick = [this] { showSetup (true); };
    c.addChildComponent (setupButton);

    // --- Settings > Model ---
    for (const auto& a : swadv::params())
    {
        auto k = std::make_unique<Knob>();
        k->setup (c, s, a.id, a.label);
        k->slider.setTooltip (juce::String (a.name) + ": " + a.help);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
        advKnobs.push_back (std::move (k));
    }
    styleButton (resetAdv, kPanel2);
    resetAdv.setTooltip ("Put every Model knob back to the calibrated value (12 o'clock).");
    resetAdv.onClick = [this] {
        for (const auto& a : swadv::params())
            if (auto* prm = proc.apvts.getParameter (a.id))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->getDefaultValue());
                prm->endChangeGesture();
            }
    };
    c.addChildComponent (resetAdv);

    // --- header ---
    for (auto* b : { &playTab, &settingsTab, &soundTab, &ctlTab, &advTab })
    {
        styleButton (*b, kAccent.darker (0.6f));
        c.addAndMakeVisible (*b);
    }
    playTab.onClick = [this] { showPage (PlayPage); };
    settingsTab.onClick = [this] { showPage (page == PlayPage ? SoundPage : page); };
    soundTab.onClick = [this] { showPage (SoundPage); };
    ctlTab.onClick = [this] { showPage (ControlsPage); };
    advTab.onClick = [this] { showPage (AdvancedPage); };

    for (auto* b : { &prevPreset, &nextPreset, &presetButton, &favButton, &abCopy, &auditionButton })
    {
        styleButton (*b, kPanel2);
        c.addAndMakeVisible (*b);
    }
    for (auto* b : { &abA, &abB })
    {
        styleButton (*b, kAccent.darker (0.5f));
        c.addAndMakeVisible (*b);
    }
    presetButton.setTooltip ("Presets: favourites, factory sounds by category, and your own. They set the sound and feel, not your rig "
                             "(mode, key, controller setup and level stay).");
    presetButton.onClick = [this] { presetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    favButton.setTooltip ("Favourite this preset: favourites come first in the preset menu.");
    favButton.onClick = [this] {
        proc.toggleFavourite (proc.presetName());
        updateHeader();
    };
    abA.setTooltip ("A/B compare: two sounds to flip between. Change one, flip to the other, flip back.");
    abB.setTooltip (abA.getTooltip());
    abA.onClick = [this] { proc.abSwitch (0); updateHeader(); };
    abB.onClick = [this] { proc.abSwitch (1); updateHeader(); };
    abCopy.setTooltip ("Copy this sound to the other side of the A/B.");
    abCopy.onClick = [this] { proc.abCopyToOther(); };
    auditionButton.setTooltip ("Hear the sound: a short phrase in the mode you're in (chords: I IV V I; smart notes: a chord and a bent melody note).");
    auditionButton.onClick = [this] { proc.audition(); };

    learnButton.setClickingTogglesState (true);
    styleButton (learnButton, kWarm.darker (0.3f));
    learnButton.setTooltip ("MIDI learn: click a knob or menu, then move a knob, slider or pedal on your controller. Click MIDI LEARN again when done.");
    learnButton.onClick = [this] {
        learning = learnButton.getToggleState();
        if (! learning)
            proc.learnTarget = -1;
        clearLearn.setVisible (learning);
        auditionButton.setVisible (! learning);
        canvas.repaint();
    };
    c.addAndMakeVisible (learnButton);
    styleButton (clearLearn, kPanel2);
    clearLearn.setTooltip ("Forget every MIDI learn assignment.");
    clearLearn.onClick = [this] { proc.clearMidiMap(); canvas.repaint(); };
    c.addChildComponent (clearLearn);

    for (auto* ch : std::initializer_list<Choice*> { &mode, &voicing, &scale, &key, &pattern, &patRate, &pickup, &volcc, &voltype, &pedalKeys, &modWheel,
                                                     &aftertouch, &timing, &mpe, &bendRange, &autoBend, &legatoSlide, &wheelMode, &chordMove, &tempSync })
        registerControl (ch->box, ch->paramId);
    const std::pair<Knob*, const char*> knobs[] = {
        { &tone, "brightness" }, { &pick, "hardness" }, { &sustain, "sustain" }, { &vibDepth, "vibdepth" }, { &glide, "glide" }, { &melody, "melody" },
        { &swellMain, "swellbelow" }, { &sweeten, "sweeten" }, { &level, "level" }, { &strum, "strum" }, { &noise, "barnoise" },
        { &behind, "behindbar" }, { &vibRate, "vibrate" }, { &sym, "sympathetic" }, { &pedalSpeed, "pedalspeed" },
        { &swellBelow, "swellbelow" }, { &swellTime, "swelltime" }, { &autoVib, "autovib" }, { &vibDelay, "vibdelay" },
        { &slideIn, "slidein" }, { &slideFrom, "slidefrom" }, { &slideTime, "slidetime" }, { &glideFeel, "glidefeel" }, { &barRange, "barrange" },
        { &velCurve, "velcurve" } };
    for (auto& [k, id] : knobs)
        registerControl (k->slider, id);
    for (size_t i = 0; i < advKnobs.size(); ++i)
        registerControl (advKnobs[i]->slider, swadv::params()[i].id);

    c.addChildComponent (wizard);

    // resizable: the layout scales with the window
    setResizable (true, true);
    setResizeLimits (kW * 4 / 5, kH * 4 / 5, kW * 3 / 2, kH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    canvas.setBounds (0, 0, kW, kH);
    layout();
    setSize (kW, kH);
    showPage (PlayPage);
    updateHeader();
    startTimerHz (20);
    // first time on this computer: offer controller setup
    if (! proc.calibration().asked)
        showSetup (true);
}

SteelwaveEditor::~SteelwaveEditor()
{
    stopTimer();
    proc.wizardStep = -1;
    setLookAndFeel (nullptr);
}

void SteelwaveEditor::showSetup (bool show)
{
    if (show)
    {
        wizard.setBounds (0, 0, kW, kH);
        wizard.begin();
        wizard.setVisible (true);
        wizard.toFront (true);
    }
    else
    {
        proc.wizardStep = -1;
        wizard.setVisible (false);
    }
}

void SteelwaveEditor::timerCallback()
{
    if (wizard.isVisible())
        wizard.tick();
    if (page == PlayPage)
    {
        canvas.repaint (neckArea);
        canvas.repaint (mapArea);
        canvas.repaint (statusArea);
        const int m = (int) proc.apvts.getRawParameterValue ("mode")->load();
        if (m != lastMode)
            showPage (PlayPage); // the voicing / scale menu follows the mode (keyswitches too)
    }
    else
    {
        canvas.repaint (statusArea);
        if (page == ControlsPage)
            canvas.repaint (pedalStrip);
    }
    if (learning)
        canvas.repaint(); // CC labels appear as they're learned
    updateHeader();
}

void SteelwaveEditor::updateHeader()
{
    const auto name = proc.presetName();
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);
    const bool fav = proc.isFavourite (name);
    const juce::String star = juce::String::fromUTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86"); // filled / hollow star
    if (favButton.getButtonText() != star)
    {
        favButton.setButtonText (star);
        favButton.setColour (juce::TextButton::textColourOffId, fav ? kWarm : kDim);
    }
    abA.setToggleState (proc.abActive() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.abActive() == 1, juce::dontSendNotification);
    abCopy.setButtonText (proc.abActive() == 0 ? "A>B" : "B>A");
}

// ---- presets -----------------------------------------------------------------------------------
juce::StringArray SteelwaveEditor::allPresets() const
{
    juce::StringArray a;
    for (const auto& p : swpresets::presets())
        a.add (p.name);
    a.addArray (proc.userPresetNames());
    return a;
}

void SteelwaveEditor::stepPreset (int dir)
{
    const auto all = allPresets();
    const int nFactory = (int) swpresets::presets().size(), n = all.size();
    if (n == 0)
        return;
    int i = proc.userPreset.isNotEmpty() ? all.indexOf (proc.userPreset, false, nFactory) : proc.currentProgram.load();
    i = ((juce::jmax (i, 0) + dir) % n + n) % n;
    if (i < nFactory)
        proc.setCurrentProgram (i);
    else
        proc.loadUserPreset (all[i]);
}

void SteelwaveEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Saves the sound and feel as one of your own presets.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.userPreset.isNotEmpty() ? proc.userPreset : juce::String ("My steel"), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<SteelwaveEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result) {
        if (result == 1 && safe != nullptr)
            safe->proc.saveUserPreset (w->getTextEditorContents ("name"));
    }), true);
}

void SteelwaveEditor::presetMenu()
{
    juce::PopupMenu m;
    const auto& fac = swpresets::presets();
    const int current = proc.userPreset.isEmpty() ? proc.currentProgram.load() : -1;
    const auto user = proc.userPresetNames();
    // favourites first: factory ones by index, the user's by name
    const auto favs = proc.favourites();
    juce::PopupMenu favMenu;
    for (size_t i = 0; i < fac.size(); ++i)
        if (favs.contains (fac[i].name))
            favMenu.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
    for (int i = 0; i < user.size(); ++i)
        if (favs.contains (user[i]))
            favMenu.addItem (1000 + i, user[i], true, user[i] == proc.userPreset);
    m.addSubMenu (juce::String::fromUTF8 ("\xe2\x98\x85 Favourites"), favMenu, favMenu.getNumItems() > 0);
    m.addSeparator();
    juce::StringArray cats;
    for (const auto& p : fac)
        cats.addIfNotAlreadyThere (p.category);
    for (const auto& cat : cats)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < fac.size(); ++i)
            if (cat == fac[i].category)
                sub.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
        m.addSubMenu (cat, sub);
    }
    juce::PopupMenu mine;
    for (int i = 0; i < user.size(); ++i)
        mine.addItem (1000 + i, user[i], true, user[i] == proc.userPreset);
    m.addSubMenu ("My presets", mine, user.size() > 0);
    m.addSeparator();
    m.addItem (5000, "Save as...");
    m.addItem (5001, "Delete \"" + proc.userPreset + "\"", proc.userPreset.isNotEmpty());
    juce::Component::SafePointer<SteelwaveEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [safe, user] (int r) {
        if (safe == nullptr || r == 0)
            return;
        if (r == 5000)
            safe->savePresetDialog();
        else if (r == 5001)
            safe->proc.deleteUserPreset (safe->proc.userPreset);
        else if (r >= 1000)
            safe->proc.loadUserPreset (user[r - 1000]);
        else
            safe->proc.setCurrentProgram (r - 1);
        safe->updateHeader();
    });
}

// ---- pages ---------------------------------------------------------------------------------------
void SteelwaveEditor::showPage (int p)
{
    page = p;
    const bool play = p == PlayPage, sound = p == SoundPage, ctl = p == ControlsPage, adv = p == AdvancedPage;
    lastMode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    playTab.setToggleState (play, juce::dontSendNotification);
    settingsTab.setToggleState (! play, juce::dontSendNotification);
    soundTab.setToggleState (sound, juce::dontSendNotification);
    ctlTab.setToggleState (ctl, juce::dontSendNotification);
    advTab.setToggleState (adv, juce::dontSendNotification);
    for (auto* b : { &soundTab, &ctlTab, &advTab })
        b->setVisible (! play);
    for (auto* ch : { &mode, &key, &pattern, &patRate })
        ch->show (play);
    voicing.show (play && lastMode == 1);
    scale.show (play && lastMode == 2);
    for (auto* k : { &tone, &pick, &sustain, &vibDepth, &glide, &melody, &swellMain, &sweeten, &level })
        show (*k, play);
    for (auto* ch : { &pickup, &volcc, &voltype })
        ch->show (sound);
    for (auto* k : { &strum, &noise, &behind, &vibRate, &sym })
        show (*k, sound);
    for (auto* ch : { &pedalKeys, &modWheel, &aftertouch, &timing, &mpe, &bendRange, &autoBend, &legatoSlide, &wheelMode, &chordMove, &tempSync })
        ch->show (ctl);
    for (auto* k : { &pedalSpeed, &swellBelow, &swellTime, &autoVib, &vibDelay, &slideIn, &slideFrom, &slideTime, &glideFeel, &barRange, &velCurve })
        show (*k, ctl);
    setupButton.setVisible (ctl);
    for (auto& k : advKnobs)
        show (*k, adv);
    resetAdv.setVisible (adv);
    canvas.repaint();
}

void SteelwaveEditor::resized()
{
    const float sc = (float) getWidth() / (float) kW;
    canvas.setTransform (juce::AffineTransform::scale (sc));
}

void SteelwaveEditor::layout()
{
    auto r = juce::Rectangle<int> (0, 0, kW, kH).reduced (14);
    auto header = r.removeFromTop (34);
    settingsTab.setBounds (header.removeFromRight (92).reduced (0, 4));
    header.removeFromRight (6);
    playTab.setBounds (header.removeFromRight (64).reduced (0, 4));
    header.removeFromRight (8);
    statusArea = header.removeFromRight (104);
    header.removeFromLeft (112); // title
    prevPreset.setBounds (header.removeFromLeft (26).reduced (0, 4));
    header.removeFromLeft (3);
    presetButton.setBounds (header.removeFromLeft (190).reduced (0, 4));
    header.removeFromLeft (3);
    nextPreset.setBounds (header.removeFromLeft (26).reduced (0, 4));
    header.removeFromLeft (3);
    favButton.setBounds (header.removeFromLeft (30).reduced (0, 4));
    header.removeFromLeft (10);
    abA.setBounds (header.removeFromLeft (28).reduced (0, 4));
    header.removeFromLeft (2);
    abB.setBounds (header.removeFromLeft (28).reduced (0, 4));
    header.removeFromLeft (2);
    abCopy.setBounds (header.removeFromLeft (40).reduced (0, 4));
    header.removeFromLeft (10);
    learnButton.setBounds (header.removeFromLeft (96).reduced (0, 4));
    header.removeFromLeft (6);
    auditionButton.setBounds (header.removeFromLeft (72).reduced (0, 4));
    clearLearn.setBounds (auditionButton.getBounds());
    r.removeFromTop (6);

    auto placeChoice = [] (Choice& c, juce::Rectangle<int> cell) {
        c.label.setBounds (cell.removeFromTop (14));
        c.box.setBounds (cell.reduced (0, 1));
    };
    auto placeKnob = [] (Knob& k, juce::Rectangle<int> cell, int labelH = 16) {
        k.label.setBounds (cell.removeFromTop (labelH));
        k.slider.setBounds (cell);
    };

    // --- Play: mode, voicing/scale, key, pattern; eight knobs; the neck; the keyboard map ---
    {
        auto a = r;
        auto row = a.removeFromTop (42);
        placeChoice (mode, row.removeFromLeft (200).withTrimmedRight (10));
        const auto vs = row.removeFromLeft (230).withTrimmedRight (10);
        placeChoice (voicing, vs);
        placeChoice (scale, vs);
        modeHint = vs;
        placeChoice (key, row.removeFromLeft (90).withTrimmedRight (10));
        row.removeFromLeft (30);
        placeChoice (pattern, row.removeFromLeft (220).withTrimmedRight (10));
        placeChoice (patRate, row.removeFromLeft (140));
        a.removeFromTop (8);
        auto knobs = a.removeFromTop (118);
        const int kw = knobs.getWidth() / 9;
        for (auto* k : { &tone, &pick, &sustain, &vibDepth, &glide, &melody, &swellMain, &sweeten, &level })
            placeKnob (*k, knobs.removeFromLeft (kw).reduced (6, 0));
        a.removeFromTop (6);
        mapArea = a.removeFromBottom (122);
        a.removeFromBottom (6);
        neckArea = a;
    }

    // --- Settings: sub-tabs, then the page ---
    auto tabs = r.removeFromTop (30);
    soundTab.setBounds (tabs.removeFromLeft (90).reduced (0, 3));
    tabs.removeFromLeft (6);
    ctlTab.setBounds (tabs.removeFromLeft (90).reduced (0, 3));
    tabs.removeFromLeft (6);
    advTab.setBounds (tabs.removeFromLeft (90).reduced (0, 3));
    resetAdv.setBounds (tabs.removeFromRight (150).reduced (0, 3));
    r.removeFromTop (6);

    { // Sound
        auto a = r;
        auto row = a.removeFromTop (130).reduced (0, 3);
        soundAreas[0] = row;
        row.removeFromLeft (110);
        const int kw = row.getWidth() / 5;
        for (auto* k : { &strum, &noise, &behind, &vibRate, &sym })
            placeKnob (*k, row.removeFromLeft (kw).reduced (8, 6));
        row = a.removeFromTop (84).reduced (0, 3);
        soundAreas[1] = row;
        row.removeFromLeft (110);
        const int cw = row.getWidth() / 3;
        for (auto* c : { &pickup, &volcc, &voltype })
            placeChoice (*c, row.removeFromLeft (cw).reduced (8, 0).withSizeKeepingCentre (cw - 16, 44));
    }
    { // Playing
        auto a = r;
        const int rowH = 84;
        for (int i = 0; i < 5; ++i)
        {
            auto row = a.removeFromTop (rowH).reduced (0, 3);
            ctlAreas[(size_t) i] = row;
            row.removeFromLeft (110);
            const int cw = row.getWidth() / 5;
            auto knobCell = [&] (Knob& k) { placeKnob (k, row.removeFromLeft (cw).reduced (4, 2), 14); };
            auto choiceCell = [&] (Choice& c) { placeChoice (c, row.removeFromLeft (cw).reduced (6, 0).withSizeKeepingCentre (cw - 12, 44)); };
            if (i == 0)
            {
                choiceCell (autoBend);
                choiceCell (legatoSlide);
                choiceCell (pedalKeys);
                knobCell (pedalSpeed);
                choiceCell (modWheel);
            }
            else if (i == 1)
            {
                knobCell (swellBelow);
                knobCell (swellTime);
                knobCell (autoVib);
                knobCell (vibDelay);
                choiceCell (aftertouch);
            }
            else if (i == 2)
            {
                knobCell (slideIn);
                knobCell (slideFrom);
                knobCell (slideTime);
                knobCell (glideFeel);
                knobCell (barRange);
            }
            else if (i == 3)
            {
                choiceCell (chordMove);
                choiceCell (wheelMode);
                choiceCell (timing);
                choiceCell (mpe);
                choiceCell (bendRange);
            }
            else
            {
                knobCell (velCurve);
                choiceCell (tempSync);
                setupButton.setBounds (row.removeFromLeft (cw * 2).reduced (16, 0).withSizeKeepingCentre (cw * 2 - 32, 34));
            }
        }
        a.removeFromTop (3);
        ctlAreas[5] = a;
        pedalStrip = a.reduced (14, 0).removeFromTop (28).translated (0, 24);
    }
    { // Model: four groups, one row each
        auto a = r;
        const int rowH = a.getHeight() / swadv::kGroups;
        for (int g = 0; g < swadv::kGroups; ++g)
        {
            auto row = a.removeFromTop (rowH).reduced (0, 3);
            groupAreas[(size_t) g] = row;
            row.removeFromLeft (96);
            const int kw = row.getWidth() / 7;
            const auto& ps = swadv::params();
            for (size_t i = 0; i < ps.size(); ++i)
                if (ps[i].group == g)
                    placeKnob (*advKnobs[i], row.removeFromLeft (kw).reduced (2, 2), 14);
        }
    }
    wizard.setBounds (0, 0, kW, kH);
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

void SteelwaveEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("STEELWAVE", 14, 14, 110, 34, juce::Justification::centredLeft);

    // CPU and tempo
    {
        const float cpu = proc.uiCpu.load();
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (cpu > 0.7f ? kWarm : kDim);
        auto st = statusArea;
        g.drawText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0f)) + " %", st.removeFromTop (st.getHeight() / 2), juce::Justification::centredRight);
        g.setColour (proc.uiPlaying.load() ? kAccent : kDim);
        g.drawText (juce::String (juce::roundToInt (proc.uiBpm.load())) + " BPM" + (proc.uiPlaying.load() ? juce::String::fromUTF8 (" \xe2\x96\xb6") : ""),
                    st, juce::Justification::centredRight);
    }

    auto panel = [&] (juce::Rectangle<int> a, const juce::String& title, bool side) {
        g.setColour (kPanel);
        g.fillRoundedRectangle (a.toFloat(), 8.0f);
        g.setColour (kAccent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        if (side)
            g.drawFittedText (title, a.withWidth (106).reduced (12, 0), juce::Justification::centredLeft, 2);
        else
            g.drawText (title, a.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft);
    };

    if (page == SoundPage)
    {
        panel (soundAreas[0], "STRINGS & BAR", true);
        panel (soundAreas[1], "PICKUP & PEDAL", true);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("The output is a dry, DI-style signal: put your own amp and effects after it. Tone, Pick, Sustain and Vibrato are on the Play page; "
                          "the physical model's calibration is under Model.",
                          soundAreas[1].translated (0, soundAreas[1].getHeight() + 10).reduced (14, 0).withHeight (40), juce::Justification::topLeft, 3);
        return;
    }
    if (page == ControlsPage)
    {
        const char* titles[] = { "SMART NOTES & PEDALS", "SWELL & VIBRATO", "SLIDES", "CHORDS, WHEEL & MPE", "TOUCH & TEMPO", "WHAT DOES WHAT" };
        for (int i = 0; i < 6; ++i)
            panel (ctlAreas[(size_t) i], titles[i], i < 5);
        drawPedals (g, pedalStrip);
        auto t = ctlAreas[5].reduced (14, 6);
        t.removeFromTop (16 + 8 + 28 + 6);
        const bool keys = (int) proc.apvts.getRawParameterValue ("pedalkeys")->load() == 1;
        const int mw = (int) proc.apvts.getRawParameterValue ("modwheel")->load();
        const juce::String mwText[] = { "vibrato", "the A+B pedals",
                                        "a bar slide (up to " + juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("barrange")->load())) + " frets)",
                                        "nothing" };
        const bool at = (int) proc.apvts.getRawParameterValue ("aftertouch")->load() == 0;
        const bool melody = (int) proc.apvts.getRawParameterValue ("wheel")->load() == 0;
        juce::String txt;
        txt << (keys ? "Pedal keys: MIDI 36-42 are A  B  C  LKL  LKR  RKL  RKR. Hold them like pedals; hit harder for a faster pedal.\n"
                     : "Pedal keys are off: MIDI 36-42 play notes. CC20-26 still work the pedals and levers.\n")
            << (melody ? "Pitch wheel: the top note moves a scale step up or down; the rest holds.   Mod wheel: "
                       : "Pitch wheel: moves the chord along the key's scale (up I to IV, down I to iii).   Mod wheel: ") << mwText[juce::jlimit (0, 3, mw)] << ".\n"
            << "Aftertouch: " << (at ? "vibrato" : "off") << ".   Expression pedal: the volume pedal.   Sustain: let strings ring.\n"
            << "Keyswitches: B0 smart notes, C1-G1 chord voicings, G#1-B1 single-note sets.";
        g.setColour (kText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (txt, t, juce::Justification::topLeft, 5);
        return;
    }
    if (page == AdvancedPage)
    {
        for (int gi = 0; gi < swadv::kGroups; ++gi)
            panel (groupAreas[(size_t) gi], swadv::groupName (gi), true);
        return;
    }
    if (lastMode == 0)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Play notes and chords as you would on a piano: the steel finds the strings, bends and slides.",
                          modeHint.withTrimmedTop (14), juce::Justification::centredLeft, 2);
    }
    drawNeck (g);
    drawChordMap (g, mapArea);
}

// ---- Play: the neck (10 strings, the bar, what is ringing) and what's happening ----------------------
void SteelwaveEditor::drawNeck (juce::Graphics& g)
{
    const int setIdx = proc.uiSet.load();
    const int keyPc = (int) proc.apvts.getRawParameterValue ("key")->load();
    g.setColour (kPanel);
    g.fillRoundedRectangle (neckArea.toFloat(), 8.0f);
    auto a = neckArea.reduced (14, 10);
    const int slot = proc.uiSlot.load(), bar = proc.uiBar.load();
    const bool smart = setIdx == sw::kSmartSet, chords = setIdx < sw::kChordSets;
    const bool melodyWheel = (int) proc.apvts.getRawParameterValue ("wheel")->load() == 0;

    auto info = a.removeFromRight (std::min (320, a.getWidth() / 3));
    a.removeFromRight (12);
    const int sounding = proc.uiSounding.load();
    const float rowH = (float) a.getHeight() / (float) sw::kStrings;
    const float x0 = (float) a.getX() + 64.0f, x1 = (float) a.getRight() - 40.0f;
    auto fretX = [&] (float semis) { // fret spacing as on the instrument (24.5" scale; the bar can go 2 past the 24th)
        const float L = 1.0f - std::pow (2.0f, -semis / 12.0f);
        const float Lmax = 1.0f - std::pow (2.0f, -(float) sw::kTopFret / 12.0f);
        return x0 + (x1 - x0) * juce::jlimit (0.0f, 1.0f, L / Lmax);
    };
    g.setColour (kLine);
    for (int f = 0; f <= sw::kTopFret; ++f)
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
            g.drawText (juce::String (kNoteNames[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1), (int) x1 + 8, (int) (y - 7), 34, 14,
                        juce::Justification::centredLeft);
        }
    }
    const float bx = fretX ((float) bar);
    g.setColour (kWarm.withAlpha (0.85f));
    g.fillRoundedRectangle (bx - 4.0f, (float) a.getY() - 2.0f, 8.0f, (float) a.getHeight() - 8.0f, 4.0f);

    // ---- what is playing ---------------------------------------------------------------------
    g.setColour (kDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (smart ? "SMART NOTES" : chords ? "CHORD" : "SINGLE NOTES", info.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (kText);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    juce::String title = "-", sub, how;
    if (smart)
    {
        const int root = proc.uiSmartRoot.load(), q = proc.uiSmartQual.load();
        if (root >= 0)
            title = juce::String (kNoteNames[root]) + (q == 0 ? "" : q == 1 ? "m" : "dim");
        sub = "bar " + juce::String (bar) + "  |  key of " + kNoteNames[keyPc];
        how = juce::String ("a step from a held note: that string bends\na legato line slides  |  wheel: ") + (melodyWheel ? "top note, a scale step" : "up I to IV, down I to iii");
    }
    else if (chords && slot >= 0)
    {
        const auto& sl = sw::chordSlots()[(size_t) slot];
        title = chordName (sl.rest, keyPc);
        sub = juce::String (kSlotNames[slot]) + "  |  bar " + juce::String (bar);
        how = "last change: " + moveText (proc.uiMoveCombo.load(), proc.uiMoveFrets.load()) + "\n"
              + (melodyWheel ? juce::String ("wheel: the top note moves a scale step")
                             : "wheel up: " + chordName (sl.up, keyPc) + ", down: " + chordName (sl.down, keyPc));
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
    info.removeFromTop (4);
    g.setColour (kText);
    g.drawFittedText (how, info.removeFromTop (32), juce::Justification::topLeft, 2);
    // the picking pattern
    const int pat = (int) proc.apvts.getRawParameterValue ("pattern")->load();
    if (pat > 0 && (smart || chords))
    {
        const int stepNow = proc.uiPatternStep.load();
        auto pr = info.removeFromTop (18);
        g.setColour (kAccent);
        g.drawText (juce::String (sw::patternName (pat)) + ", " + sw::patternRateName ((int) proc.apvts.getRawParameterValue ("patrate")->load()),
                    pr.removeFromLeft (pr.getWidth() - 70), juce::Justification::centredLeft);
        for (int i = 0; i < 4; ++i) // a beat light
        {
            const bool lit = stepNow >= 0 && stepNow % 4 == i;
            g.setColour (lit ? kWarm : kLine);
            g.fillEllipse ((float) pr.getX() + 4.0f + 16.0f * (float) i, (float) pr.getCentreY() - 4.0f, 8.0f, 8.0f);
        }
    }
    else
        info.removeFromTop (18);
    info.removeFromTop (4);
    auto meter = [&] (const juce::String& name, float v, bool bipolar) {
        auto rr = info.removeFromTop (18);
        g.setColour (kDim);
        g.drawText (name, rr.removeFromLeft (56), juce::Justification::centredLeft);
        const auto t = rr.toFloat().reduced (0, 5);
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
    drawPedals (g, info.removeFromTop (20)); // pedals and levers down, live
    info.removeFromTop (4);
    meter ("wheel", proc.uiWheel.load(), true);
    meter ("volume", proc.uiVolume.load(), false);
}

// ---- Play: one octave of the keyboard, labelled with what each key plays ----------------------------
void SteelwaveEditor::drawChordMap (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int setIdx = proc.uiSet.load();
    const int keyPc = (int) proc.apvts.getRawParameterValue ("key")->load();
    const bool smart = setIdx == sw::kSmartSet, chords = setIdx < sw::kChordSets;
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto a = area.reduced (14, 8);
    auto head = a.removeFromLeft (118);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("KEYBOARD", head.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText (chords ? "Each chord is voiced from the last, the way the keys went: down the keyboard, down the neck."
                      : smart ? "Play real notes. Lit: the chord you hold. Dots: the key's scale."
                              : "White keys: the key's scale. Black keys: the note below, unbent.",
                      head.withTrimmedRight (8), juce::Justification::topLeft, 5);

    const int slot = proc.uiSlot.load(), smartRoot = proc.uiSmartRoot.load();
    const float ww = (float) a.getWidth() / 7.0f;
    const int whitePc[] = { 0, 2, 4, 5, 7, 9, 11 };
    const int blackPc[] = { 1, 3, -1, 6, 8, 10 }; // after C, D, (E), F, G, A
    auto inScale = [&] (int pc) {
        for (int d : kMajor)
            if ((keyPc + d) % 12 == pc)
                return true;
        return false;
    };
    auto labelFor = [&] (int pc, juce::String& l1, juce::String& l2, juce::String& l3, bool& lit) {
        lit = false;
        if (chords)
        {
            const auto& sl = sw::chordSlots()[(size_t) pc];
            const auto& v = sw::chordSets()[(size_t) setIdx][(size_t) pc];
            l1 = chordName (sl.rest, keyPc);
            l2 = kSlotNames[pc];
            l3 = juce::String (v.restCombo == 0 ? "open" : sw::comboName (v.restCombo)) + "  fr " + juce::String (sw::chordBar (setIdx, v, keyPc, 60 + pc));
            lit = pc == slot;
        }
        else if (smart)
        {
            l1 = kNoteNames[pc];
            l2 = inScale (pc) ? juce::String::fromUTF8 ("\xe2\x80\xa2") : juce::String();
            lit = pc == smartRoot;
        }
        else
        {
            // single notes: the white key's scale degree in the key; black keys repeat the white key below, unbent
            int deg = -1;
            for (int i = 0; i < 7; ++i)
                if (whitePc[i] == pc)
                    deg = i;
            if (deg >= 0)
            {
                const int n = (keyPc + kMajor[deg]) % 12, up = (keyPc + kMajor[(deg + 1) % 7]) % 12;
                l1 = kNoteNames[n];
                l2 = juce::String (deg + 1);
                const int fixed = sw::singleSetDefs()[(size_t) juce::jlimit (0, sw::kSingleSets - 1, setIdx - sw::kChordSets)].fixedBend;
                l3 = fixed > 0 ? "bends +" + juce::String (fixed) : juce::String ("bends to ") + kNoteNames[up];
            }
            else
            {
                l1 = "hold";
                l2 = juce::String();
            }
        }
    };
    // white keys
    for (int i = 0; i < 7; ++i)
    {
        const auto k = juce::Rectangle<float> ((float) a.getX() + ww * (float) i, (float) a.getY(), ww, (float) a.getHeight()).reduced (1.5f, 0.0f);
        juce::String l1, l2, l3;
        bool lit = false;
        labelFor (whitePc[i], l1, l2, l3, lit);
        g.setColour (lit ? kAccent : kText.withAlpha (0.9f));
        g.fillRoundedRectangle (k, 4.0f);
        auto t = k.toNearestInt().reduced (4, 4);
        t.removeFromTop (t.getHeight() / 2);
        g.setColour (kBg);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (l1, t.removeFromTop (17), juce::Justification::centred);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (l2, t.removeFromTop (13), juce::Justification::centred);
        g.setFont (juce::FontOptions (9.5f));
        g.setColour (kBg.withAlpha (0.75f));
        g.drawFittedText (l3, t, juce::Justification::centredTop, 1);
    }
    // black keys
    for (int i = 0; i < 6; ++i)
    {
        if (blackPc[i] < 0)
            continue;
        const float bw = ww * 0.62f;
        const auto k = juce::Rectangle<float> ((float) a.getX() + ww * (float) (i + 1) - bw * 0.5f, (float) a.getY(), bw, (float) a.getHeight() * 0.5f);
        juce::String l1, l2, l3;
        bool lit = false;
        labelFor (blackPc[i], l1, l2, l3, lit);
        g.setColour (lit ? kAccent : kPanel2.darker (0.5f));
        g.fillRoundedRectangle (k, 4.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (k, 4.0f, 1.0f);
        auto t = k.toNearestInt().reduced (2, 3);
        g.setColour (lit ? kBg : kText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (l1, t.removeFromTop (15), juce::Justification::centred);
        g.setFont (juce::FontOptions (9.5f));
        g.setColour (lit ? kBg : kDim);
        g.drawText (l2, t.removeFromTop (12), juce::Justification::centred);
        g.drawFittedText (l3, t, juce::Justification::centredTop, 1);
    }
}

// MIDI learn: click a control to pick it, then move a control on the keyboard
void SteelwaveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! learning)
        return;
    for (auto* c = e.eventComponent; c != nullptr && c != &canvas; c = c->getParentComponent())
        if (auto it = controlParam.find (c); it != controlParam.end())
        {
            proc.learnTarget = proc.paramIndex (it->second);
            canvas.repaint();
            return;
        }
}

void SteelwaveEditor::paintOverCanvas (juce::Graphics& g)
{
    if (! learning || wizard.isVisible())
        return;
    const int target = proc.learnTarget.load();
    for (const auto& [comp, id] : controlParam)
    {
        if (! comp->isVisible())
            continue;
        const int pi = proc.paramIndex (id);
        int cc = -1;
        for (int i = 0; i < 128; ++i)
            if (proc.ccMap[(size_t) i].load() == pi)
                cc = i;
        const auto b = comp->getBounds().toFloat().expanded (3.0f);
        if (pi == target)
        {
            g.setColour (kWarm);
            g.drawRoundedRectangle (b, 5.0f, 2.5f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("move a control", b.withHeight (14.0f).translated (0.0f, -14.0f), juce::Justification::centred);
        }
        else
        {
            g.setColour (cc >= 0 ? kAccent : kLine);
            g.drawRoundedRectangle (b, 5.0f, cc >= 0 ? 2.0f : 1.0f);
        }
        if (cc >= 0)
        {
            const auto tag = juce::Rectangle<float> (b.getRight() - 40.0f, b.getY(), 40.0f, 15.0f);
            g.setColour (kAccent);
            g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (kBg);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("CC " + juce::String (cc), tag, juce::Justification::centred);
        }
    }
}
