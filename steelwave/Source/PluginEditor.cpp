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

void styleButton (juce::TextButton& b, juce::Colour on)
{
    b.setColour (juce::TextButton::buttonColourId, kPanel2);
    b.setColour (juce::TextButton::buttonOnColourId, on);
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
    paramId = id;
}

SteelwaveEditor::SteelwaveEditor (SteelwaveProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    auto& s = proc.apvts;

    // --- Play page ---
    set.setup (*this, s, "set", "MODE  (keyswitch B0: smart notes, C1-B1: sets)");
    key.setup (*this, s, "key", "KEY");
    tone.setup (*this, s, "brightness", "TONE");
    pick.setup (*this, s, "hardness", "PICK");
    sustain.setup (*this, s, "sustain", "SUSTAIN");
    vibDepth.setup (*this, s, "vibdepth", "VIBRATO");
    glide.setup (*this, s, "glide", "GLIDE");
    swellMain.setup (*this, s, "swellbelow", "SWELL");
    sweeten.setup (*this, s, "sweeten", "SWEETEN");
    level.setup (*this, s, "level", "LEVEL");
    set.box.setTooltip ("Smart notes: play ordinary notes and chords and the steel finds them. Chord and single-note sets: one key per chord or scale note.");
    key.box.setTooltip ("The key you're in: the pitch wheel bends along its scale, and the sets are laid out in it.");
    tone.slider.setTooltip ("How long the high partials ring: dull, old strings to bright, new ones.");
    pick.slider.setTooltip ("Soft thumb and flesh to hard metal fingerpicks.");
    sustain.slider.setTooltip ("How long notes ring.");
    vibDepth.slider.setTooltip ("Depth of the bar vibrato (aftertouch, mod wheel or auto vibrato bring it in).");
    glide.slider.setTooltip ("How long the bar takes to slide between notes played legato.");
    swellMain.slider.setTooltip ("Notes played softer than this fade in like a volume-pedal swell. Off at 0; all the way up, every note swells.");
    sweeten.slider.setTooltip ("Tunes thirds and fifths pure against the chord's root, the way steel players tune. 0 is equal temperament.");
    level.slider.setTooltip ("Output level.");

    // --- Settings > Sound ---
    pickup.setup (*this, s, "pickup", "PICKUP");
    volcc.setup (*this, s, "volcc", "VOLUME PEDAL");
    voltype.setup (*this, s, "voltype", "PEDAL TYPE");
    strum.setup (*this, s, "strum", "STRUM");
    noise.setup (*this, s, "barnoise", "BAR NOISE");
    behind.setup (*this, s, "behindbar", "BEHIND BAR");
    vibRate.setup (*this, s, "vibrate", "VIB RATE");
    sym.setup (*this, s, "sympathetic", "SYMPATHETIC");
    strum.slider.setTooltip ("Chord sets: the pick spread across a chord, low string first.");
    noise.slider.setTooltip ("Contact noise of the bar sliding over the strings.");
    behind.slider.setTooltip ("How freely the strings ring behind the bar (0: muted by the hand).");
    vibRate.slider.setTooltip ("Speed of the bar vibrato.");
    sym.slider.setTooltip ("Unpicked strings ringing along through the bridge.");
    voltype.box.setTooltip ("A passive volume pedal loses treble part-way down, like the real thing. Active: no tone change.");

    // --- Settings > Playing ---
    pedalKeys.setup (*this, s, "pedalkeys", "PEDAL KEYS");
    modWheel.setup (*this, s, "modwheel", "MOD WHEEL");
    aftertouch.setup (*this, s, "aftertouch", "AFTERTOUCH");
    pedalSpeed.setup (*this, s, "pedalspeed", "PEDAL SPEED");
    swellBelow.setup (*this, s, "swellbelow", "AUTO SWELL");
    swellTime.setup (*this, s, "swelltime", "SWELL TIME");
    autoVib.setup (*this, s, "autovib", "AUTO VIBRATO");
    vibDelay.setup (*this, s, "vibdelay", "VIB DELAY");
    slideIn.setup (*this, s, "slidein", "SLIDE-IN");
    slideFrom.setup (*this, s, "slidefrom", "SLIDE FROM");
    slideTime.setup (*this, s, "slidetime", "SLIDE TIME");
    glideFeel.setup (*this, s, "glidefeel", "GLIDE FEEL");
    barRange.setup (*this, s, "barrange", "BAR RANGE");
    timing.setup (*this, s, "timing", "TIMING");
    mpe.setup (*this, s, "mpe", "MPE");
    bendRange.setup (*this, s, "bendrange", "MPE BEND RANGE");
    autoBend.setup (*this, s, "autobend", "AUTO BEND");
    legatoSlide.setup (*this, s, "legatoslide", "LEGATO SLIDE");
    wheelMode.setup (*this, s, "wheel", "PITCH WHEEL");
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

    // --- Settings > Model ---
    for (const auto& a : swadv::params())
    {
        auto k = std::make_unique<Knob>();
        k->setup (*this, s, a.id, a.label);
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
    addChildComponent (resetAdv);

    // --- header ---
    for (auto* b : { &playTab, &settingsTab, &soundTab, &ctlTab, &advTab })
    {
        styleButton (*b, kAccent.darker (0.6f));
        addAndMakeVisible (*b);
    }
    playTab.onClick = [this] { showPage (PlayPage); };
    settingsTab.onClick = [this] { showPage (page == PlayPage ? SoundPage : page); };
    soundTab.onClick = [this] { showPage (SoundPage); };
    ctlTab.onClick = [this] { showPage (ControlsPage); };
    advTab.onClick = [this] { showPage (AdvancedPage); };

    for (auto* b : { &prevPreset, &nextPreset, &presetButton })
    {
        styleButton (*b, kPanel2);
        addAndMakeVisible (*b);
    }
    presetButton.setButtonText (proc.presetName());
    presetButton.setTooltip ("Presets: factory sounds by category, and your own. They set the sound and feel, not your rig (mode, key, controller setup and level stay).");
    presetButton.onClick = [this] { presetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };

    learnButton.setClickingTogglesState (true);
    styleButton (learnButton, kWarm.darker (0.3f));
    learnButton.setTooltip ("MIDI learn: click a knob or menu, then move a knob, slider or pedal on your controller. Click MIDI LEARN again when done.");
    learnButton.onClick = [this] {
        learning = learnButton.getToggleState();
        if (! learning)
            proc.learnTarget = -1;
        clearLearn.setVisible (learning);
        repaint();
    };
    addAndMakeVisible (learnButton);
    styleButton (clearLearn, kPanel2);
    clearLearn.setTooltip ("Forget every MIDI learn assignment.");
    clearLearn.onClick = [this] { proc.clearMidiMap(); repaint(); };
    addChildComponent (clearLearn);

    for (auto* c : std::initializer_list<Choice*> { &set, &key, &pickup, &volcc, &voltype, &pedalKeys, &modWheel, &aftertouch, &timing, &mpe,
                                                    &bendRange, &autoBend, &legatoSlide, &wheelMode })
        registerControl (c->box, c->paramId);
    const std::pair<Knob*, const char*> knobs[] = {
        { &tone, "brightness" }, { &pick, "hardness" }, { &sustain, "sustain" }, { &vibDepth, "vibdepth" }, { &glide, "glide" },
        { &swellMain, "swellbelow" }, { &sweeten, "sweeten" }, { &level, "level" }, { &strum, "strum" }, { &noise, "barnoise" },
        { &behind, "behindbar" }, { &vibRate, "vibrate" }, { &sym, "sympathetic" }, { &pedalSpeed, "pedalspeed" },
        { &swellBelow, "swellbelow" }, { &swellTime, "swelltime" }, { &autoVib, "autovib" }, { &vibDelay, "vibdelay" },
        { &slideIn, "slidein" }, { &slideFrom, "slidefrom" }, { &slideTime, "slidetime" }, { &glideFeel, "glidefeel" }, { &barRange, "barrange" } };
    for (auto& [k, id] : knobs)
        registerControl (k->slider, id);
    for (size_t i = 0; i < advKnobs.size(); ++i)
        registerControl (advKnobs[i]->slider, swadv::params()[i].id);

    setResizable (true, true);
    setResizeLimits (840, 560, 1400, 933);
    getConstrainer()->setFixedAspectRatio (900.0 / 600.0);
    setSize (900, 600);
    showPage (PlayPage);
    startTimerHz (20);
}

SteelwaveEditor::~SteelwaveEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SteelwaveEditor::timerCallback()
{
    if (page == PlayPage)
        repaint (neckArea);
    else if (page == ControlsPage)
        repaint (pedalStrip);
    if (learning)
        repaint(); // CC labels appear as they're learned
    const auto name = proc.presetName();
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);
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
    juce::StringArray cats;
    for (const auto& p : fac)
        cats.addIfNotAlreadyThere (p.category);
    const int current = proc.userPreset.isEmpty() ? proc.currentProgram.load() : -1;
    for (const auto& cat : cats)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < fac.size(); ++i)
            if (cat == fac[i].category)
                sub.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
        m.addSubMenu (cat, sub);
    }
    const auto user = proc.userPresetNames();
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
    });
}

// ---- pages ---------------------------------------------------------------------------------------
void SteelwaveEditor::showPage (int p)
{
    page = p;
    const bool play = p == PlayPage, sound = p == SoundPage, ctl = p == ControlsPage, adv = p == AdvancedPage;
    playTab.setToggleState (play, juce::dontSendNotification);
    settingsTab.setToggleState (! play, juce::dontSendNotification);
    soundTab.setToggleState (sound, juce::dontSendNotification);
    ctlTab.setToggleState (ctl, juce::dontSendNotification);
    advTab.setToggleState (adv, juce::dontSendNotification);
    for (auto* b : { &soundTab, &ctlTab, &advTab })
        b->setVisible (! play);
    for (auto* c : { &set, &key })
        c->show (play);
    for (auto* k : { &tone, &pick, &sustain, &vibDepth, &glide, &swellMain, &sweeten, &level })
        show (*k, play);
    for (auto* c : { &pickup, &volcc, &voltype })
        c->show (sound);
    for (auto* k : { &strum, &noise, &behind, &vibRate, &sym })
        show (*k, sound);
    for (auto* c : { &pedalKeys, &modWheel, &aftertouch, &timing, &mpe, &bendRange, &autoBend, &legatoSlide, &wheelMode })
        c->show (ctl);
    for (auto* k : { &pedalSpeed, &swellBelow, &swellTime, &autoVib, &vibDelay, &slideIn, &slideFrom, &slideTime, &glideFeel, &barRange })
        show (*k, ctl);
    for (auto& k : advKnobs)
        show (*k, adv);
    resetAdv.setVisible (adv);
    repaint();
}

void SteelwaveEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto header = r.removeFromTop (34);
    settingsTab.setBounds (header.removeFromRight (92).reduced (0, 4));
    header.removeFromRight (6);
    playTab.setBounds (header.removeFromRight (64).reduced (0, 4));
    header.removeFromLeft (118); // title
    prevPreset.setBounds (header.removeFromLeft (26).reduced (0, 4));
    header.removeFromLeft (4);
    presetButton.setBounds (header.removeFromLeft (220).reduced (0, 4));
    header.removeFromLeft (4);
    nextPreset.setBounds (header.removeFromLeft (26).reduced (0, 4));
    header.removeFromLeft (12);
    learnButton.setBounds (header.removeFromLeft (100).reduced (0, 4));
    header.removeFromLeft (6);
    clearLearn.setBounds (header.removeFromLeft (70).reduced (0, 4));
    r.removeFromTop (6);

    auto placeChoice = [] (Choice& c, juce::Rectangle<int> cell) {
        c.label.setBounds (cell.removeFromTop (14));
        c.box.setBounds (cell.reduced (0, 1));
    };
    auto placeKnob = [] (Knob& k, juce::Rectangle<int> cell, int labelH = 16) {
        k.label.setBounds (cell.removeFromTop (labelH));
        k.slider.setBounds (cell);
    };

    // --- Play: mode and key, eight knobs, the neck ---
    {
        auto a = r;
        auto row = a.removeFromTop (42);
        placeChoice (set, row.removeFromLeft (row.getWidth() * 2 / 3).withTrimmedRight (10));
        placeChoice (key, row);
        a.removeFromTop (10);
        auto knobs = a.removeFromTop (128);
        const int kw = knobs.getWidth() / 8;
        for (auto* k : { &tone, &pick, &sustain, &vibDepth, &glide, &swellMain, &sweeten, &level })
            placeKnob (*k, knobs.removeFromLeft (kw).reduced (6, 0));
        a.removeFromTop (8);
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
        const int rowH = 92;
        for (int i = 0; i < 4; ++i)
        {
            auto row = a.removeFromTop (rowH).reduced (0, 3);
            ctlAreas[(size_t) i] = row;
            row.removeFromLeft (110);
            const int cw = row.getWidth() / 5;
            auto knobCell = [&] (Knob& k) { placeKnob (k, row.removeFromLeft (cw).reduced (4, 4)); };
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
            else
            {
                choiceCell (wheelMode);
                choiceCell (timing);
                choiceCell (mpe);
                choiceCell (bendRange);
            }
        }
        a.removeFromTop (3);
        ctlAreas[4] = a;
        pedalStrip = a.reduced (14, 0).removeFromTop (30).translated (0, 26);
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

void SteelwaveEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("STEELWAVE", 14, 14, 110, 26, juce::Justification::centredLeft);

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
    const int setIdx = proc.uiSet.load();
    const int keyPc = (int) proc.apvts.getRawParameterValue ("key")->load();

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
        const char* titles[] = { "SMART NOTES & PEDALS", "SWELL & VIBRATO", "SLIDES", "WHEEL, TIMING & MPE", "WHAT DOES WHAT" };
        for (int i = 0; i < 5; ++i)
            panel (ctlAreas[(size_t) i], titles[i], i < 4);
        drawPedals (g, pedalStrip);
        auto t = ctlAreas[4].reduced (14, 6);
        t.removeFromTop (16 + 10 + 30 + 6);
        const bool keys = (int) proc.apvts.getRawParameterValue ("pedalkeys")->load() == 1;
        const int mw = (int) proc.apvts.getRawParameterValue ("modwheel")->load();
        const juce::String mwText[] = { "vibrato", "the A+B pedals",
                                        "a bar slide (up to " + juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("barrange")->load())) + " frets)",
                                        "nothing" };
        const bool at = (int) proc.apvts.getRawParameterValue ("aftertouch")->load() == 0;
        const bool melody = (int) proc.apvts.getRawParameterValue ("wheel")->load() == 0;
        juce::String txt;
        txt << "Smart notes: play normally. A note a step from one you hold bends that string to it; let it go and it bends back. A single legato line slides.\n"
            << (keys ? "Pedal keys: MIDI 36-42 are A  B  C  LKL  LKR  RKL  RKR. Hold them like pedals; hit harder for a faster pedal.\n"
                     : "Pedal keys are off: MIDI 36-42 play notes. CC20-26 still work the pedals and levers.\n")
            << (melody ? "Pitch wheel: the top note moves a scale step up or down; the rest holds.   Mod wheel: "
                       : "Pitch wheel: moves the chord along the key's scale (up I to IV, down I to iii).   Mod wheel: ") << mwText[juce::jlimit (0, 3, mw)] << ".\n"
            << "Aftertouch: " << (at ? "vibrato" : "off") << ".   Expression pedal: the volume pedal.   Sustain: let strings ring.   Keyswitches: B0 smart notes, C1-B1 sets.";
        g.setColour (kText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (txt, t, juce::Justification::topLeft, 6);
        return;
    }
    if (page == AdvancedPage)
    {
        for (int gi = 0; gi < swadv::kGroups; ++gi)
            panel (groupAreas[(size_t) gi], swadv::groupName (gi), true);
        return;
    }

    // ---- Play: the neck (10 strings, the bar, what is ringing) ---------------------------------
    g.setColour (kPanel);
    g.fillRoundedRectangle (neckArea.toFloat(), 8.0f);
    auto a = neckArea.reduced (14, 10);
    const int slot = proc.uiSlot.load(), bar = proc.uiBar.load();
    const bool smart = setIdx == sw::kSmartSet, chords = setIdx < sw::kChordSets;
    const bool melodyWheel = (int) proc.apvts.getRawParameterValue ("wheel")->load() == 0;

    auto info = a.removeFromRight (std::min (300, a.getWidth() / 3));
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
        const auto& v = sw::chordSets()[(size_t) setIdx][(size_t) slot];
        title = chordName (sl.rest, keyPc);
        sub = juce::String (kSlotNames[slot]) + "  |  bar " + juce::String (bar) + "  |  " + juce::String (sw::comboName (v.restCombo));
        how = melodyWheel ? juce::String ("wheel: the top note moves a scale step\nup or down; the chord holds")
                          : "wheel up: " + chordName (sl.up, keyPc) + " (" + juce::String (v.upHow) + ")\nwheel down: " + chordName (sl.down, keyPc)
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
    info.removeFromTop (8);
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
}

// MIDI learn: click a control to pick it, then move a control on the keyboard
void SteelwaveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! learning)
        return;
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
        if (auto it = controlParam.find (c); it != controlParam.end())
        {
            proc.learnTarget = proc.paramIndex (it->second);
            repaint();
            return;
        }
}

void SteelwaveEditor::paintOverChildren (juce::Graphics& g)
{
    if (! learning)
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
