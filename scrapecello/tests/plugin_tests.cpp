// Host-style tests for the Scrape Cello processor: parameters, playing through the plug-in's MIDI path
// (aftertouch, the pedal, MPE), state, the editor.
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

    std::printf ("Parameters and defaults\n");
    {
        ScrapeCelloProcessor p;
        int missing = 0;
        for (auto* id : { "pressure", "pressurerange", "position", "speed", "grit", "brightness", "ring", "body", "vibdepth", "vibrate", "vibdelay",
                          "glide", "mode", "pedalcc", "mpe", "memberrange", "masterrange", "level" })
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
        CHECK (ed != nullptr && ed->getWidth() > 0, "editor opens");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
