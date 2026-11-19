// Host-style tests for the Pump Organ processor: parameters, sound with no pedal, MPE through the
// plug-in's MIDI path, the pedal, state, the editor.
#include "../Source/PluginEditor.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...)                                        \
    do {                                                        \
        const bool ok_ = (cond);                                \
        std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL");         \
        std::printf (__VA_ARGS__);                              \
        std::printf ("\n");                                     \
        if (! ok_) ++failures;                                  \
    } while (0)

static constexpr double kFs = 48000.0;
struct Ev { double t; juce::MidiMessage m; };

// render `seconds` in 512-sample blocks, MIDI at its times; returns the left channel
static std::vector<float> render (PumpOrganProcessor& p, std::vector<Ev> ev, double seconds)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, 512);
    size_t k = 0;
    const int64_t total = (int64_t) (seconds * kFs);
    for (int64_t pos = 0; pos < total; pos += 512)
    {
        const int n = (int) std::min<int64_t> (512, total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        view.clear();
        juce::MidiBuffer midi;
        while (k < ev.size() && (int64_t) (ev[k].t * kFs) < pos + n)
        {
            midi.addEvent (ev[k].m, (int) std::max<int64_t> (0, (int64_t) (ev[k].t * kFs) - pos));
            ++k;
        }
        p.processBlock (view, midi);
        out.insert (out.end(), view.getReadPointer (0), view.getReadPointer (0) + n);
    }
    return out;
}
static double rms (const std::vector<float>& x, double t0, double t1)
{
    double s = 0;
    const size_t a = (size_t) (t0 * kFs), b = std::min (x.size(), (size_t) (t1 * kFs));
    for (size_t i = a; i < b; ++i) s += x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, b - a));
}
static constexpr int kHold = PumpOrganProcessor::kHoldKey;
static void setPlain (PumpOrganProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using MM = juce::MidiMessage;
    // a scratch settings folder (controller setup, favourites, presets), the setup already offered
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pumporgan_test_settings");
    settings.deleteRecursively();
    settings.createDirectory();
#ifdef _WIN32
    _putenv_s ("PUMPORGAN_SETTINGS_DIR", settings.getFullPathName().toRawUTF8());
#else
    setenv ("PUMPORGAN_SETTINGS_DIR", settings.getFullPathName().toRawUTF8(), 1);
#endif
    settings.getChildFile ("Controller.xml").replaceWithText ("<PumpOrganController asked=\"1\"/>");

    std::printf ("Parameters and defaults\n");
    {
        PumpOrganProcessor p;
        int missing = 0;
        for (auto* id : { "model", "wind", "windmode", "stop0", "stop5", "vox", "forte", "age", "air", "level", "pedalcc", "mpe", "memberrange",
                          "masterrange", "pressure", "vibdepth", "vibrate", "slide", "cutout", "warble", "swell", "chiff", "mic", "pumprate", "pumpdepth",
                          "hold", "bass", "melody", "neglect", "velcurve" })
            missing += p.apvts.getParameter (id) == nullptr;
        CHECK (missing == 0, "every parameter is there");
        int inAdvanced = 0;
        for (auto* g : p.getParameterTree().getSubgroups (false))
            if (g->getID() == "advanced")
                for (auto* prm : g->getParameters (false))
                    if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
                        inAdvanced += w->paramID == "cutout" || w->paramID == "warble" || w->paramID == "age" || w->paramID == "air" || w->paramID == "neglect";
        CHECK (inAdvanced == 5, "Tuning age, Air noise, Low-wind cut-out and warble, and Neglect sit in the Advanced group");
        auto v = [&] (const char* id) { return p.apvts.getParameter (id)->getCurrentValueAsText(); };
        CHECK (v ("model") == "Chord organ" && v ("mpe") == "On" && v ("pressure") == "Vibrato" && v ("memberrange") == "48 st" && v ("masterrange") == "2 st",
               "defaults: chord organ, MPE on, pressure = vibrato, note bend range 48 (Ableton's), pitch wheel 2");
        CHECK (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "an instrument that takes MIDI");
    }

    std::printf ("Playing\n");
    {
        PumpOrganProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        auto x = render (p, { { 2.0, MM::noteOn (1, 60, (juce::uint8) 100) }, { 4.0, MM::noteOff (1, 60) } }, 4.5);
        bool finite = true;
        for (float s : x) finite = finite && std::isfinite (s);
        CHECK (finite && rms (x, 3.0, 4.0) > 1e-3, "sounds with no pedal at all (the Wind knob): %.1f dB", 20 * std::log10 (rms (x, 3.0, 4.0)));
        CHECK (rms (x, 4.3, 4.5) < 0.1 * rms (x, 3.0, 4.0), "and stops when the key is let go");
        // the pedal (CC11) scales the wind: heel down, quiet
        auto y = render (p, { { 0.1, MM::noteOn (1, 60, (juce::uint8) 100) }, { 1.5, MM::controllerEvent (1, 11, 0) } }, 3.0);
        CHECK (rms (y, 2.5, 3.0) < 0.05 * rms (y, 1.0, 1.4), "the expression pedal is the wind: heel down, it falls silent");
    }

    std::printf ("MPE through the plug-in\n");
    {
        PumpOrganProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        // as Live sends MPE: each note on its own channel, its bend/pressure/slide before and after the note-on
        const int up2 = 8192 + (int) std::lround (8191 * 2.0 / 48.0);
        render (p, { { 2.0, MM::pitchWheel (2, 8192) }, { 2.0, MM::channelPressureChange (2, 0) },
                     { 2.0, MM::noteOn (2, 60, (juce::uint8) 100) }, { 2.0, MM::noteOn (3, 67, (juce::uint8) 100) },
                     { 2.5, MM::pitchWheel (2, up2) }, { 2.5, MM::channelPressureChange (3, 127) } },
                3.0);
        const auto& o = p.organ();
        CHECK (std::fabs (o.noteBend (60) - 2.0) < 0.02 && std::fabs (o.noteBend (67)) < 1e-6,
               "a bend on channel 2 bends only its note (C4 %+.2f, G4 %+.2f semitones)", o.noteBend (60), o.noteBend (67));
        CHECK (o.notePressure (67) > 0.9 && o.notePressure (60) < 0.01, "pressure on channel 3 presses only its note (vibrato by default)");
        // switched off, the same messages bend everything by the pitch wheel's range
        setPlain (p, "mpe", 0);
        render (p, { { 0.0, MM::pitchWheel (5, 16383) } }, 0.1);
        CHECK (std::fabs (o.noteBend (60) - 2.0) < 0.05 && std::fabs (o.noteBend (67) - 2.0) < 0.05,
               "MPE off: any pitch bend bends every note, by 2 (C4 %+.2f, G4 %+.2f)", o.noteBend (60), o.noteBend (67));
    }

    std::printf ("The swell lever, Drone hold, auto-pump\n");
    {
        PumpOrganProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        render (p, { { 0.0, MM::noteOn (1, 60, (juce::uint8) 100) }, { 0.1, MM::controllerEvent (1, 1, 127) } }, 0.5);
        CHECK (p.uiSwell.load() > 0.95f && p.uiMod.load() > 0.99f, "the mod slider opens the swell shutters (%.2f)", p.uiSwell.load());
        // B1, just below the keyboard: toggles Drone hold; what's held stays when it's let go
        render (p, { { 0.0, MM::noteOn (1, kHold, (juce::uint8) 100) }, { 0.05, MM::noteOff (1, kHold) }, { 0.1, MM::noteOff (1, 60) } }, 1.0);
        const bool latched = p.apvts.getRawParameterValue ("hold")->load() > 0.5f && p.organ().keyState (60).latched && p.uiHolding.load();
        auto x = render (p, { { 0.0, MM::noteOn (1, kHold, (juce::uint8) 100) }, { 0.05, MM::noteOff (1, kHold) } }, 2.0);
        CHECK (latched && p.apvts.getRawParameterValue ("hold")->load() < 0.5f && ! p.organ().keyState (60).latched && rms (x, 1.6, 2.0) < 0.05 * rms (x, 0.0, 0.03),
               "key B1 toggles Drone hold: C4 latched, then let go");
        // auto-pump follows the host's tempo
        setPlain (p, "pumprate", 1);
        setPlain (p, "pumpdepth", 1.0f);
        auto y = render (p, { { 0.0, MM::noteOn (1, 60, (juce::uint8) 100) }, { 0.0, MM::noteOn (1, 64, (juce::uint8) 100) } }, 3.0);
        double lo = 1e9, hi = 0;
        for (double t = 1.0; t < 2.9; t += 0.05)
        {
            lo = std::min (lo, rms (y, t, t + 0.05));
            hi = std::max (hi, rms (y, t, t + 0.05));
        }
        CHECK (20 * std::log10 (hi / lo) > 3.0, "auto-pump breathes the wind (%.1f dB swing)", 20 * std::log10 (hi / lo));
    }

    std::printf ("Presets, A/B, favourites, audition\n");
    {
        PumpOrganProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        const auto& fac = popresets::presets();
        int bad = 0;
        for (const auto& pr : fac)
            for (const auto& [id, v] : pr.values)
                bad += p.apvts.getParameter (id) == nullptr || popresets::isRig (id);
        CHECK (fac.size() >= 15 && p.getNumPrograms() == (int) fac.size() && bad == 0, "%d factory presets, every value a real sound setting", (int) fac.size());
        int sunday = -1;
        for (size_t i = 0; i < fac.size(); ++i)
            if (juce::String (fac[i].name) == "Sunday Best") sunday = (int) i;
        setPlain (p, "level", -6.0f);
        setPlain (p, "windmode", 1);
        p.setCurrentProgram (sunday);
        CHECK (p.apvts.getParameter ("model")->getCurrentValueAsText() == "Parlour organ" && p.apvts.getRawParameterValue ("bass")->load() > 0.5f
                   && std::fabs (p.apvts.getRawParameterValue ("level")->load() + 6.0f) < 1e-3 && (int) p.apvts.getRawParameterValue ("windmode")->load() == 1
                   && p.presetName() == "Sunday Best" && p.presetNote().isNotEmpty(),
               "a preset sets the sound, and leaves the rig (level, pumping) alone");
        render (p, { { 0.0, MM::programChange (1, 0) } }, 0.05);
        CHECK (p.presetName() == juce::String (fac[0].name) && p.apvts.getParameter ("model")->getCurrentValueAsText() == "Chord organ",
               "MIDI program change picks a preset (and the rest go back to defaults)");
        setPlain (p, "chiff", 0.77f);
        CHECK (p.saveUserPreset ("My Organ") && p.userPresetNames().contains ("My Organ"), "saves a user preset");
        setPlain (p, "chiff", 0.1f);
        CHECK (p.loadUserPreset ("My Organ") && std::fabs (p.apvts.getRawParameterValue ("chiff")->load() - 0.77f) < 1e-3, "and loads it back");
        CHECK (p.deleteUserPreset ("My Organ") && ! p.userPresetNames().contains ("My Organ"), "and deletes it");
        setPlain (p, "swell", 0.1f);
        p.abSwitch (1);
        setPlain (p, "swell", 0.9f);
        p.abSwitch (0);
        const float a = p.apvts.getRawParameterValue ("swell")->load();
        p.abSwitch (1);
        const float b = p.apvts.getRawParameterValue ("swell")->load();
        CHECK (std::fabs (a - 0.1f) < 1e-3 && std::fabs (b - 0.9f) < 1e-3, "A/B flips between two sounds");
        p.toggleFavourite ("Water Damage");
        PumpOrganProcessor q;
        CHECK (q.isFavourite ("Water Damage"), "favourites are kept for every instance");
        p.toggleFavourite ("Water Damage");
        PumpOrganProcessor au;
        au.setRateAndBufferSizeDetails (kFs, 512);
        au.prepareToPlay (kFs, 512);
        au.audition();
        auto x = render (au, {}, 11.5);
        CHECK (rms (x, 1.0, 3.0) > 1e-3 && rms (x, 6.0, 8.0) > rms (x, 1.0, 3.0) && au.uiPedal.load() > 0.99f,
               "audition plays a phrase, the swell lever opening for the climax, and gives the pedal back");
    }

    std::printf ("MIDI learn, controller setup\n");
    {
        PumpOrganProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        p.learnTarget = p.paramIndex ("chiff");
        render (p, { { 0.0, MM::controllerEvent (1, 21, 0) }, { 0.02, MM::controllerEvent (1, 21, 127) } }, 0.1);
        CHECK (p.ccMap[21].load() == p.paramIndex ("chiff") && p.apvts.getRawParameterValue ("chiff")->load() > 0.99f, "MIDI learn: CC21 now drives Chiff");
        p.startCapture (PumpOrganProcessor::WizMod);
        std::vector<Ev> ev;
        for (int i = 0; i <= 10; ++i)
        {
            ev.push_back ({ 0.01 * i, MM::controllerEvent (1, 22, 20 + 9 * i) });
            ev.push_back ({ 0.01 * i, MM::controllerEvent (1, 1, 60 + i) });
        }
        render (p, ev, 0.2);
        const int src = p.bestSource (PumpOrganProcessor::WizMod);
        p.finishStep (PumpOrganProcessor::WizMod, src);
        p.wizardStep = -1;
        CHECK (src == 22 && p.calibration().modCc == 22, "setup: CC22, moved furthest, becomes the swell lever");
        render (p, { { 0.0, MM::controllerEvent (1, 22, 110) } }, 0.3);
        CHECK (p.uiMod.load() > 0.99f && p.uiSwell.load() > 0.9f, "and its real top (87%%) opens the shutters fully");
        // the expression pedal on another CC, with a short travel
        p.startCapture (PumpOrganProcessor::WizExpr);
        std::vector<Ev> pe;
        for (int i = 0; i <= 10; ++i)
            pe.push_back ({ 0.01 * i, MM::controllerEvent (1, 30, 10 + 8 * i) });
        render (p, pe, 0.2);
        p.finishStep (PumpOrganProcessor::WizExpr, p.bestSource (PumpOrganProcessor::WizExpr, { 22 }));
        p.wizardStep = -1;
        render (p, { { 0.0, MM::controllerEvent (1, 30, 10) } }, 0.1);
        CHECK (p.calibration().exprCc == 30 && p.uiPedal.load() < 0.01f, "setup: CC30 becomes the wind pedal; its heel is no wind");
        PumpOrganProcessor q;
        CHECK (q.calibration().modCc == 22, "saved for every instance on this computer");
        settings.getChildFile ("Controller.xml").replaceWithText ("<PumpOrganController asked=\"1\"/>");
    }

    std::printf ("State and editor\n");
    {
        PumpOrganProcessor p;
        setPlain (p, "model", 1);
        setPlain (p, "vibdepth", 0.6f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        PumpOrganProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.apvts.getParameter ("model")->getCurrentValueAsText() == "Parlour organ"
                   && std::fabs (q.apvts.getRawParameterValue ("vibdepth")->load() - 0.6f) < 1e-3,
               "state round trip");
        p.ccMap[33] = p.paramIndex ("swell");
        p.getStateInformation (mb);
        PumpOrganProcessor r;
        r.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (r.ccMap[33].load() == r.paramIndex ("swell"), "MIDI learn assignments are saved with the session");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() == PumpOrganEditor::kW, "editor opens");
        CHECK (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == PumpOrganEditor::kW, "the front paints");
        auto* pe = dynamic_cast<PumpOrganEditor*> (ed.get());
        pe->showPage (PumpOrganEditor::BackPage);
        CHECK (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == PumpOrganEditor::kW, "behind the panel paints");
        pe->showSetup (true);
        CHECK (p.wizardStep.load() == 0 && ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == PumpOrganEditor::kW,
               "controller setup opens and listens");
        pe->showSetup (false);
        ed->setSize (PumpOrganEditor::kW * 6 / 5, PumpOrganEditor::kH * 6 / 5);
        CHECK (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == PumpOrganEditor::kW * 6 / 5, "and scales with the window");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
