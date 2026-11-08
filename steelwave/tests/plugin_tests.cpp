// Host-style tests for the real Steelwave processor.
#include "../Source/PluginEditor.h"

#include <chrono>
#include <random>

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

static void setPlain (SteelwaveProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
static float getPlain (SteelwaveProcessor& p, const char* id)
{
    auto* prm = p.apvts.getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}
static void prepare (SteelwaveProcessor& p, double sr, int bs)
{
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);
}

struct Ev { int64_t at; juce::MidiMessage m; };

// Render `seconds` with MIDI events at absolute sample positions, in the given block sizes.
static std::vector<float> render (SteelwaveProcessor& p, const std::vector<Ev>& ev, double seconds, const std::vector<int>& blocks,
                                  double fs = kFs, std::vector<float>* right = nullptr)
{
    const int64_t total = (int64_t) (seconds * fs);
    std::vector<float> out ((size_t) total);
    if (right) right->assign ((size_t) total, 0.0f);
    juce::AudioBuffer<float> buf (2, 8192);
    int64_t pos = 0;
    size_t bi = 0;
    while (pos < total)
    {
        const int n = (int) std::min<int64_t> (blocks[bi++ % blocks.size()], total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        view.clear();
        juce::MidiBuffer midi;
        for (auto& e : ev)
            if (e.at >= pos && e.at < pos + n)
                midi.addEvent (e.m, (int) (e.at - pos));
        p.processBlock (view, midi);
        for (int i = 0; i < n; ++i)
        {
            out[(size_t) (pos + i)] = view.getSample (0, i);
            if (right) (*right)[(size_t) (pos + i)] = view.getSample (1, i);
        }
        pos += n;
    }
    return out;
}

static double rms (const std::vector<float>& x, double t0, double t1, double fs = kFs)
{
    double s = 0;
    size_t n = 0;
    for (size_t i = (size_t) (t0 * fs); i < (size_t) (t1 * fs) && i < x.size(); ++i, ++n)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (n, 1));
}
static double dftDb (const std::vector<float>& x, double f, double t0, double len, double fs = kFs)
{
    const size_t i0 = (size_t) (t0 * fs), L = (size_t) (len * fs);
    double re = 0, im = 0;
    for (size_t j = 0; j < L && i0 + j < x.size(); ++j)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * sw::kPi * (j + 0.5) / L), ph = 2 * sw::kPi * f * (double) j / fs;
        re += w * x[i0 + j] * std::cos (ph);
        im -= w * x[i0 + j] * std::sin (ph);
    }
    return 20 * std::log10 (std::sqrt (re * re + im * im) * 4.0 / (double) L + 1e-15);
}
static double hz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
static int64_t S (double t, double fs = kFs) { return (int64_t) (t * fs); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using MM = juce::MidiMessage;

    std::printf ("Parameters, layouts, state\n");
    {
        SteelwaveProcessor p;
        const char* ids[] = { "set", "key", "glide", "strum", "hardness", "brightness", "sustain", "barnoise", "vibrate", "vibdepth",
                              "sympathetic", "pickup", "volcc", "level", "behindbar", "voltype" };
        int missing = 0;
        for (auto* id : ids)
            missing += p.apvts.getParameter (id) == nullptr;
        CHECK (missing == 0 && p.getParameters().size() == 16, "16 parameters");
        CHECK ((int) getPlain (p, "key") == 4 && (int) getPlain (p, "set") == 0, "defaults: key of E, chord set 1");
        CHECK (p.acceptsMidi() && ! p.producesMidi(), "takes MIDI");
        juce::AudioProcessor::BusesLayout mono, stereo, withIn;
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        withIn.inputBuses.add (juce::AudioChannelSet::stereo());
        withIn.outputBuses.add (juce::AudioChannelSet::stereo());
        CHECK (p.checkBusesLayoutSupported (mono) && p.checkBusesLayoutSupported (stereo) && ! p.checkBusesLayoutSupported (withIn),
               "mono or stereo out, no input");
        setPlain (p, "key", 7);
        setPlain (p, "glide", 0.6f);
        setPlain (p, "set", 9);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        SteelwaveProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK ((int) getPlain (q, "key") == 7 && std::fabs (getPlain (q, "glide") - 0.6f) < 1e-3 && (int) getPlain (q, "set") == 9,
               "state round trip");
    }

    std::printf ("Playing\n");
    {
        SteelwaveProcessor p;
        prepare (p, kFs, 512);
        auto silent = render (p, {}, 0.5, { 512 });
        CHECK (rms (silent, 0, 0.5) == 0.0, "silent without notes");
        std::vector<Ev> ev = { { S (0.05), MM::noteOn (1, 60, (juce::uint8) 100) },
                               { S (1.0), MM::pitchWheel (1, 16383) },
                               { S (2.0), MM::noteOff (1, 60) } };
        std::vector<float> r;
        auto x = render (p, ev, 2.6, { 512 }, kFs, &r);
        bool same = true, finite = true;
        float peak = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            same &= x[i] == r[i];
            finite &= std::isfinite (x[i]);
            peak = std::max (peak, std::fabs (x[i]));
        }
        CHECK (finite && same, "finite, both channels identical (mono instrument)");
        CHECK (peak > 0.05f && peak < 0.9f, "a chord at velocity 100 peaks at %.1f dBFS", 20 * std::log10 (peak));
        CHECK (dftDb (x, hz (56), 0.4, 0.3) - dftDb (x, hz (57), 0.4, 0.3) > 20, "C4 plays E major (G#3)");
        CHECK (dftDb (x, hz (57), 1.6, 0.3) - dftDb (x, hz (56), 1.6, 0.3) > 20, "full pitch wheel bends it to A (G# -> A)");
        CHECK (rms (x, 2.4, 2.6) < 0.01 * rms (x, 1.6, 1.9), "key up blocks it");
    }

    std::printf ("Keyswitches and the Set parameter\n");
    {
        SteelwaveProcessor p;
        prepare (p, kFs, 256);
        std::vector<Ev> ev = { { S (0.01), MM::noteOn (1, sw::kKeyswitchLo + 8, (juce::uint8) 100) }, // single set 1
                               { S (0.02), MM::noteOff (1, sw::kKeyswitchLo + 8) },
                               { S (0.05), MM::noteOn (1, 48, (juce::uint8) 100) } };                // C3 -> E3
        auto x = render (p, ev, 1.0, { 256 });
        CHECK ((int) getPlain (p, "set") == 8, "keyswitch G#1 moved the Set parameter to Single 1");
        CHECK (dftDb (x, hz (52), 0.4, 0.3) - dftDb (x, hz (54), 0.4, 0.3) > 20, "and C3 plays a single E3");
        setPlain (p, "set", 2);
        render (p, {}, 0.05, { 256 });
        CHECK (p.uiSet.load() == 2, "moving the parameter changes the set");
    }

    std::printf ("Block sizes and sample rates\n");
    {
        std::vector<Ev> ev;
        std::mt19937 g (5);
        double t = 0.05;
        int prev = -1;
        for (int i = 0; i < 24; ++i)
        {
            const int note = 48 + (int) (g() % 24);
            if (prev >= 0 && g() % 3) ev.push_back ({ S (t), MM::noteOff (1, prev) });
            ev.push_back ({ S (t + 0.001), MM::noteOn (1, note, (juce::uint8) (60 + g() % 60)) });
            ev.push_back ({ S (t + 0.15), MM::pitchWheel (1, (int) (g() % 16384)) });
            ev.push_back ({ S (t + 0.2), MM::controllerEvent (1, 1, (int) (g() % 128)) });
            prev = note;
            t += 0.3;
        }
        auto run = [&] (std::vector<int> blocks) {
            SteelwaveProcessor p;
            prepare (p, kFs, 1024);
            return render (p, ev, t + 0.5, blocks);
        };
        auto a = run ({ 512 }), b = run ({ 1, 7, 64, 333, 1024, 2, 96 });
        double maxDiff = 0;
        for (size_t i = 0; i < a.size(); ++i)
            maxDiff = std::max (maxDiff, (double) std::fabs (a[i] - b[i]));
        CHECK (maxDiff == 0.0, "bit-identical with block sizes 512 and 1..1024 mixed (events are sample-accurate)");
        for (double fs : { 44100.0, 96000.0 })
        {
            SteelwaveProcessor p;
            prepare (p, fs, 480);
            std::vector<Ev> e2 = { { S (0.05, fs), MM::noteOn (1, 60, (juce::uint8) 100) } };
            auto x = render (p, e2, 1.0, { 480 }, fs);
            CHECK (dftDb (x, hz (56), 0.4, 0.3, fs) - dftDb (x, hz (57), 0.4, 0.3, fs) > 20, "%.1f kHz: in tune", fs / 1000);
        }
    }

    std::printf ("Bypass, volume pedal\n");
    {
        SteelwaveProcessor p;
        prepare (p, kFs, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (MM::noteOn (1, 60, (juce::uint8) 100), 0);
        buf.clear();
        p.processBlockBypassed (buf, midi);
        CHECK (buf.getMagnitude (0, 512) == 0.0f, "bypassed: silent");
        std::vector<Ev> ev = { { 0, MM::controllerEvent (1, 11, 0) }, { S (0.05), MM::noteOn (1, 60, (juce::uint8) 100) } };
        auto x = render (p, ev, 0.6, { 512 });
        CHECK (rms (x, 0.1, 0.6) < 1e-6, "CC11 at 0: silent (volume pedal heel down)");
        setPlain (p, "volcc", 4); // off
        auto y = render (p, { { S (0.05), MM::noteOn (1, 60, (juce::uint8) 100) } }, 0.6, { 512 });
        CHECK (rms (y, 0.2, 0.6) > 1e-3, "volume pedal Off: plays at full level");
    }

    std::printf ("Performance\n");
    {
        SteelwaveProcessor p;
        prepare (p, kFs, 512);
        setPlain (p, "set", 7); // Pad: 6-8 strings
        std::vector<Ev> ev;
        for (int i = 0; i < 20; ++i)
            ev.push_back ({ S (0.5 * i), MM::noteOn (1, 60 + (i % 7) * 2 - (i % 7 > 2), (juce::uint8) 90) });
        const auto t0 = std::chrono::steady_clock::now();
        auto x = render (p, ev, 10.0, { 512 });
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (sec < 5.0, "10 s of 8-string legato chords rendered in %.2f s (%.1f%% of one core)", sec, 10.0 * sec);
    }

    std::printf ("Editor\n");
    {
        SteelwaveProcessor p;
        prepare (p, kFs, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (900, 460);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        CHECK (img.getWidth() == 900, "editor opens and paints");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
