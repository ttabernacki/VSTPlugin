#include "PluginEditor.h"

namespace
{
using namespace bassui;
const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const juce::Colour kGrind (0xffe5534b);

void styleButton (juce::TextButton& b, juce::Colour on)
{
    b.setColour (juce::TextButton::buttonColourId, kPanel2);
    b.setColour (juce::TextButton::buttonOnColourId, on);
}
juce::String noteName (double midi)
{
    const int n = (int) std::lround (midi);
    return juce::String (kNoteNames[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}
} // namespace

// ==== controller setup ===========================================================================
SetupWizard::SetupWizard (ScrapeCelloProcessor& p, std::function<void()> onClose) : proc (p), close (std::move (onClose))
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
        if (step >= ScrapeCelloProcessor::kWizSteps)
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
        ScrapeCelloProcessor::ControllerCal c;
        c.asked = true;
        proc.setCalibration (c);
        found.fill (-1);
        go (ScrapeCelloProcessor::kWizSteps);
    };
}

void SetupWizard::begin()
{
    found.fill (-1);
    go (0);
}

void SetupWizard::go (int s)
{
    step = juce::jlimit (0, (int) ScrapeCelloProcessor::kWizSteps, s);
    if (step < ScrapeCelloProcessor::kWizSteps)
        proc.startCapture (step);
    else
        proc.wizardStep = -1;
    const bool summary = step == ScrapeCelloProcessor::kWizSteps;
    skip.setVisible (! summary);
    next.setVisible (! summary);
    reset.setVisible (summary);
    back.setEnabled (step > 0);
    next.setButtonText (step == ScrapeCelloProcessor::kWizSteps - 1 ? "Finish" : "Next");
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
    if (src == ScrapeCelloProcessor::kSrcPitch)
        return "Pitch wheel";
    if (src == ScrapeCelloProcessor::kSrcPressure)
        return "Aftertouch";
    if (src == ScrapeCelloProcessor::kSrcVelocity)
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
    const int nSteps = ScrapeCelloProcessor::kWizSteps;
    g.drawText (step < nSteps ? "CONTROLLER SETUP   " + juce::String (step + 1) + " / " + juce::String (nSteps) : juce::String ("CONTROLLER SETUP   DONE"),
                a.removeFromTop (24), juce::Justification::centredLeft);
    a.removeFromTop (10);
    static const char* titles[] = { "Pitch slider", "Mod slider", "Expression pedal", "Aftertouch", "Velocity" };
    static const char* what[] = {
        "Move your pitch slider (or wheel) all the way up and all the way down, then let it go. Scrape Cello bends to its real ends, "
        "with a little dead zone where it rests.",
        "Move your mod slider (or wheel) from the bottom to the top. Whatever CC it sends becomes the mod wheel.",
        "Rock your expression pedal from heel to toe. It becomes the bow speed, over the travel it really has (heel: the bow stops).",
        "Hold a key and press into it as hard as you comfortably would. That pressure becomes a full grind: the whole bow-pressure range.",
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
            g.drawText (step == ScrapeCelloProcessor::WizVelocity ? "Listening... (" + juce::String (proc.capNotes.load()) + " notes so far)"
                                                                : juce::String ("Listening... move it all the way."),
                        a.removeFromTop (20), juce::Justification::centredLeft);
            return;
        }
        const bool pitchWheel = src == ScrapeCelloProcessor::kSrcPitch;
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
    line ("Expression pedal", (c.exprCc >= 0 ? sourceName (c.exprCc) : proc.apvts.getParameter ("pedalcc")->getCurrentValueAsText()) + ",  " + pc (c.exprLo)
                                  + " to " + pc (c.exprHi));
    line ("Aftertouch", "full bow pressure at " + pc (c.atHi));
    line ("Velocity", pc (c.velLo) + " to " + pc (c.velHi) + " is the whole range");
    a.removeFromTop (8);
    g.setColour (kDim);
    g.drawFittedText ("Saved for every Scrape Cello on this computer. Run it again any time from Settings.", a.removeFromTop (36),
                      juce::Justification::topLeft, 2);
}

// ==== editor =======================================================================================
void ScrapeCelloEditor::Choice::setup (juce::Component& parent, juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
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

ScrapeCelloEditor::ScrapeCelloEditor (ScrapeCelloProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), wizard (p, [this] { showSetup (false); })
{
    setLookAndFeel (&laf);
    addAndMakeVisible (canvas);
    auto& s = proc.apvts;
    auto& c = canvas;

    // --- Play page: the bow, the string, the hand ---
    pressure.setup (c, s, "pressure", "PRESSURE");
    reach.setup (c, s, "pressurerange", "REACH");
    position.setup (c, s, "position", "POSITION");
    speed.setup (c, s, "speed", "SPEED");
    grit.setup (c, s, "grit", "GRIT");
    brightness.setup (c, s, "brightness", "BRIGHTNESS");
    body.setup (c, s, "body", "BODY");
    vibDepth.setup (c, s, "vibdepth", "VIBRATO");
    glide.setup (c, s, "glide", "GLIDE");
    level.setup (c, s, "level", "LEVEL");
    pressure.slider.setTooltip ("Bow pressure with no aftertouch. About half: a full clean tone. About two thirds: the gritty edge. Full: a grind. "
                                "Aftertouch (or MPE pressure) adds to it.");
    reach.slider.setTooltip ("How far aftertouch / MPE pressure can push the bow: all the way to a full grind, or a gentler range.");
    position.slider.setTooltip ("Where the bow crosses the string: sul tasto (over the fingerboard: soft, flutey) to ponticello (at the bridge: glassy, "
                                "and it takes more pressure to grind). The mod slider moves it towards the bridge; MPE slide, per note.");
    speed.slider.setTooltip ("Bow speed (the expression pedal scales it). With the same weight on it, a slower bow creaks and grinds sooner.");
    grit.slider.setTooltip ("Rosin and hair: grain in the friction while the string slips, growing as it nears the edge.");
    brightness.slider.setTooltip ("How long the string's upper partials last: dark, old strings to bright, new ones.");
    body.slider.setTooltip ("The cello body's resonance: its air, its wood, the bridge hill. Down: a drier, more direct string.");
    vibDepth.slider.setTooltip ("Finger vibrato, coming in on held notes (rate and delay in Settings).");
    glide.slider.setTooltip ("How long legato slides take (mono) and how fast MPE bends follow.");
    level.slider.setTooltip ("Output level.");

    // --- Settings ---
    vibRate.setup (c, s, "vibrate", "VIB RATE");
    vibDelay.setup (c, s, "vibdelay", "VIB DELAY");
    ring.setup (c, s, "ring", "RING");
    velCurve.setup (c, s, "velcurve", "VELOCITY CURVE");
    mode.setup (c, s, "mode", "VOICES");
    pedalCc.setup (c, s, "pedalcc", "BOW SPEED PEDAL");
    mpe.setup (c, s, "mpe", "MPE");
    memberRange.setup (c, s, "memberrange", "MPE NOTE BEND RANGE");
    masterRange.setup (c, s, "masterrange", "PITCH WHEEL RANGE");
    vibRate.slider.setTooltip ("Speed of the finger vibrato.");
    vibDelay.slider.setTooltip ("How long a note is held before its vibrato comes in.");
    ring.slider.setTooltip ("How long the strings ring on after the bow lifts.");
    velCurve.slider.setTooltip ("How your keys' velocity becomes the bow's attack: Light, a soft touch bites; Heavy, it takes a hard hit.");
    mode.box.setTooltip ("Poly: four bows, one per note (drones, chords). Mono: one bow; legato notes glide under it.");
    pedalCc.box.setTooltip ("The pedal that sets the bow's speed (heel: the bow stops). Off: the Speed knob alone.");
    mpe.box.setTooltip ("MPE: each note's own pressure, slide and bend (in Ableton: Enable MPE Mode on the plug-in).");
    memberRange.box.setTooltip ("Bend range of each MPE note: match your controller (48 is the MPE default, and Ableton's).");
    masterRange.box.setTooltip ("Range of an ordinary pitch wheel / the MPE master channel.");
    styleButton (setupButton, kAccent.darker (0.6f));
    setupButton.setTooltip ("Find your pitch slider, mod slider, expression pedal, aftertouch and velocity range, and fit the cello to them.");
    setupButton.onClick = [this] { showSetup (true); };
    c.addChildComponent (setupButton);

    // --- header ---
    for (auto* b : { &playTab, &settingsTab })
    {
        styleButton (*b, kAccent.darker (0.6f));
        c.addAndMakeVisible (*b);
    }
    playTab.onClick = [this] { showPage (PlayPage); };
    settingsTab.onClick = [this] { showPage (SettingsPage); };
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
    presetButton.setTooltip ("Presets: favourites, factory sounds by category, and your own. They set the sound and the bowing, not your rig.");
    presetButton.onClick = [this] { presetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    favButton.setTooltip ("Favourite this preset: favourites come first in the preset menu.");
    favButton.onClick = [this] { proc.toggleFavourite (proc.presetName()); updateHeader(); };
    abA.setTooltip ("A/B compare: two sounds to flip between.");
    abB.setTooltip (abA.getTooltip());
    abA.onClick = [this] { proc.abSwitch (0); updateHeader(); };
    abB.onClick = [this] { proc.abSwitch (1); updateHeader(); };
    abCopy.setTooltip ("Copy this sound to the other side of the A/B.");
    abCopy.onClick = [this] { proc.abCopyToOther(); };
    auditionButton.setTooltip ("Hear the sound: a drone pressed into a grind and back, then a short melody leaning on the edge.");
    auditionButton.onClick = [this] { proc.audition(); };
    learnButton.setClickingTogglesState (true);
    styleButton (learnButton, kWarm.darker (0.3f));
    learnButton.setTooltip ("MIDI learn: click a knob or menu, then move a control on your keyboard. Click MIDI LEARN again when done.");
    learnButton.onClick = [this] {
        learning = learnButton.getToggleState();
        if (! learning) proc.learnTarget = -1;
        clearLearn.setVisible (learning);
        auditionButton.setVisible (! learning);
        canvas.repaint();
    };
    c.addAndMakeVisible (learnButton);
    styleButton (clearLearn, kPanel2);
    clearLearn.onClick = [this] { proc.clearMidiMap(); canvas.repaint(); };
    c.addChildComponent (clearLearn);

    const std::pair<Knob*, const char*> knobs[] = { { &pressure, "pressure" }, { &reach, "pressurerange" }, { &position, "position" },
                                                    { &speed, "speed" }, { &grit, "grit" }, { &brightness, "brightness" }, { &body, "body" },
                                                    { &vibDepth, "vibdepth" }, { &glide, "glide" }, { &level, "level" }, { &vibRate, "vibrate" },
                                                    { &vibDelay, "vibdelay" }, { &ring, "ring" }, { &velCurve, "velcurve" } };
    for (auto& [k, id] : knobs)
        registerControl (k->slider, id);
    for (auto* ch : { &mode, &pedalCc, &mpe, &memberRange, &masterRange })
        registerControl (ch->box, ch->paramId);

    c.addChildComponent (wizard);
    setResizable (true, true);
    setResizeLimits (kW * 4 / 5, kH * 4 / 5, kW * 3 / 2, kH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    canvas.setBounds (0, 0, kW, kH);
    layout();
    setSize (kW, kH);
    showPage (PlayPage);
    updateHeader();
    startTimerHz (30);
    if (! proc.calibration().asked)
        showSetup (true);
}

ScrapeCelloEditor::~ScrapeCelloEditor()
{
    stopTimer();
    proc.wizardStep = -1;
    setLookAndFeel (nullptr);
}

void ScrapeCelloEditor::showSetup (bool show)
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

void ScrapeCelloEditor::timerCallback()
{
    phase += 0.9f;
    if (wizard.isVisible())
        wizard.tick();
    if (page == PlayPage)
        canvas.repaint (bowArea);
    canvas.repaint (statusArea);
    if (learning)
        canvas.repaint();
    updateHeader();
}

void ScrapeCelloEditor::updateHeader()
{
    const auto name = proc.presetName();
    if (presetButton.getButtonText() != name)
        presetButton.setButtonText (name);
    const bool fav = proc.isFavourite (name);
    const juce::String star = juce::String::fromUTF8 (fav ? "\xe2\x98\x85" : "\xe2\x98\x86");
    if (favButton.getButtonText() != star)
    {
        favButton.setButtonText (star);
        favButton.setColour (juce::TextButton::textColourOffId, fav ? kWarm : kDim);
    }
    abA.setToggleState (proc.abActive() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.abActive() == 1, juce::dontSendNotification);
    abCopy.setButtonText (proc.abActive() == 0 ? "A>B" : "B>A");
}

juce::StringArray ScrapeCelloEditor::allPresets() const
{
    juce::StringArray a;
    for (const auto& p : scpresets::presets())
        a.add (p.name);
    a.addArray (proc.userPresetNames());
    return a;
}

void ScrapeCelloEditor::stepPreset (int dir)
{
    const auto all = allPresets();
    const int nFactory = (int) scpresets::presets().size(), n = all.size();
    if (n == 0) return;
    int i = proc.userPreset.isNotEmpty() ? all.indexOf (proc.userPreset, false, nFactory) : proc.currentProgram.load();
    i = ((juce::jmax (i, 0) + dir) % n + n) % n;
    if (i < nFactory) proc.setCurrentProgram (i);
    else proc.loadUserPreset (all[i]);
}

void ScrapeCelloEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save preset", "Saves the sound and the bowing as one of your own presets.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.userPreset.isNotEmpty() ? proc.userPreset : juce::String ("My cello"), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ScrapeCelloEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result) {
        if (result == 1 && safe != nullptr)
            safe->proc.saveUserPreset (w->getTextEditorContents ("name"));
    }), true);
}

void ScrapeCelloEditor::presetMenu()
{
    juce::PopupMenu m;
    const auto& fac = scpresets::presets();
    const int current = proc.userPreset.isEmpty() ? proc.currentProgram.load() : -1;
    const auto user = proc.userPresetNames();
    const auto favs = proc.favourites();
    juce::PopupMenu favMenu;
    for (size_t i = 0; i < fac.size(); ++i)
        if (favs.contains (fac[i].name)) favMenu.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
    for (int i = 0; i < user.size(); ++i)
        if (favs.contains (user[i])) favMenu.addItem (1000 + i, user[i], true, user[i] == proc.userPreset);
    m.addSubMenu (juce::String::fromUTF8 ("\xe2\x98\x85 Favourites"), favMenu, favMenu.getNumItems() > 0);
    m.addSeparator();
    juce::StringArray cats;
    for (const auto& p : fac) cats.addIfNotAlreadyThere (p.category);
    for (const auto& cat : cats)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < fac.size(); ++i)
            if (cat == fac[i].category) sub.addItem (1 + (int) i, fac[i].name, true, (int) i == current);
        m.addSubMenu (cat, sub);
    }
    juce::PopupMenu mine;
    for (int i = 0; i < user.size(); ++i)
        mine.addItem (1000 + i, user[i], true, user[i] == proc.userPreset);
    m.addSubMenu ("My presets", mine, user.size() > 0);
    m.addSeparator();
    m.addItem (5000, "Save as...");
    m.addItem (5001, "Delete \"" + proc.userPreset + "\"", proc.userPreset.isNotEmpty());
    juce::Component::SafePointer<ScrapeCelloEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [safe, user] (int r) {
        if (safe == nullptr || r == 0) return;
        if (r == 5000) safe->savePresetDialog();
        else if (r == 5001) safe->proc.deleteUserPreset (safe->proc.userPreset);
        else if (r >= 1000) safe->proc.loadUserPreset (user[r - 1000]);
        else safe->proc.setCurrentProgram (r - 1);
        safe->updateHeader();
    });
}

void ScrapeCelloEditor::showPage (int p)
{
    page = p;
    const bool play = p == PlayPage, set = p == SettingsPage;
    playTab.setToggleState (play, juce::dontSendNotification);
    settingsTab.setToggleState (set, juce::dontSendNotification);
    for (auto* k : { &pressure, &reach, &position, &speed, &grit, &brightness, &body, &vibDepth, &glide, &level })
        show (*k, play);
    for (auto* k : { &vibRate, &vibDelay, &ring, &velCurve })
        show (*k, set);
    for (auto* ch : { &mode, &pedalCc, &mpe, &memberRange, &masterRange })
        ch->show (set);
    setupButton.setVisible (set);
    canvas.repaint();
}

void ScrapeCelloEditor::resized()
{
    canvas.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
}

void ScrapeCelloEditor::layout()
{
    auto r = juce::Rectangle<int> (0, 0, kW, kH).reduced (14);
    auto header = r.removeFromTop (34);
    settingsTab.setBounds (header.removeFromRight (92).reduced (0, 4));
    header.removeFromRight (6);
    playTab.setBounds (header.removeFromRight (64).reduced (0, 4));
    header.removeFromRight (8);
    statusArea = header.removeFromRight (70);
    header.removeFromLeft (132);
    prevPreset.setBounds (header.removeFromLeft (26).reduced (0, 4));
    header.removeFromLeft (3);
    presetButton.setBounds (header.removeFromLeft (180).reduced (0, 4));
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
    r.removeFromTop (8);

    auto placeKnob = [] (Knob& k, juce::Rectangle<int> cell) {
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    };
    auto placeChoice = [] (Choice& c, juce::Rectangle<int> cell) {
        c.label.setBounds (cell.removeFromTop (14));
        c.box.setBounds (cell.reduced (0, 1));
    };
    { // Play: the bows above, the knobs below
        auto a = r;
        auto knobs = a.removeFromBottom (126);
        a.removeFromBottom (8);
        legendArea = a.removeFromBottom (22);
        bowArea = a;
        const int kw = knobs.getWidth() / 10;
        for (auto* k : { &pressure, &reach, &position, &speed, &grit, &brightness, &body, &vibDepth, &glide, &level })
            placeKnob (*k, knobs.removeFromLeft (kw).reduced (6, 0));
    }
    { // Settings: four panels
        auto a = r;
        const int rowH = 112;
        for (int i = 0; i < 4; ++i)
        {
            auto row = a.removeFromTop (rowH).reduced (0, 4);
            setAreas[(size_t) i] = row;
            row.removeFromLeft (140);
            const int cw = row.getWidth() / 4;
            auto knobCell = [&] (Knob& k) { placeKnob (k, row.removeFromLeft (cw).reduced (14, 6)); };
            auto choiceCell = [&] (Choice& c) { placeChoice (c, row.removeFromLeft (cw).reduced (8, 0).withSizeKeepingCentre (cw - 16, 44)); };
            if (i == 0) { knobCell (vibRate); knobCell (vibDelay); knobCell (ring); }
            else if (i == 1) { choiceCell (mode); knobCell (velCurve); choiceCell (pedalCc); }
            else if (i == 2) { choiceCell (mpe); choiceCell (memberRange); choiceCell (masterRange); }
            else setupButton.setBounds (row.removeFromLeft (cw * 2).reduced (16, 0).withSizeKeepingCentre (cw * 2 - 32, 36));
        }
    }
    wizard.setBounds (0, 0, kW, kH);
}

// ---- painting --------------------------------------------------------------------------------------
void ScrapeCelloEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("SCRAPE CELLO", 14, 14, 130, 34, juce::Justification::centredLeft);
    const float cpu = proc.uiCpu.load();
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (cpu > 0.7f ? kWarm : kDim);
    g.drawText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0f)) + " %", statusArea, juce::Justification::centredRight);

    if (page == SettingsPage)
    {
        const char* titles[] = { "LEFT HAND", "PLAYING", "MPE", "CONTROLLERS" };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (kPanel);
            g.fillRoundedRectangle (setAreas[(size_t) i].toFloat(), 8.0f);
            g.setColour (kAccent);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (titles[i], setAreas[(size_t) i].withWidth (130).reduced (14, 0), juce::Justification::centredLeft, 2);
        }
        auto t = setAreas[3].withTrimmedLeft (140 + (setAreas[3].getWidth() - 140) / 2).reduced (8, 10);
        g.setColour (kText);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("Aftertouch / MPE pressure: bow pressure, up to a grind.\nMod slider: bow position, towards the bridge.  MPE slide: per note.\n"
                          "Expression pedal: bow speed (heel: the bow stops).\nPitch bend: per note with MPE (48), else the wheel (2).  Sustain: keep bowing.",
                          t, juce::Justification::topLeft, 6);
        auto below = juce::Rectangle<int> (14, setAreas[3].getBottom() + 12, kW - 28, kH - setAreas[3].getBottom() - 26);
        g.setColour (kDim);
        g.drawFittedText ("In Ableton Live: right-click the plug-in's title bar, Enable MPE Mode. Each note then grinds, slides and bends on its own.",
                          below, juce::Justification::topLeft, 2);
        return;
    }
    drawBows (g, bowArea);
    // the legend: what the gauge's colours mean
    auto l = legendArea.reduced (14, 2);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    const std::pair<juce::Colour, const char*> zones[] = { { kDim, "LIGHT" }, { kAccent, "CLEAN" }, { kWarm, "EDGE" }, { kGrind, "GRIND" } };
    for (auto& [col, name] : zones)
    {
        auto z = l.removeFromLeft (90);
        g.setColour (col);
        g.fillRoundedRectangle (z.removeFromLeft (12).withSizeKeepingCentre (12, 8).toFloat(), 2.0f);
        g.setColour (kDim);
        g.drawText (name, z.withTrimmedLeft (6), juce::Justification::centredLeft);
    }
    g.setColour (kDim);
    g.drawText ("aftertouch presses the bow  |  mod slider moves it to the bridge  |  pedal is its speed", l, juce::Justification::centredRight);
}

// the strings: each sounding note a string, vibrating with its level; the bow where it crosses it; its pressure
// gauge, coloured by regime
void ScrapeCelloEditor::drawBows (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto a = area.reduced (16, 12);
    auto gauges = a.removeFromRight (200);
    a.removeFromRight (16);
    g.setColour (kAccent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("THE BOWS", a.removeFromTop (18), juce::Justification::centredLeft);
    // the neck end (left) to the bridge (right)
    const float x0 = (float) a.getX() + 60.0f, x1 = (float) a.getRight() - 30.0f;
    g.setColour (kLine);
    g.fillRoundedRectangle (x1 - 4.0f, (float) a.getY() + 4.0f, 8.0f, (float) a.getHeight() - 8.0f, 3.0f); // the bridge
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.setColour (kDim);
    g.drawText ("TASTO", (int) x0 - 10, a.getBottom() - 14, 60, 12, juce::Justification::centredLeft);
    g.drawText ("PONTICELLO", (int) x1 - 110, a.getBottom() - 14, 100, 12, juce::Justification::centredRight);
    g.drawText ("BRIDGE", (int) x1 - 24, a.getY() - 14, 48, 12, juce::Justification::centred);
    const int nv = sc::ScrapeCello::kVoices;
    const float rowH = (float) (a.getHeight() - 20) / (float) nv;
    // the bowing range on the string: from ~1/4 of the way out (tasto) to right by the bridge
    const float bowFrom = x0 + 0.55f * (x1 - x0), bowTo = x1 - 12.0f;
    for (int i = 0; i < nv; ++i)
    {
        const auto& u = proc.uiVoices[(size_t) i];
        const float y = (float) a.getY() + rowH * ((float) i + 0.5f);
        const bool on = u.on.load();
        const float lv = on ? juce::jlimit (0.0f, 1.0f, (10.0f * std::log10 (u.level.load() + 1e-12f) + 70.0f) / 45.0f) : 0.0f; // ~70 dB shown
        const float pr = u.pressure.load();
        const juce::Colour zone = ! on ? kLine : pr < 0.3f ? kDim : pr < 1.0f ? kAccent : pr < 1.35f ? kWarm : kGrind;
        // the string, vibrating
        juce::Path str;
        const int steps = 120;
        for (int k = 0; k <= steps; ++k)
        {
            const float t = (float) k / steps, x = x0 + (x1 - x0) * t;
            const float env = std::sin (juce::MathConstants<float>::pi * t);
            const float rough = pr > 1.0f ? 0.35f * std::sin (37.0f * t + phase * 1.7f + (float) i) : 0.0f;
            const float dy = lv * rowH * 0.32f * env * (std::sin (phase + 3.0f * (float) i + 1.2f) + rough);
            if (k == 0) str.startNewSubPath (x, y + dy);
            else str.lineTo (x, y + dy);
        }
        g.setColour (on ? zone.withAlpha (0.9f) : kLine.brighter (0.3f));
        g.strokePath (str, juce::PathStrokeType (on ? 2.2f : 1.4f));
        // the note
        g.setColour (on ? kText : kDim);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (on ? noteName (u.pitch.load()) : juce::String ("-"), a.getX(), (int) (y - 9), 54, 18, juce::Justification::centredLeft);
        if (! on)
            continue;
        // the bow, where it crosses: its hair across the string, an arrow for its direction
        const float bx = bowFrom + (bowTo - bowFrom) * u.position.load();
        const float hb = rowH * 0.42f;
        g.setColour (kWarm.withAlpha (0.25f + 0.6f * juce::jlimit (0.0f, 1.0f, pr / 1.5f)));
        g.fillRoundedRectangle (bx - 5.0f, y - hb, 10.0f, 2.0f * hb, 3.0f);
        g.setColour (kWarm);
        const float dir = u.dir.load() >= 0 ? 1.0f : -1.0f;
        juce::Path arrow;
        arrow.addTriangle (bx - 4.0f, y - hb - 4.0f + dir * 0.0f, bx + 4.0f, y - hb - 4.0f, bx, y - hb - 4.0f - 7.0f * dir);
        g.fillPath (arrow);
        // the gauge: pressure against this note's edge (1 = the edge), up to 2
        auto gr = juce::Rectangle<float> ((float) gauges.getX(), y - 7.0f, (float) gauges.getWidth() - 50.0f, 14.0f);
        g.setColour (kLine);
        g.fillRoundedRectangle (gr, 4.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, pr / 2.0f);
        g.setColour (zone);
        g.fillRoundedRectangle (gr.withWidth (std::max (3.0f, gr.getWidth() * fill)), 4.0f);
        g.setColour (kText.withAlpha (0.6f));
        g.drawVerticalLine ((int) (gr.getX() + gr.getWidth() * 0.5f), gr.getY() - 3.0f, gr.getBottom() + 3.0f); // the edge
        g.setColour (zone);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (pr < 0.3f ? "LIGHT" : pr < 1.0f ? "CLEAN" : pr < 1.35f ? "EDGE" : "GRIND", (int) gr.getRight() + 6, (int) (y - 7), 44, 14,
                    juce::Justification::centredLeft);
    }
    g.setColour (kDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PRESSURE  (the line: the edge)", gauges.removeFromTop (14), juce::Justification::centredLeft);
}

void ScrapeCelloEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! learning) return;
    for (auto* c = e.eventComponent; c != nullptr && c != &canvas; c = c->getParentComponent())
        if (auto it = controlParam.find (c); it != controlParam.end())
        {
            proc.learnTarget = proc.paramIndex (it->second);
            canvas.repaint();
            return;
        }
}

void ScrapeCelloEditor::paintOverCanvas (juce::Graphics& g)
{
    if (! learning || wizard.isVisible()) return;
    const int target = proc.learnTarget.load();
    for (const auto& [comp, id] : controlParam)
    {
        if (! comp->isVisible()) continue;
        const int pi = proc.paramIndex (id);
        int cc = -1;
        for (int i = 0; i < 128; ++i)
            if (proc.ccMap[(size_t) i].load() == pi) cc = i;
        const auto b = comp->getBounds().toFloat().expanded (3.0f);
        g.setColour (pi == target ? kWarm : cc >= 0 ? kAccent : kLine);
        g.drawRoundedRectangle (b, 5.0f, pi == target ? 2.5f : cc >= 0 ? 2.0f : 1.0f);
        if (pi == target)
        {
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("move a control", b.withHeight (14.0f).translated (0.0f, -14.0f), juce::Justification::centred);
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
