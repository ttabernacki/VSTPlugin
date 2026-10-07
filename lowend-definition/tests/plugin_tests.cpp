// Host-style tests for the real Low-End Definition processor.
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
static const char* kIds[] = { "contrast", "punch", "sustain", "range", "match", "kick", "align", "kickmode" };

static void setPlain (LowEndDefinitionProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void prepare (LowEndDefinitionProcessor& p, double sr, int bs)
{
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);
}

static std::vector<float> render (LowEndDefinitionProcessor& p, const std::vector<float>& in, const std::vector<int>& blocks)
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

static std::vector<float> riff (double& total, unsigned seed = 3)
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
    total = t + 0.5;
    std::array<double, 128> flat {};
    auto x = synth::render (kFs, ev, total, flat, seed);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += (float) (0.03 * std::sin (2 * 3.14159265 * 63.0 * (double) i / kFs));
    return x;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("Parameters and layouts\n");
    {
        LowEndDefinitionProcessor p;
        bool present = true, automatable = true;
        for (auto* id : kIds)
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
            l.inputBuses.add (juce::AudioChannelSet::disabled()); // the optional sidechain, not connected
            l.outputBuses.add (out);
            return l;
        };
        const auto M = juce::AudioChannelSet::mono(), S = juce::AudioChannelSet::stereo();
        CHECK (p.checkBusesLayoutSupported (layout (S, S)) && p.checkBusesLayoutSupported (layout (M, M)) && p.checkBusesLayoutSupported (layout (M, S)),
               "stereo, mono and mono-in/stereo-out are supported (sidechain off)");
        CHECK (! p.checkBusesLayoutSupported (layout (S, M)) && ! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(), S)),
               "stereo-in/mono-out and 5.1 are rejected");
        auto* c = p.apvts.getParameter ("contrast");
        CHECK (c->getText (c->convertTo0to1 (0.5f), 0) == "+50 %" && p.apvts.getParameter ("range")->getText (p.apvts.getParameter ("range")->getDefaultValue(), 0) == "200 Hz",
               "knob readouts are readable (%s, %s)", c->getText (c->convertTo0to1 (0.5f), 0).toRawUTF8(),
               p.apvts.getParameter ("range")->getText (p.apvts.getParameter ("range")->getDefaultValue(), 0).toRawUTF8());
    }

    std::printf ("Latency and transparency\n");
    {
        LowEndDefinitionProcessor p;
        prepare (p, kFs, 512);
        const int lat = p.getLatencySamples();
        CHECK (lat > 0 && lat < (int) (0.2 * kFs), "reports %d samples (%.0f ms) to the host", lat, 1000.0 * lat / kFs);
        CHECK (std::abs (p.getTailLengthSeconds() - (double) lat / kFs) < 1e-6, "tail length covers the delayed audio");
        double total;
        const auto sig = riff (total);
        for (auto* id : { "contrast", "punch", "sustain" })
            setPlain (p, id, 0.0f);
        const auto out = render (p, sig, { 512 });
        double md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (out[i] - sig[i - (size_t) lat]));
        CHECK (md < 1e-7, "all three controls at 0 give a bit-exact delay (diff %.1e)", md);
    }

    std::printf ("It does something audible, and the UI sees it\n");
    {
        LowEndDefinitionProcessor p;
        prepare (p, kFs, 480);
        setPlain (p, "contrast", 1.0f);
        setPlain (p, "punch", 0.8f);
        double total;
        const auto sig = riff (total);
        const auto out = render (p, sig, { 480 });
        double diff = 0, ref = 0;
        for (size_t i = (size_t) p.getLatencySamples(); i < sig.size(); ++i)
        {
            const double a = out[i] - sig[i - (size_t) p.getLatencySamples()];
            diff += a * a;
            ref += (double) sig[i] * sig[i];
        }
        CHECK (10 * std::log10 (diff / ref) > -30.0, "the processed signal differs from the dry one (%.1f dB re dry)", 10 * std::log10 (diff / ref));
        LowEndDefinitionProcessor::Point pts[LowEndDefinitionProcessor::kHist];
        p.getHistory (pts);
        int seen = 0;
        float lastIn = -1, lastOut = -1;
        for (auto& pt : pts)
            if (pt.defIn >= 0)
            {
                ++seen;
                lastIn = pt.defIn;
                lastOut = pt.defOut;
            }
        CHECK (seen > 10 && lastOut >= lastIn - 0.02f, "the editor gets a definition history (%d points, %.0f%% in, %.0f%% out)", seen, 100 * lastIn, 100 * lastOut);
    }


    std::printf ("Sidechain (kick)\n");
    {
        using L = juce::AudioProcessor::BusesLayout;
        auto S = juce::AudioChannelSet::stereo();
        L l;
        l.inputBuses.add (S);
        l.inputBuses.add (S);
        l.outputBuses.add (S);
        LowEndDefinitionProcessor probe;
        CHECK (probe.checkBusesLayoutSupported (l), "stereo + stereo sidechain is a supported layout");
        L l5 = l;
        l5.inputBuses.set (1, juce::AudioChannelSet::create5point1());
        CHECK (! probe.checkBusesLayoutSupported (l5), "a 5.1 sidechain is rejected");

        const double f0 = 41.2, mudHz = 64.0;
        std::vector<float> bass ((size_t) (4 * kFs)), kick (bass.size(), 0.0f);
        for (size_t i = 0; i < bass.size(); ++i)
        {
            const double t = (double) i / kFs, env = std::min (1.0, t / 0.03);
            bass[i] = (float) (0.3 * env * (std::sin (2 * 3.14159265 * f0 * t) + 0.5 * std::sin (2 * 3.14159265 * 2 * f0 * t + 0.4) + 0.3 * std::sin (2 * 3.14159265 * mudHz * t + 0.7)));
        }
        for (int b = 0; b < 6; ++b)
            for (size_t i = 0; i < (size_t) (0.18 * kFs); ++i)
            {
                const size_t n = (size_t) ((0.5 + 0.6 * b) * kFs) + i;
                const double t = (double) i / kFs;
                kick[n] = (float) (0.8 * std::exp (-t / 0.06) * std::sin (2 * 3.14159265 * mudHz * t));
            }
        auto go = [&] (float kickAmount, bool connect) {
            LowEndDefinitionProcessor p;
            if (connect)
            {
                p.setBusesLayout (l);
                p.getBus (true, 1)->enable();
            }
            prepare (p, kFs, 512);
            for (auto* id : { "contrast", "punch", "sustain" })
                setPlain (p, id, 0.0f);
            setPlain (p, "kick", kickAmount);
            std::vector<float> out (bass.size());
            juce::MidiBuffer midi;
            const int nCh = p.getTotalNumInputChannels();
            juce::AudioBuffer<float> buf (std::max (nCh, 2), 512);
            for (size_t pos = 0; pos < bass.size(); pos += 512)
            {
                const int n = (int) std::min<size_t> (512, bass.size() - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
                for (int i = 0; i < n; ++i)
                {
                    view.setSample (0, i, bass[pos + (size_t) i]);
                    view.setSample (1, i, bass[pos + (size_t) i]);
                    if (view.getNumChannels() > 3)
                    {
                        view.setSample (2, i, kick[pos + (size_t) i]);
                        view.setSample (3, i, kick[pos + (size_t) i]);
                    }
                }
                p.processBlock (view, midi);
                for (int i = 0; i < n; ++i)
                    out[pos + (size_t) i] = view.getSample (0, i);
            }
            return out;
        };
        auto amp = [] (const std::vector<float>& x, double f, double t0, double cycles) {
            const size_t i0 = (size_t) (t0 * kFs), L = (size_t) std::llround (cycles * kFs / f);
            double re = 0, im = 0;
            for (size_t j = 0; j < L; ++j)
            {
                re += x[i0 + j] * std::cos (2 * 3.14159265358979 * cycles * (double) j / (double) L);
                im -= x[i0 + j] * std::sin (2 * 3.14159265358979 * cycles * (double) j / (double) L);
            }
            return 20.0 * std::log10 (2.0 / (double) L * std::sqrt (re * re + im * im) + 1e-12);
        };
        const auto wet = go (1.0f, true), dry = go (0.0f, true), nosc = go (1.0f, false);
        LowEndDefinitionProcessor lat;
        prepare (lat, kFs, 512);
        const int L0 = lat.getLatencySamples();
        double md = 0, md2 = 0;
        for (size_t i = (size_t) L0; i < bass.size(); ++i)
        {
            md = std::max (md, (double) std::fabs (dry[i] - bass[i - (size_t) L0]));
            md2 = std::max (md2, (double) std::fabs (nosc[i] - bass[i - (size_t) L0]));
        }
        CHECK (md < 1e-6, "kick 0 %%: a delay, nothing else (%.1e)", md);
        CHECK (md2 < 1e-6, "sidechain not connected: a delay, nothing else (%.1e)", md2);
        const double tHit = 0.5 + 0.6 * 3 + 0.04, shift = (double) L0 / kFs;
        const double mudHit = amp (wet, mudHz, tHit + shift, 6) - amp (bass, mudHz, tHit, 6);
        const double fundHit = amp (wet, f0, tHit + shift, 4) - amp (bass, f0, tHit, 4);
        CHECK (mudHit < -3.0 && std::fabs (fundHit) < 1.5, "through the real processor: mud under the kick %+.1f dB, the note's fundamental %+.1f dB", mudHit, fundHit);
    }


    std::printf ("Sidechain layouts\n");
    {
        // The audio channels are those of the main bus; the sidechain must not be mistaken for audio, must survive a
        // mono-in / stereo-out layout (where the host shares a channel between them), and must not be altered.
        const double f0 = 41.2, mudHz = 64.0;
        std::vector<float> bass ((size_t) (3 * kFs)), kick (bass.size(), 0.0f);
        for (size_t i = 0; i < bass.size(); ++i)
        {
            const double t = (double) i / kFs, env = std::min (1.0, t / 0.03);
            bass[i] = (float) (0.3 * env * (std::sin (2 * 3.14159265 * f0 * t) + 0.5 * std::sin (2 * 3.14159265 * 2 * f0 * t + 0.4) + 0.3 * std::sin (2 * 3.14159265 * mudHz * t + 0.7)));
        }
        for (int b = 0; b < 4; ++b)
            for (size_t i = 0; i < (size_t) (0.18 * kFs); ++i)
            {
                const double t = (double) i / kFs;
                kick[(size_t) ((0.5 + 0.6 * b) * kFs) + i] = (float) (0.8 * std::exp (-t / 0.06) * std::sin (2 * 3.14159265 * mudHz * t));
            }
        struct Cfg
        {
            const char* name;
            juce::AudioChannelSet in, sc, out;
        };
        const auto M = juce::AudioChannelSet::mono(), S = juce::AudioChannelSet::stereo();
        double ref[3] = {};
        int idx = 0;
        for (const Cfg& cfg : { Cfg { "stereo + stereo sidechain", S, S, S }, Cfg { "mono + stereo sidechain, mono out", M, S, M },
                                Cfg { "mono + mono sidechain, stereo out", M, M, S } })
        {
            LowEndDefinitionProcessor p;
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (cfg.in);
            l.inputBuses.add (cfg.sc);
            l.outputBuses.add (cfg.out);
            const bool okLayout = p.setBusesLayout (l);
            p.getBus (true, 1)->enable();
            prepare (p, kFs, 512);
            for (auto* id : { "contrast", "punch", "sustain" })
                setPlain (p, id, 0.0f);
            setPlain (p, "kick", 1.0f);
            const int nIn = p.getTotalNumInputChannels(), nOut = p.getTotalNumOutputChannels(), nBuf = std::max (nIn, nOut);
            const int mainIn = p.getMainBusNumInputChannels(), scN = nIn - mainIn;
            juce::AudioBuffer<float> buf (nBuf, 512);
            juce::MidiBuffer midi;
            std::vector<float> out (bass.size()), out1 (bass.size());
            bool scIntact = true;
            for (size_t pos = 0; pos < bass.size(); pos += 512)
            {
                const int n = (int) std::min<size_t> (512, bass.size() - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), nBuf, n);
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < mainIn; ++c)
                        view.setSample (c, i, bass[pos + (size_t) i]);
                    for (int c = 0; c < scN; ++c)
                        view.setSample (mainIn + c, i, kick[pos + (size_t) i]);
                }
                p.processBlock (view, midi);
                for (int i = 0; i < n; ++i)
                {
                    out[pos + (size_t) i] = view.getSample (0, i);
                    out1[pos + (size_t) i] = nOut > 1 ? view.getSample (1, i) : view.getSample (0, i);
                    // an input-only sidechain channel (not shared with an output) must come back as it went in
                    for (int c = 0; c < scN; ++c)
                        if (mainIn + c >= nOut && view.getSample (mainIn + c, i) != kick[pos + (size_t) i])
                            scIntact = false;
                }
            }
            auto amp = [&] (const std::vector<float>& x, double f, double t0, double cycles) {
                const size_t i0 = (size_t) (t0 * kFs), L = (size_t) std::llround (cycles * kFs / f);
                double re = 0, im = 0;
                for (size_t j = 0; j < L; ++j)
                {
                    re += x[i0 + j] * std::cos (2 * 3.14159265358979 * cycles * (double) j / (double) L);
                    im -= x[i0 + j] * std::sin (2 * 3.14159265358979 * cycles * (double) j / (double) L);
                }
                return 20.0 * std::log10 (2.0 / (double) L * std::sqrt (re * re + im * im) + 1e-12);
            };
            const double sh = (double) p.getLatencySamples() / kFs, tHit = 0.5 + 0.6 * 2 + 0.04;
            const double mud = amp (out, mudHz, tHit + sh, 6) - amp (bass, mudHz, tHit, 6);
            ref[idx] = mud;
            double chDiff = 0;
            for (size_t i = 0; i < out.size(); ++i)
                chDiff = std::max (chDiff, (double) std::fabs (out[i] - out1[i]));
            CHECK (okLayout && mud < -3.0 && scIntact && chDiff < 1e-6, "%s: layout accepted, mud under the kick %+.1f dB, sidechain left intact, outputs agree (%.1e)", cfg.name, mud, chDiff);
            ++idx;
        }
        CHECK (std::fabs (ref[0] - ref[1]) < 0.5 && std::fabs (ref[0] - ref[2]) < 0.5, "the duck is the same whatever the layout (%.1f / %.1f / %.1f dB)", ref[0], ref[1], ref[2]);
    }


    std::printf ("Hosts that exceed the announced block size\n");
    {
        double total;
        const auto sig = riff (total, 5);
        auto go = [&] (int announced, int actual) {
            LowEndDefinitionProcessor p;
            prepare (p, kFs, announced);
            setPlain (p, "contrast", 0.8f);
            setPlain (p, "punch", 0.6f);
            return render (p, sig, { actual });
        };
        const auto ref = go (512, 512);
        const auto big = go (256, 100000); // announced 256, delivered in a single 100000-sample block
        double md = 0;
        for (size_t i = 0; i < ref.size(); ++i)
            md = std::max (md, (double) std::fabs (ref[i] - big[i]));
        CHECK (md < 1e-6, "a block 400 times the announced size is processed in slices, same result (%.1e)", md);
        LowEndDefinitionProcessor p;
        prepare (p, kFs, 512);
        juce::AudioBuffer<float> empty (2, 0);
        juce::MidiBuffer midi;
        p.processBlock (empty, midi);
        CHECK (true, "an empty block is harmless");
    }

    std::printf ("State recall\n");
    {
        LowEndDefinitionProcessor a, b;
        setPlain (a, "contrast", -0.33f);
        setPlain (a, "punch", 0.77f);
        setPlain (a, "sustain", -0.5f);
        setPlain (a, "range", 123.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* id : kIds)
            same = same && std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) < 1e-5f;
        CHECK (same, "all parameters survive a state round trip (%zu bytes)", mb.getSize());
        b.setStateInformation ("garbage", 7);
        CHECK (true, "garbage state does not crash");
    }

    std::printf ("Block sizes\n");
    {
        double total;
        const auto sig = riff (total, 7);
        auto go = [&] (const std::vector<int>& blocks) {
            LowEndDefinitionProcessor p;
            prepare (p, kFs, 4096);
            setPlain (p, "contrast", 0.8f);
            setPlain (p, "punch", 0.6f);
            setPlain (p, "sustain", 0.4f);
            return render (p, sig, blocks);
        };
        const auto ref = go ({ 512 });
        for (auto blocks : { std::vector<int> { 1 }, { 7, 64 }, { 333 }, { 2048, 3, 1000 } })
        {
            const auto o = go (blocks);
            double md = 0;
            for (size_t i = 0; i < o.size(); ++i)
                md = std::max (md, (double) std::fabs (o[i] - ref[i]));
            CHECK (md < 1e-6, "blocks starting {%d,...}: max difference vs 512-sample blocks %.1e", blocks[0], md);
        }
    }

    std::printf ("Sample rates and stress\n");
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        LowEndDefinitionProcessor p;
        prepare (p, sr, 256);
        setPlain (p, "contrast", 1.0f);
        setPlain (p, "punch", 1.0f);
        setPlain (p, "sustain", 1.0f);
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
        CHECK (finite && peak < 3.0f, "sample rate %.0f: finite output, peak %.2f", sr, peak);
    }
    {
        LowEndDefinitionProcessor p;
        prepare (p, kFs, 2048);
        std::mt19937 g (42);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, 4096);
        bool finite = true;
        for (int blk = 0; blk < 800; ++blk)
        {
            for (auto* id : kIds)
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

    if (const char* dir = std::getenv ("LED_SNAPSHOT_DIR")) // optional: render the real editor to a PNG after some playing
    {
        double total;
        const auto sig = riff (total, 11);
        LowEndDefinitionProcessor p;
        prepare (p, kFs, 480);
        setPlain (p, "contrast", 0.7f);
        setPlain (p, "punch", 0.5f);
        setPlain (p, "sustain", -0.3f);
        render (p, sig, { 480 });
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        for (auto size : { std::make_pair (680, 440), std::make_pair (560, 364), std::make_pair (1100, 711) }) // default, smallest, largest
        {
            ed->setSize (size.first, size.second);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (juce::String (dir) + "/low_end_definition_ui" + (size.first == 680 ? juce::String() : "_" + juce::String (size.first)) + ".png");
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (img, out);
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
