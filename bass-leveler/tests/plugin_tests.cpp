// Host-style tests for the real Bass Note Leveler processor.
#include "../Source/PluginProcessor.h"
#include "synth.h"

#include <map>
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

static void setPlain (BassLevelerProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void prepare (BassLevelerProcessor& p, double sr, int bs)
{
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);
}

static std::vector<float> render (BassLevelerProcessor& p, const std::vector<float>& in, const std::vector<int>& blocks)
{
    std::vector<float> out (in.size());
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, 8192);
    size_t pos = 0, bi = 0;
    while (pos < in.size())
    {
        const int n = (int) std::min<size_t> ((size_t) blocks[bi++ % blocks.size()], in.size() - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        for (int i = 0; i < n; ++i)
        {
            view.setSample (0, i, in[pos + (size_t) i]);
            view.setSample (1, i, in[pos + (size_t) i]);
        }
        p.processBlock (view, midi);
        for (int i = 0; i < n; ++i)
            out[pos + (size_t) i] = view.getSample (0, i);
        pos += (size_t) n;
    }
    return out;
}

// std::shuffle and the std distributions are implementation-defined (libc++ on macOS played the notes in another order than
// libstdc++), so the test signals are drawn straight from the generator, the same on every platform
static double uni (std::mt19937& g) { return (double) g() / 4294967296.0 * 2.0 - 1.0; }

static std::vector<synth::Ev> bassEvents (unsigned seed, double& total)
{
    std::mt19937 g (seed);
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int rep = 0; rep < 4; ++rep)
    {
        std::vector<int> pitches;
        for (int p = 28; p <= 43; ++p)
            pitches.push_back (p);
        for (size_t i = pitches.size() - 1; i > 0; --i)
            std::swap (pitches[i], pitches[(size_t) (g() % (i + 1))]);
        for (int p : pitches)
        {
            ev.push_back ({ t, 0.42, p, uni (g) * 3.0 });
            t += 0.55;
        }
    }
    total = t + 0.5;
    return ev;
}

static std::array<double, 128> resonances (double spread)
{
    std::mt19937 g (99);
    std::array<double, 128> res {};
    for (auto& r : res)
        r = uni (g) * spread;
    return res;
}

static double spreadOf (const std::vector<float>& sig, int shift, const std::vector<synth::Ev>& ev)
{
    std::map<int, std::vector<double>> acc;
    for (size_t i = 8; i < ev.size(); ++i)
        acc[ev[i].midi].push_back (synth::measure (sig, kFs, ev[i].midi, ev[i].t + (double) shift / kFs).balDb);
    std::vector<double> means;
    for (auto& kv : acc)
    {
        double s = 0;
        for (double v : kv.second)
            s += v;
        means.push_back (s / (double) kv.second.size());
    }
    double m = 0;
    for (double v : means)
        m += v;
    m /= (double) means.size();
    double s = 0;
    for (double v : means)
        s += (v - m) * (v - m);
    return std::sqrt (s / (double) means.size());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("Parameters and layouts\n");
    {
        BassLevelerProcessor p;
        const char* ids[] = { "amount", "boost", "cut", "speed" };
        bool present = true, automatable = true;
        for (auto* id : ids)
        {
            auto* prm = p.apvts.getParameter (id);
            present = present && prm != nullptr;
            automatable = automatable && prm != nullptr && prm->isAutomatable();
        }
        CHECK (present && automatable && p.getParameters().size() == 4, "4 automatable parameters");
        using L = juce::AudioProcessor::BusesLayout;
        auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out) {
            L l;
            l.inputBuses.add (in);
            l.outputBuses.add (out);
            return l;
        };
        const auto M = juce::AudioChannelSet::mono(), S = juce::AudioChannelSet::stereo();
        CHECK (p.checkBusesLayoutSupported (layout (S, S)) && p.checkBusesLayoutSupported (layout (M, M))
                   && p.checkBusesLayoutSupported (layout (M, S)),
               "stereo, mono and mono-in/stereo-out are supported");
        CHECK (! p.checkBusesLayoutSupported (layout (S, M)) && ! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(), S)),
               "stereo-in/mono-out and 5.1 are rejected");
    }

    std::printf ("Latency and transparency\n");
    {
        BassLevelerProcessor p;
        prepare (p, kFs, 512);
        const int lat = p.getLatencySamples();
        CHECK (lat > 0 && lat < (int) (0.2 * kFs), "reports %d samples (%.0f ms) to the host", lat, 1000.0 * lat / kFs);
        CHECK (std::abs (p.getTailLengthSeconds() - (double) lat / kFs) < 1e-6, "tail length covers the delayed audio");
        std::vector<float> in (48000);
        std::mt19937 g (1);
        for (auto& v : in)
            v = std::uniform_real_distribution<float> (-0.5f, 0.5f) (g);
        const auto out = render (p, in, { 512 });
        double md = 0;
        for (size_t i = (size_t) lat; i < in.size(); ++i)
            md = std::max (md, (double) std::fabs (out[i] - in[i - (size_t) lat]));
        CHECK (md < 1e-7, "audio with no pitch to track passes through untouched, delayed by the reported latency (diff %.1e)", md);

        double total;
        const auto ev = bassEvents (3, total);
        const auto sig = synth::render (kFs, ev, total, resonances (6.0));
        BassLevelerProcessor z;
        prepare (z, kFs, 512);
        setPlain (z, "amount", 0.0f);
        const auto o2 = render (z, sig, { 512 });
        md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (o2[i] - sig[i - (size_t) lat]));
        CHECK (md < 1e-7, "amount 0 is a bit-exact delay (diff %.1e)", md);
    }

    std::printf ("Automatic leveling\n");
    {
        double total;
        const auto ev = bassEvents (5, total);
        const auto sig = synth::render (kFs, ev, total, resonances (7.0), 2);
        BassLevelerProcessor p;
        prepare (p, kFs, 480);
        setPlain (p, "amount", 1.0f);
        setPlain (p, "boost", 12.0f);
        setPlain (p, "cut", 18.0f);
        const auto out = render (p, sig, { 480 });
        const double before = spreadOf (sig, 0, ev), after = spreadOf (out, p.getLatencySamples(), ev);
        CHECK (after < before * 0.5, "no learning pass: spread between pitches %.2f dB -> %.2f dB (-%.0f%%)", before, after,
               100.0 * (1.0 - after / before));
        bnl::RecentNote r[BassLevelerProcessor::kRecent];
        const int n = p.getRecent (r);
        CHECK (n == BassLevelerProcessor::kRecent, "the editor gets the last %d notes to display", n);
    }

    std::printf ("Bypass\n");
    {
        // A host that bypasses the plug-in still compensates its reported latency: bypassed, the output must be the input
        // delayed by exactly that latency (not the input itself, which would come out that much early), and switching
        // between bypassed and active while the audio plays must not click.
        double total;
        const auto bev = bassEvents (7, total);
        const auto sig = synth::render (kFs, bev, total, resonances (6.0));
        auto go = [&] (int toggleEvery) {
            BassLevelerProcessor p;
            prepare (p, kFs, 512);
            setPlain (p, "amount", 1.0f);
            setPlain (p, "boost", 12.0f);
            setPlain (p, "cut", 18.0f);
            std::vector<float> out (sig.size());
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buf (2, 512);
            int blk = 0;
            for (size_t pos = 0; pos < sig.size(); pos += 512, ++blk)
            {
                const int n = (int) std::min<size_t> (512, sig.size() - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                for (int i = 0; i < n; ++i)
                    view.setSample (0, i, sig[pos + (size_t) i]), view.setSample (1, i, sig[pos + (size_t) i]);
                const bool bypassed = toggleEvery == 0 || (toggleEvery > 0 && (blk / toggleEvery) % 2 == 1); // < 0: never
                if (bypassed)
                    p.processBlockBypassed (view, midi);
                else
                    p.processBlock (view, midi);
                for (int i = 0; i < n; ++i)
                    out[pos + (size_t) i] = view.getSample (0, i);
            }
            return std::make_pair (out, p.getLatencySamples());
        };
        const auto [byp, lat] = go (0);
        double md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (byp[i] - sig[i - (size_t) lat]));
        CHECK (lat > 0 && md == 0.0, "bypassed with Amount 100 %%: the input delayed by exactly the reported %d samples (max diff %.1e)", lat, md);
        // switching may not make a step bigger than either state makes on its own (processed, the bass can be much louder)
        const auto tog = go (28).first, act = go (-1).first; // about 0.3 s on, 0.3 s off; and never bypassed
        double stepOut = 0, stepIn = 0, stepAct = 0;
        for (size_t i = (size_t) lat + 1; i < sig.size(); ++i)
        {
            stepOut = std::max (stepOut, (double) std::fabs (tog[i] - tog[i - 1]));
            stepAct = std::max (stepAct, (double) std::fabs (act[i] - act[i - 1]));
            stepIn = std::max (stepIn, (double) std::fabs (sig[i - (size_t) lat] - sig[i - 1 - (size_t) lat]));
        }
        CHECK (stepOut < 1.25 * std::max (stepIn, stepAct), "bypass toggled every 0.3 s while playing: largest sample step %.4f (dry %.4f, never bypassed %.4f)",
               stepOut, stepIn, stepAct);
    }

    std::printf ("State recall\n");
    {
        BassLevelerProcessor a, b;
        setPlain (a, "amount", 0.33f);
        setPlain (a, "boost", 4.5f);
        setPlain (a, "cut", 13.0f);
        setPlain (a, "speed", 77.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* id : { "amount", "boost", "cut", "speed" })
            same = same && std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) < 1e-5f;
        CHECK (same, "all parameters survive a state round trip (%zu bytes)", mb.getSize());
        b.setStateInformation ("garbage", 7);
        CHECK (true, "garbage state does not crash");
    }

    std::printf ("Block sizes\n");
    {
        double total;
        const auto ev = bassEvents (7, total);
        const auto sig = synth::render (kFs, ev, total, resonances (6.0));
        auto go = [&] (const std::vector<int>& blocks) {
            BassLevelerProcessor p;
            prepare (p, kFs, 4096);
            setPlain (p, "amount", 1.0f);
            return render (p, sig, blocks);
        };
        const auto ref = go ({ 512 });
        for (auto blocks : { std::vector<int> { 1 }, { 7, 64 }, { 333 }, { 2048, 3, 1000 } })
        {
            const auto o = go (blocks);
            double md = 0;
            for (size_t i = 0; i < o.size(); ++i)
                md = std::max (md, (double) std::fabs (o[i] - ref[i]));
            CHECK (md < 1e-5, "blocks starting {%d,...}: max difference vs 512-sample blocks %.1e", blocks[0], md);
        }
    }

    std::printf ("Sample rates and stress\n");
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        BassLevelerProcessor p;
        prepare (p, sr, 256);
        setPlain (p, "amount", 1.0f);
        std::mt19937 g (7);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        std::vector<float> in ((size_t) sr * 2);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.4f * std::sin (2.0f * 3.14159265f * 55.0f * (float) i / (float) sr) * (i % 20000 < 12000 ? 1.0f : 0.0f) + 0.02f * u (g);
        in[1000] = std::numeric_limits<float>::quiet_NaN();
        const auto out = render (p, in, { 100, 257 });
        bool finite = true;
        float peak = 0;
        for (float v : out)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::fabs (v));
        }
        CHECK (finite && peak < 2.0f, "sample rate %.0f: finite output, peak %.2f", sr, peak);
    }
    {
        BassLevelerProcessor p;
        prepare (p, kFs, 2048);
        std::mt19937 g (42);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, 4096);
        bool finite = true;
        for (int blk = 0; blk < 800; ++blk)
        {
            for (auto* id : { "amount", "boost", "cut", "speed" })
                if (u (g) < 0.25f)
                    p.apvts.getParameter (id)->setValueNotifyingHost (u (g));
            const int n = 1 + (int) (u (g) * 3000);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    view.setSample (c, i, 0.4f * std::sin (0.03f * (float) (i + blk * 17)) + 0.1f * (u (g) - 0.5f));
            p.processBlock (view, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    finite = finite && std::isfinite (view.getSample (c, i));
        }
        CHECK (finite, "800 blocks of random parameter changes: output always finite");
    }

    if (const char* dir = std::getenv ("BNL_SNAPSHOT_DIR")) // optional: render the real editor to a PNG after some playing
    {
        double total;
        const auto ev = bassEvents (11, total);
        const auto sig = synth::render (kFs, ev, total, resonances (7.0), 4);
        BassLevelerProcessor p;
        prepare (p, kFs, 480);
        setPlain (p, "amount", 0.8f);
        render (p, sig, { 480 });
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (680, 440);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::String (dir) + "/bass_leveler_ui.png");
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
