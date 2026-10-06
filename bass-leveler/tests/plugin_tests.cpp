// Host-style tests for the real Bass Note Leveler processor.
#include "../Source/PluginProcessor.h"
#include "synth.h"

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
            view.setSample (0, i, in[pos + (size_t) i]), view.setSample (1, i, in[pos + (size_t) i]);
        p.processBlock (view, midi);
        for (int i = 0; i < n; ++i)
            out[pos + (size_t) i] = view.getSample (0, i);
        pos += (size_t) n;
    }
    return out;
}

static std::vector<float> bassLine (unsigned seed, double& total, double resSpread)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    std::array<double, 128> res {};
    std::mt19937 gr (99);
    for (auto& r : res)
        r = std::uniform_real_distribution<double> (-1.0, 1.0) (gr) * resSpread;
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int rep = 0; rep < 3; ++rep)
        for (int p = 28; p <= 43; ++p)
        {
            ev.push_back ({ t, 0.42, p, u (g) * 3.0 });
            t += 0.55;
        }
    total = t + 0.5;
    return synth::render (kFs, ev, total, res, seed);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("Parameters and layouts\n");
    {
        BassLevelerProcessor p;
        const char* ids[] = { "learn", "strength", "mode", "rider", "focus", "boost", "cut", "speed" };
        bool present = true, automatable = true;
        for (auto* id : ids)
        {
            auto* prm = p.apvts.getParameter (id);
            present = present && prm != nullptr;
            automatable = automatable && prm != nullptr && prm->isAutomatable();
        }
        CHECK (present && automatable && p.getParameters().size() == 8, "8 automatable parameters");
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

    std::printf ("Latency\n");
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
        CHECK (md < 1e-7, "with nothing learned the output is the input delayed by the reported latency (diff %.1e)", md);
    }

    std::printf ("Learning, state recall and correction\n");
    {
        double total;
        const auto sig = bassLine (3, total, 6.0);

        BassLevelerProcessor a;
        prepare (a, kFs, 512);
        setPlain (a, "learn", 1.0f);
        render (a, sig, { 512 });
        // let the processor publish what it learned, then switch learning off
        setPlain (a, "learn", 0.0f);
        render (a, std::vector<float> (4800, 0.0f), { 512 });
        BassLevelerProcessor::View v;
        a.getView (v);
        CHECK (v.total >= 44 && v.observedPitches >= 15, "learned %d notes on %d pitches", v.total, v.observedPitches);

        juce::MemoryBlock state;
        a.getStateInformation (state);
        BassLevelerProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        prepare (b, kFs, 512);
        b.getView (v);
        CHECK (v.total >= 44, "project state carries the learned table (%zu bytes, %d notes after reload)", state.getSize(), v.total);

        setPlain (a, "strength", 1.0f);
        setPlain (b, "strength", 1.0f);
        a.prepareToPlay (kFs, 512); // fresh audio state in both, keeping the table
        const auto outA = render (a, sig, { 480 });
        const auto outB = render (b, sig, { 480 });
        double md = 0;
        for (size_t i = 0; i < outA.size(); ++i)
            md = std::max (md, (double) std::fabs (outA[i] - outB[i]));
        CHECK (md < 1e-5, "a reloaded project corrects exactly like the original (max diff %.1e)", md);

        double changed = 0;
        const int lat = a.getLatencySamples();
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            changed = std::max (changed, (double) std::fabs (outA[i] - sig[i - (size_t) lat]));
        CHECK (changed > 0.01, "the correction actually changes the audio (max change %.3f)", changed);

        BassLevelerProcessor c;
        setPlain (c, "strength", 1.0f);
        c.setStateInformation ("garbage", 7);
        juce::MemoryBlock bad;
        bad.append ("<STATE table=\"AAAA\"/>", 21);
        c.setStateInformation (bad.getData(), (int) bad.getSize());
        CHECK (true, "garbage and truncated state do not crash");

        if (const char* dir = std::getenv ("BNL_SNAPSHOT_DIR")) // optional: render the real editor to a PNG
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
            ed->setSize (680, 440);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (juce::String (dir) + "/bass_leveler_ui.png");
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (img, out);
        }

        a.clearTable();
        render (a, std::vector<float> (4800, 0.0f), { 512 });
        a.getView (v);
        CHECK (v.total == 0, "Clear table empties the table");
    }

    std::printf ("Block sizes\n");
    {
        double total;
        const auto sig = bassLine (5, total, 6.0);
        auto go = [&] (const std::vector<int>& blocks) {
            BassLevelerProcessor p;
            prepare (p, kFs, 4096);
            setPlain (p, "learn", 1.0f);
            render (p, sig, blocks);
            setPlain (p, "learn", 0.0f);
            setPlain (p, "strength", 1.0f);
            setPlain (p, "rider", 0.4f);
            p.prepareToPlay (kFs, 4096);
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
        setPlain (p, "learn", 1.0f);
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
            for (auto* id : { "learn", "strength", "mode", "rider", "focus", "boost", "cut", "speed" })
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

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
