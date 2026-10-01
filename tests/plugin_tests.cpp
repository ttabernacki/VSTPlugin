// Host-style tests for the real plugin processor (what a DAW like Ableton Live exercises):
// parameter metadata, state recall, block-size independence, extreme sample rates / buffer
// sizes, automation stress, and timeline-locked Orbit.
#include "../Source/PluginProcessor.h"

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

static void setPlain (OrbitPanAudioProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
static float getPlain (OrbitPanAudioProcessor& p, const char* id)
{
    auto* prm = p.apvts.getParameter (id);
    return prm->convertFrom0to1 (prm->getValue());
}

struct FakePlayHead : juce::AudioPlayHead
{
    juce::int64 pos = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setTimeInSamples (pos);
        i.setIsPlaying (playing);
        return i;
    }
};

static void prepare (OrbitPanAudioProcessor& p, double sr, int bs)
{
    p.setRateAndBufferSizeDetails (sr, bs);
    p.prepareToPlay (sr, bs);
}

static std::vector<float> noise (int n, unsigned seed)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    std::vector<float> v ((size_t) n);
    for (auto& s : v)
        s = d (g);
    return v;
}

// Renders stereo noise through the processor in blocks of the given sizes (cycled).
static std::vector<float> render (OrbitPanAudioProcessor& p, const std::vector<float>& inL, const std::vector<float>& inR,
                                  const std::vector<int>& blockSizes, FakePlayHead* ph = nullptr, int maxBlock = 4096)
{
    std::vector<float> out (inL.size() * 2);
    juce::MidiBuffer midi;
    size_t pos = 0, bi = 0;
    juce::AudioBuffer<float> buf (2, maxBlock);
    while (pos < inL.size())
    {
        const int n = (int) std::min<size_t> ((size_t) blockSizes[bi++ % blockSizes.size()], inL.size() - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        std::copy (inL.begin() + (long) pos, inL.begin() + (long) pos + n, view.getWritePointer (0));
        std::copy (inR.begin() + (long) pos, inR.begin() + (long) pos + n, view.getWritePointer (1));
        p.processBlock (view, midi);
        std::copy (view.getReadPointer (0), view.getReadPointer (0) + n, out.begin() + (long) pos);
        std::copy (view.getReadPointer (1), view.getReadPointer (1) + n, out.begin() + (long) (inL.size() + pos));
        pos += (size_t) n;
        if (ph != nullptr)
            ph->pos += n;
    }
    return out;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("Parameters\n");
    {
        OrbitPanAudioProcessor p;
        const char* ids[] = { "azimuth", "elevation", "distance", "depth", "focus", "rear", "room", "decay", "orbit" };
        bool allAutomatable = true, allPresent = true;
        for (auto* id : ids)
        {
            auto* prm = p.apvts.getParameter (id);
            allPresent = allPresent && prm != nullptr;
            allAutomatable = allAutomatable && prm != nullptr && prm->isAutomatable();
        }
        CHECK (allPresent, "all 9 parameters exist");
        CHECK (allAutomatable, "all parameters are automatable");
        CHECK (p.getParameters().size() == 9, "no stray parameters (%d)", p.getParameters().size());
        setPlain (p, "azimuth", 300.0f);
        CHECK (std::abs (getPlain (p, "azimuth") - 300.0f) < 0.01f, "azimuth holds two-turn values (300 deg)");
        auto* az = p.apvts.getParameter ("azimuth");
        CHECK (az->getText (az->convertTo0to1 (0.0f), 100) == "front", "azimuth text 0 -> '%s'", az->getText (az->convertTo0to1 (0.0f), 100).toRawUTF8());
        CHECK (az->getText (az->convertTo0to1 (190.0f), 100).contains ("L"), "azimuth text 190 -> '%s' (wrapped, left-behind)", az->getText (az->convertTo0to1 (190.0f), 100).toRawUTF8());
        CHECK (az->getText (az->convertTo0to1 (-180.0f), 100) == "behind", "azimuth text -180 -> behind");
        CHECK (std::abs (az->convertFrom0to1 (az->getValueForText ("45 R")) - 45.0f) < 0.01f, "azimuth text entry '45 R'");
        CHECK (std::abs (az->convertFrom0to1 (az->getValueForText ("45 L")) + 45.0f) < 0.01f, "azimuth text entry '45 L'");
    }

    std::printf ("Bus layouts\n");
    {
        OrbitPanAudioProcessor p;
        using L = juce::AudioProcessor::BusesLayout;
        auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out) {
            L l;
            l.inputBuses.add (in);
            l.outputBuses.add (out);
            return l;
        };
        CHECK (p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())), "stereo -> stereo");
        CHECK (p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())), "mono -> stereo");
        CHECK (! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())), "mono -> mono rejected");
        CHECK (! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::stereo())), "5.1 rejected");
    }

    std::printf ("State recall (project save/load, Freeze, Export)\n");
    {
        OrbitPanAudioProcessor a, b;
        setPlain (a, "azimuth", -250.5f);
        setPlain (a, "elevation", 33.3f);
        setPlain (a, "distance", 0.777f);
        setPlain (a, "depth", -0.42f);
        setPlain (a, "focus", 0.11f);
        setPlain (a, "rear", 0.93f);
        setPlain (a, "room", 0.66f);
        setPlain (a, "decay", 0.25f);
        setPlain (a, "orbit", -1.25f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* id : { "azimuth", "elevation", "distance", "depth", "focus", "rear", "room", "decay", "orbit" })
            same = same && std::abs (getPlain (a, id) - getPlain (b, id)) < 1e-4f;
        CHECK (same, "all parameters survive a state round-trip (%zu bytes)", mb.getSize());
        b.setStateInformation ("garbage", 7);
        CHECK (true, "garbage state does not crash");
    }

    std::printf ("Block-size independence\n");
    {
        const int n = 48000;
        const auto l = noise (n, 1), r = noise (n, 2);
        auto run = [&] (const std::vector<int>& sizes) {
            OrbitPanAudioProcessor p;
            prepare (p, 48000.0, 4096);
            setPlain (p, "azimuth", 125.0f);
            setPlain (p, "elevation", 20.0f);
            setPlain (p, "depth", -0.3f);
            setPlain (p, "room", 0.4f);
            return render (p, l, r, sizes);
        };
        const auto ref = run ({ 512 });
        for (auto sizes : { std::vector<int> { 1, 7, 64 }, { 17 }, { 480 }, { 1024 }, { 4096 }, { 33, 2000, 5 } })
        {
            const auto o = run (sizes);
            float md = 0;
            for (size_t i = 0; i < o.size(); ++i)
                md = std::max (md, std::abs (o[i] - ref[i]));
            CHECK (md < 2e-3f, "blocks starting {%d,...}: max difference vs 512-sample blocks %.2e", sizes[0], md);
        }
    }

    std::printf ("Sample rates and buffer sizes\n");
    for (double sr : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int bs : { 16, 128, 1024, 4096 })
        {
            OrbitPanAudioProcessor p;
            prepare (p, sr, bs);
            setPlain (p, "azimuth", 40.0f);
            setPlain (p, "room", 0.5f);
            const int n = (int) sr / 2;
            const auto l = noise (n, 3), r = noise (n, 4);
            const auto o = render (p, l, r, { bs });
            bool finite = true;
            double e = 0, ei = 0;
            for (size_t i = 0; i < o.size(); ++i)
                finite = finite && std::isfinite (o[i]);
            for (int i = n / 4; i < n; ++i)
            {
                e += (double) o[(size_t) i] * o[(size_t) i] + (double) o[(size_t) (n + i)] * o[(size_t) (n + i)];
                ei += (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
            }
            const double dB = 10 * std::log10 (e / ei);
            if (bs == 128 || ! finite || dB < -15 || dB > 12)
                CHECK (finite && dB > -15 && dB < 12, "sr %.0f, block %d: finite, level %+.1f dB vs input", sr, bs, dB);
        }

    std::printf ("Automation stress (random jumps, odd block sizes, NaN input)\n");
    {
        OrbitPanAudioProcessor p;
        prepare (p, 48000.0, 2048);
        std::mt19937 g (42);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, 4096);
        bool finite = true;
        float peak = 0;
        for (int blk = 0; blk < 3000; ++blk)
        {
            for (auto* id : { "azimuth", "elevation", "distance", "depth", "focus", "rear", "room", "decay", "orbit" })
                if (u (g) < 0.3f)
                    p.apvts.getParameter (id)->setValueNotifyingHost (u (g) < 0.2f ? (u (g) < 0.5f ? 0.0f : 1.0f) : u (g));
            const int n = 1 + (int) (u (g) * 3000);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    view.setSample (c, i, u (g) - 0.5f);
            if (blk == 1500)
            {
                view.setSample (0, 3, std::numeric_limits<float>::quiet_NaN());
                view.setSample (1, 9, std::numeric_limits<float>::infinity());
            }
            p.processBlock (view, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    finite = finite && std::isfinite (view.getSample (c, i));
                    peak = std::max (peak, std::abs (view.getSample (c, i)));
                }
        }
        CHECK (finite, "3000 random blocks, output always finite (peak %.2f)", peak);
        CHECK (peak < 20.0f, "output stays bounded");
        buf.clear();
        juce::AudioBuffer<float> empty (buf.getArrayOfWritePointers(), 2, 0);
        p.processBlock (empty, midi);
        CHECK (true, "zero-length block is harmless");
    }

    std::printf ("Orbit locked to the host timeline\n");
    {
        const int sr = 48000, n = 4 * sr;
        const auto l = noise (n, 5), r = noise (n, 6);
        auto setup = [] (OrbitPanAudioProcessor& p) {
            prepare (p, 48000.0, 512);
            setPlain (p, "orbit", 0.6f);
            setPlain (p, "room", 0.0f);
        };
        // A: play from 0 for 4 s
        OrbitPanAudioProcessor pa;
        setup (pa);
        FakePlayHead pha;
        pa.setPlayHead (&pha);
        const auto a = render (pa, l, r, { 512 }, &pha);
        // B: transport starts at 2 s (as after scrubbing, loop or Freeze starting mid-song)
        const int start = 2 * sr;
        const std::vector<float> l2 (l.begin() + start, l.end()), r2 (r.begin() + start, r.end());
        OrbitPanAudioProcessor pb;
        setup (pb);
        FakePlayHead phb;
        phb.pos = start;
        pb.setPlayHead (&phb);
        const auto b = render (pb, l2, r2, { 512 }, &phb);
        float md = 0;
        for (int i = sr / 4; i < (int) l2.size(); ++i) // skip 250 ms of filter history
            md = std::max (md, std::abs (a[(size_t) (start + i)] - b[(size_t) i]));
        CHECK (md < 5e-3f, "same output whether playback starts at 0 s or 2 s (max diff %.2e)", md);

        // and without the lock (no playhead) the phase differs between runs started at different times
        OrbitPanAudioProcessor pc;
        setup (pc);
        const auto c = render (pc, l2, r2, { 512 });
        float mdc = 0;
        for (int i = sr / 4; i < (int) l2.size(); ++i)
            mdc = std::max (mdc, std::abs (a[(size_t) (start + i)] - c[(size_t) i]));
        CHECK (mdc > 0.05f, "control: free-running orbit would differ (max diff %.2f)", mdc);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
