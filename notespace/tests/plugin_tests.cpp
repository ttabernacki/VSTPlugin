// Host-style tests for the real Note Space processor.
#include "../Source/PluginProcessor.h"
#include "../../bass-leveler/tests/synth.h"

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
static const char* kIds[] = { "contrast", "tonelock", "fundamental", "repair", "translate", "range" };

static void setPlain (NoteSpaceProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void prepare (NoteSpaceProcessor& p, double sr, int bs)
{
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);
}

static std::vector<float> render (NoteSpaceProcessor& p, const std::vector<float>& in, const std::vector<int>& blocks)
{
    std::vector<float> out (in.size());
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, 131072);
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

static std::vector<float> line (unsigned seed = 3, double mudAmp = 0.05)
{
    std::mt19937 g (seed);
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int rep = 0; rep < 3; ++rep)
        for (int p : { 28, 28, 35, 31, 33, 33, 40, 36 })
        {
            ev.push_back ({ t, 0.30, p, std::uniform_real_distribution<double> (-3.0, 3.0) (g) });
            t += 0.35;
        }
    std::array<double, 128> flat {};
    auto x = synth::render (kFs, ev, t + 0.5, flat, seed);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += (float) (mudAmp * std::sin (2 * 3.14159265358979 * 63.0 * (double) i / kFs));
    return x;
}

static double ampDb (const std::vector<float>& x, double f, double t0, double cycles)
{
    const size_t i0 = (size_t) (t0 * kFs), L = (size_t) std::llround (cycles * kFs / f);
    double re = 0, im = 0;
    for (size_t j = 0; j < L; ++j)
    {
        re += x[i0 + j] * std::cos (2 * 3.14159265358979 * cycles * (double) j / (double) L);
        im -= x[i0 + j] * std::sin (2 * 3.14159265358979 * cycles * (double) j / (double) L);
    }
    return 20.0 * std::log10 (2.0 / (double) L * std::sqrt (re * re + im * im) + 1e-12);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("Parameters and layouts\n");
    {
        NoteSpaceProcessor p;
        bool ok = true;
        for (auto* id : kIds)
            ok = ok && p.apvts.getParameter (id) != nullptr && p.apvts.getParameter (id)->isAutomatable();
        CHECK (ok && p.getParameters().size() == 6, "6 automatable parameters");
        using L = juce::AudioProcessor::BusesLayout;
        auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out) {
            L l;
            l.inputBuses.add (in);
            l.outputBuses.add (out);
            return l;
        };
        const auto M = juce::AudioChannelSet::mono(), S = juce::AudioChannelSet::stereo();
        CHECK (p.checkBusesLayoutSupported (layout (S, S)) && p.checkBusesLayoutSupported (layout (M, M)) && p.checkBusesLayoutSupported (layout (M, S)),
               "stereo, mono and mono-in/stereo-out are supported");
        CHECK (! p.checkBusesLayoutSupported (layout (S, M)), "stereo-in/mono-out is rejected");
        auto* f = p.apvts.getParameter ("fundamental");
        CHECK (f->getText (f->convertTo0to1 (3.0f), 0) == "+3.0 dB", "knob readouts are readable (%s)", f->getText (f->convertTo0to1 (3.0f), 0).toRawUTF8());
    }

    std::printf ("Latency, transparency, effect\n");
    {
        NoteSpaceProcessor p;
        prepare (p, kFs, 512);
        const int lat = p.getLatencySamples();
        CHECK (lat > 0 && lat < (int) (0.15 * kFs), "reports %d samples (%.0f ms)", lat, 1000.0 * lat / kFs);
        const auto sig = line();
        setPlain (p, "contrast", 0.0f);
        const auto out = render (p, sig, { 512 });
        double md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (out[i] - sig[i - (size_t) lat]));
        CHECK (md == 0.0, "everything at neutral: a bit-exact delay (%.1e)", md);

        NoteSpaceProcessor q;
        prepare (q, kFs, 480);
        setPlain (q, "contrast", 1.0f);
        const auto o2 = render (q, sig, { 480 });
        // the mud under a held part of the line: notes 2-3 are the same pitch (E1), 0.65..0.95 s
        const double t0 = 0.70;
        const double mud = ampDb (o2, 63.0, t0 + (double) lat / kFs, 12) - ampDb (sig, 63.0, t0, 12);
        CHECK (mud < -4.0, "Contrast +100%% on a plucked line: the 63 Hz mud under the notes %+.1f dB", mud);
        CHECK (q.pitchHz.load() >= 0.0f && q.noteResIn.load() > -60.0f, "the editor gets pitch and note/residual readings (%.1f dB in, %.1f dB out)",
               q.noteResIn.load(), q.noteResOut.load());
    }

    std::printf ("State, blocks, rates, stress\n");
    {
        NoteSpaceProcessor a, b;
        setPlain (a, "contrast", -0.3f);
        setPlain (a, "tonelock", 0.7f);
        setPlain (a, "fundamental", 2.5f);
        setPlain (a, "repair", 0.4f);
        setPlain (a, "translate", 0.9f);
        setPlain (a, "range", 222.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* id : kIds)
            same = same && std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) < 1e-5f;
        CHECK (same, "all parameters survive a state round trip");
        b.setStateInformation ("garbage", 7);
        CHECK (true, "garbage state does not crash");
    }
    {
        const auto sig = line (7);
        auto go = [&] (const std::vector<int>& blocks, int announced) {
            NoteSpaceProcessor p;
            prepare (p, kFs, announced);
            setPlain (p, "contrast", 0.8f);
            setPlain (p, "tonelock", 0.5f);
            setPlain (p, "translate", 0.5f);
            setPlain (p, "repair", 0.5f);
            return render (p, sig, blocks);
        };
        const auto ref = go ({ 512 }, 512);
        for (auto blocks : { std::vector<int> { 1 }, { 7, 64 }, { 333 }, { 100000 } })
        {
            const auto o = go (blocks, 256);
            double md = 0;
            for (size_t i = 0; i < o.size(); ++i)
                md = std::max (md, (double) std::fabs (o[i] - ref[i]));
            CHECK (md < 1e-6, "blocks starting {%d,...}: same as 512-sample blocks (%.1e)", blocks[0], md);
        }
    }
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        NoteSpaceProcessor p;
        prepare (p, sr, 256);
        for (auto* id : { "contrast", "tonelock", "repair", "translate" })
            setPlain (p, id, 1.0f);
        std::vector<float> in ((size_t) sr * 2);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.4f * std::sin (2.0f * 3.14159265f * 55.0f * (float) i / (float) sr) * (i % 20000 < 12000 ? 1.0f : 0.0f);
        in[1000] = std::numeric_limits<float>::quiet_NaN();
        const auto out = render (p, in, { 100, 257 });
        bool fin = true;
        float peak = 0;
        for (float v : out)
        {
            fin = fin && std::isfinite (v);
            peak = std::max (peak, std::fabs (v));
        }
        CHECK (fin && peak < 3.0f, "%.0f Hz: finite, peak %.2f", sr, peak);
    }
    {
        NoteSpaceProcessor p;
        prepare (p, kFs, 2048);
        std::mt19937 g (42);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, 4096);
        bool fin = true;
        for (int blk = 0; blk < 600; ++blk)
        {
            for (auto* id : kIds)
                if (u (g) < 0.25f)
                    p.apvts.getParameter (id)->setValueNotifyingHost (u (g));
            const int n = 1 + (int) (u (g) * 3000);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    view.setSample (c, i, 0.4f * std::sin (0.007f * (float) (i + blk * 17)) + 0.1f * (u (g) - 0.5f));
            p.processBlock (view, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    fin = fin && std::isfinite (view.getSample (c, i));
        }
        CHECK (fin, "600 blocks of random parameter changes: always finite");
    }

    if (const char* dir = std::getenv ("NSP_SNAPSHOT_DIR"))
    {
        const auto sig = line (11);
        NoteSpaceProcessor p;
        prepare (p, kFs, 480);
        setPlain (p, "contrast", 0.7f);
        setPlain (p, "tonelock", 0.5f);
        setPlain (p, "translate", 0.6f);
        std::vector<float> part (sig.begin(), sig.begin() + (std::ptrdiff_t) (2.25 * kFs)); // stop in the middle of a note
        render (p, part, { 480 });
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        for (auto size : { std::make_pair (720, 468), std::make_pair (600, 390), std::make_pair (1100, 715) })
        {
            ed->setSize (size.first, size.second);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (juce::String (dir) + "/note_space_ui" + (size.first == 720 ? juce::String() : "_" + juce::String (size.first)) + ".png");
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (img, out);
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
