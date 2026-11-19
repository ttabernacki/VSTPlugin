// Host-style tests for the Scrape Cello processor: parameters, playing through the plug-in's MIDI path
// (aftertouch, the pedal, MPE), state, the editor.
#include "../Source/PluginEditor.h"

#include <cstdio>
#include <cstdlib>

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

static std::vector<float> render (ScrapeCelloProcessor& p, std::vector<Ev> ev, double seconds)
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
static void setPlain (ScrapeCelloProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using MM = juce::MidiMessage;
    // a scratch settings folder (controller setup, favourites, presets), the setup already offered
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("scrapecello_test_settings");
    settings.deleteRecursively();
    settings.createDirectory();
#ifdef _WIN32
    _putenv_s ("SCRAPECELLO_SETTINGS_DIR", settings.getFullPathName().toRawUTF8());
#else
    setenv ("SCRAPECELLO_SETTINGS_DIR", settings.getFullPathName().toRawUTF8(), 1);
#endif
    settings.getChildFile ("Controller.xml").replaceWithText ("<ScrapeCelloController asked=\"1\"/>");

    std::printf ("Parameters and defaults\n");
    {
        ScrapeCelloProcessor p;
        int missing = 0;
        for (auto* id : { "pressure", "pressurerange", "position", "speed", "grit", "brightness", "ring", "body", "vibdepth", "vibrate", "vibdelay",
                          "glide", "mode", "pedalcc", "mpe", "memberrange", "masterrange", "level", "velcurve" })
            missing += p.apvts.getParameter (id) == nullptr;
        CHECK (missing == 0, "every parameter is there");
        auto v = [&] (const char* id) { return p.apvts.getParameter (id)->getCurrentValueAsText(); };
        CHECK (v ("mpe") == "On" && v ("memberrange") == "48 st" && v ("mode") == "Poly (4 bows)" && v ("pedalcc") == "CC11 Expression",
               "defaults: MPE on at 48 (Ableton's), four bows, the pedal on CC11");
        CHECK (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect(), "an instrument that takes MIDI");
    }

    std::printf ("Playing through the plug-in\n");
    {
        ScrapeCelloProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        auto x = render (p, { { 0.1, MM::noteOn (1, 50, (juce::uint8) 100) }, { 2.0, MM::noteOff (1, 50) } }, 3.5);
        bool finite = true;
        for (float s : x) finite = finite && std::isfinite (s);
        CHECK (finite && rms (x, 1.0, 1.8) > 1e-3, "a bowed D3: %.1f dB", 20 * std::log10 (rms (x, 1.0, 1.8)));
        CHECK (rms (x, 3.0, 3.5) < 0.01 * rms (x, 1.0, 1.8), "the bow lifts and it dies away");
        // the pedal is the bow's speed: heel down, the bow stops
        auto y = render (p, { { 0.1, MM::noteOn (1, 50, (juce::uint8) 100) }, { 1.2, MM::controllerEvent (1, 11, 0) } }, 3.0);
        CHECK (rms (y, 2.4, 3.0) < 0.05 * rms (y, 0.8, 1.1), "the expression pedal is the bow's speed: heel down, it stops");
    }

    std::printf ("MPE through the plug-in\n");
    {
        ScrapeCelloProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        const int up3 = 8192 + (int) std::lround (8191 * 3.0 / 48.0);
        render (p, { { 0.1, MM::noteOn (2, 45, (juce::uint8) 100) }, { 0.1, MM::noteOn (3, 52, (juce::uint8) 100) },
                     { 0.5, MM::pitchWheel (3, up3) } },
                1.2);
        const auto& c = p.cello();
        CHECK (std::fabs (c.voicePitch (52) - 55.0) < 0.05 && std::fabs (c.voicePitch (45) - 45.0) < 0.05,
               "a bend on channel 3 bends only its note (E3 -> %.2f, A2 %.2f)", c.voicePitch (52), c.voicePitch (45));
    }

    std::printf ("Presets, A/B, favourites, audition\n");
    {
        ScrapeCelloProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        const auto& fac = scpresets::presets();
        int bad = 0;
        for (const auto& pr : fac)
            for (const auto& [id, v] : pr.values)
                bad += p.apvts.getParameter (id) == nullptr || scpresets::isRig (id);
        CHECK (fac.size() >= 15 && p.getNumPrograms() == (int) fac.size() && bad == 0, "%d factory presets, every value a real sound setting", (int) fac.size());
        int grind = -1;
        for (size_t i = 0; i < fac.size(); ++i)
            if (juce::String (fac[i].name) == "Rust Belt") grind = (int) i;
        setPlain (p, "level", -6.0f);
        p.setCurrentProgram (grind);
        CHECK (std::fabs (p.apvts.getRawParameterValue ("pressure")->load() - 0.75f) < 1e-3 && std::fabs (p.apvts.getRawParameterValue ("level")->load() + 6.0f) < 1e-3
                   && p.presetName() == "Rust Belt",
               "a preset sets the sound, and leaves the rig (level) alone");
        // program change
        render (p, { { 0.0, MM::programChange (1, 0) } }, 0.05);
        CHECK (p.presetName() == juce::String (fac[0].name), "MIDI program change picks a preset");
        // user presets
        setPlain (p, "grit", 0.77f);
        CHECK (p.saveUserPreset ("My Scrape") && p.userPresetNames().contains ("My Scrape"), "saves a user preset");
        setPlain (p, "grit", 0.1f);
        CHECK (p.loadUserPreset ("My Scrape") && std::fabs (p.apvts.getRawParameterValue ("grit")->load() - 0.77f) < 1e-3, "and loads it back");
        CHECK (p.deleteUserPreset ("My Scrape") && ! p.userPresetNames().contains ("My Scrape"), "and deletes it");
        // A/B
        setPlain (p, "position", 0.1f);
        p.abSwitch (1);
        setPlain (p, "position", 0.9f);
        p.abSwitch (0);
        const float a = p.apvts.getRawParameterValue ("position")->load();
        p.abSwitch (1);
        const float b = p.apvts.getRawParameterValue ("position")->load();
        CHECK (std::fabs (a - 0.1f) < 1e-3 && std::fabs (b - 0.9f) < 1e-3, "A/B flips between two sounds");
        p.toggleFavourite ("Grinder");
        ScrapeCelloProcessor q;
        CHECK (q.isFavourite ("Grinder"), "favourites are kept for every instance");
        p.toggleFavourite ("Grinder");
        // audition: a phrase with nothing played
        ScrapeCelloProcessor au;
        au.setRateAndBufferSizeDetails (kFs, 512);
        au.prepareToPlay (kFs, 512);
        au.audition();
        auto x = render (au, {}, 6.0);
        CHECK (rms (x, 0.5, 2.5) > 1e-3 && rms (x, 3.6, 5.2) > 1e-3, "audition plays a phrase (a drone into a grind, then a melody)");
    }

    std::printf ("MIDI learn, controller setup, velocity\n");
    {
        ScrapeCelloProcessor p;
        p.setRateAndBufferSizeDetails (kFs, 512);
        p.prepareToPlay (kFs, 512);
        p.learnTarget = p.paramIndex ("grit");
        render (p, { { 0.0, MM::controllerEvent (1, 21, 0) }, { 0.02, MM::controllerEvent (1, 21, 127) } }, 0.1);
        CHECK (p.ccMap[21].load() == p.paramIndex ("grit") && p.apvts.getRawParameterValue ("grit")->load() > 0.99f, "MIDI learn: CC21 now drives Grit");
        // controller setup: the slider moved furthest becomes the mod wheel (bow position), with its range
        p.startCapture (ScrapeCelloProcessor::WizMod);
        std::vector<Ev> ev;
        for (int i = 0; i <= 10; ++i)
        {
            ev.push_back ({ 0.01 * i, MM::controllerEvent (1, 22, 20 + 9 * i) });
            ev.push_back ({ 0.01 * i, MM::controllerEvent (1, 1, 60 + i) });
        }
        render (p, ev, 0.2);
        const int src = p.bestSource (ScrapeCelloProcessor::WizMod);
        p.finishStep (ScrapeCelloProcessor::WizMod, src);
        p.wizardStep = -1;
        CHECK (src == 22 && p.calibration().modCc == 22 && p.calibration().asked, "setup: CC22, moved furthest, becomes the bow-position slider");
        render (p, { { 0.0, MM::controllerEvent (1, 22, 110) } }, 0.1);
        CHECK (p.uiMod.load() > 0.99f, "and its real top (87%%) reads as full (%.2f)", p.uiMod.load());
        ScrapeCelloProcessor q;
        CHECK (q.calibration().modCc == 22, "saved for every instance on this computer");
    }

    std::printf ("State and editor\n");
    {
        ScrapeCelloProcessor p;
        setPlain (p, "mode", 1);
        setPlain (p, "grit", 0.8f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ScrapeCelloProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.apvts.getParameter ("mode")->getCurrentValueAsText() == "Mono (legato glides)"
                   && std::fabs (q.apvts.getRawParameterValue ("grit")->load() - 0.8f) < 1e-3,
               "state round trip");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() == ScrapeCelloEditor::kW, "editor opens");
        CHECK (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == ScrapeCelloEditor::kW, "the Play page paints");
        auto* se = dynamic_cast<ScrapeCelloEditor*> (ed.get());
        se->showPage (ScrapeCelloEditor::SettingsPage);
        CHECK (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == ScrapeCelloEditor::kW, "the Settings page paints");
        se->showSetup (true);
        CHECK (p.wizardStep.load() == 0 && ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == ScrapeCelloEditor::kW,
               "controller setup opens and listens");
        se->showSetup (false);
        ed->setSize (ScrapeCelloEditor::kW * 6 / 5, ScrapeCelloEditor::kH * 6 / 5);
        CHECK (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f).getWidth() == ScrapeCelloEditor::kW * 6 / 5, "and scales with the window");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
