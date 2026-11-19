// Host-style tests for the Pump Organ processor: parameters, sound with no pedal, MPE through the
// plug-in's MIDI path, the pedal, state, the editor.
#include "../Source/PluginProcessor.h"

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
static void setPlain (PumpOrganProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using MM = juce::MidiMessage;

    std::printf ("Parameters and defaults\n");
    {
        PumpOrganProcessor p;
        int missing = 0;
        for (auto* id : { "model", "wind", "windmode", "stop0", "stop5", "vox", "forte", "age", "air", "level", "pedalcc", "mpe", "memberrange",
                          "masterrange", "pressure", "vibdepth", "vibrate", "slide", "cutout", "warble" })
            missing += p.apvts.getParameter (id) == nullptr;
        CHECK (missing == 0, "every parameter is there");
        int inAdvanced = 0;
        for (auto* g : p.getParameterTree().getSubgroups (false))
            if (g->getID() == "advanced")
                for (auto* prm : g->getParameters (false))
                    if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
                        inAdvanced += w->paramID == "cutout" || w->paramID == "warble" || w->paramID == "age" || w->paramID == "air";
        CHECK (inAdvanced == 4, "Tuning age, Air noise, Low-wind cut-out and warble sit in the Advanced group");
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
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr && ed->getWidth() > 0, "editor opens");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
