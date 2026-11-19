// Scrape Cello core tests: the bow's physics and the instrument (no JUCE).
#include "../core/ScrapeCello.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace sc;

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
static double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }
static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

struct Rig
{
    std::unique_ptr<ScrapeCello> c = std::make_unique<ScrapeCello>();
    std::vector<float> out;
    explicit Rig (CelloParams p) { c->prepare (kFs); c->setParams (p); }
    void send (std::initializer_list<int> b)
    {
        uint8_t d[3] = {};
        int n = 0;
        for (int x : b) d[n++] = (uint8_t) x;
        c->midi (d, n);
    }
    void run (double sec)
    {
        const size_t n = (size_t) (sec * kFs), at = out.size();
        out.resize (at + n);
        for (size_t i = 0; i < n; i += 64)
            c->process (out.data() + at + i, (int) std::min<size_t> (64, n - i));
    }
    double rms (double t0, double t1) const
    {
        double s = 0;
        const size_t a = (size_t) (t0 * kFs), b = std::min (out.size(), (size_t) (t1 * kFs));
        for (size_t i = a; i < b; ++i) s += out[i] * out[i];
        return std::sqrt (s / std::max<size_t> (1, b - a));
    }
    double mag (double f, size_t a, size_t n) const
    {
        std::complex<double> acc = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / n);
            acc += w * out[a + i] * std::polar (1.0, -2 * kPi * f * i / kFs);
        }
        return std::norm (acc);
    }
    double pitch (double f0, double t0, double len) const
    {
        const size_t a = (size_t) (t0 * kFs), n = (size_t) (len * kFs);
        double best = 0, bf = f0;
        for (double c = -100; c <= 100; c += 0.5)
        {
            const double f = f0 * std::pow (2.0, c / 1200.0), m = mag (f, a, n);
            if (m > best) { best = m; bf = f; }
        }
        return bf;
    }
    // share of the energy (60 Hz - 6 kHz) at harmonics of f0: 1 = a clean periodic tone
    double purity (double f0, double t0, double len) const
    {
        const size_t a = (size_t) (t0 * kFs), n = (size_t) (len * kFs);
        double h = 0, tot = 0;
        for (double f = 60; f < 6000; f += 5)
        {
            const double m = mag (f, a, n);
            tot += m;
            const double q = f / f0;
            if (std::fabs (q - std::round (q)) * f0 < 7) h += m;
        }
        return h / tot;
    }
    // share of the energy above 1.5 kHz (brightness)
    double bright (double t0, double len) const
    {
        const size_t a = (size_t) (t0 * kFs), n = (size_t) (len * kFs);
        double hi = 0, tot = 0;
        for (double f = 60; f < 8000; f += 10)
        {
            const double m = mag (f, a, n);
            tot += m;
            if (f > 1500) hi += m;
        }
        return hi / tot;
    }
};

static CelloParams quiet()
{
    CelloParams p;
    p.vibDepth = 0;
    return p;
}

int main()
{
    std::printf ("The bowed string\n");
    {
        int off = 0;
        double worst = 0;
        for (int n = 36; n <= 84; n += 4)
        {
            Rig r (quiet());
            r.c->noteOn (n, 0.7);
            r.run (1.2);
            const double c = cents (r.pitch (midiHz (n), 0.6, 0.5), midiHz (n));
            worst = std::max (worst, std::fabs (c));
            off += std::fabs (c) > 8.0;
        }
        CHECK (off == 0, "C2..C6 bowed at the default pressure land in tune (worst %.1f cents)", worst);
    }
    double pClean, pEdge, pGrind;
    {
        auto at = [] (double press) {
            Rig r (quiet());
            r.c->noteOn (50, 0.7);
            r.send ({ 0xd0, (int) std::lround (press * 127) });
            r.run (1.4);
            return r.purity (r.pitch (midiHz (50), 0.8, 0.5), 0.8, 0.5);
        };
        // the knob at 0.45 and aftertouch on top: 0 (clean), part way (the edge), full (the grind)
        pClean = at (0.0);
        pEdge = at (0.4);
        pGrind = at (1.0);
        CHECK (pClean > 0.95, "default pressure: a clean, periodic tone (%.0f%% harmonic)", 100 * pClean);
        CHECK (pGrind < 0.3, "full pressure: the stick-slip breaks down into a grind (%.0f%% harmonic)", 100 * pGrind);
        CHECK (pEdge < pClean && pEdge > pGrind, "and part way, the gritty edge in between (%.0f%%)", 100 * pEdge);
    }
    {
        // too little pressure: a breathy, weak tone, not silence
        auto p = quiet();
        p.pressure = 0.05;
        Rig r (p);
        r.c->noteOn (50, 0.7);
        r.run (1.2);
        Rig n (quiet());
        n.c->noteOn (50, 0.7);
        n.run (1.2);
        CHECK (r.rms (0.6, 1.1) > 0.02 * n.rms (0.6, 1.1) && r.rms (0.6, 1.1) < n.rms (0.6, 1.1),
               "a feather-light bow still sounds, more weakly (%.1f dB)", 20 * std::log10 (r.rms (0.6, 1.1) / n.rms (0.6, 1.1)));
    }
    {
        // position: at the bridge brighter than over the fingerboard, at much the same level
        auto at = [] (double pos, double& level) {
            auto p = quiet();
            p.position = pos;
            Rig r (p);
            r.c->noteOn (43, 0.7);
            r.run (1.2);
            level = r.rms (0.6, 1.1);
            return r.bright (0.6, 0.5);
        };
        double lt, lp;
        const double tasto = at (0.0, lt), ponti = at (0.95, lp);
        CHECK (ponti > 2.0 * tasto, "ponticello is brighter than sul tasto (%.1f%% vs %.1f%% above 1.5 kHz)", 100 * ponti, 100 * tasto);
        CHECK (std::fabs (20 * std::log10 (lp / lt)) < 6.0, "at much the same level (%.1f dB apart)", 20 * std::log10 (lp / lt));
    }
    {
        // a slow bow grinds sooner: the same pressure clean at speed, rough when slow (the pedal)
        auto at = [] (double pedal) {
            Rig r (quiet());
            r.send ({ 0xb0, 11, (int) std::lround (pedal * 127) });
            r.c->noteOn (50, 0.7);
            r.send ({ 0xd0, 60 });
            r.run (1.4);
            return r.purity (midiHz (50), 0.8, 0.5);
        };
        const double fast = at (1.0), slow = at (0.12);
        (void) fast;
        CHECK (slow < 0.95, "a slow bow on the pedal creaks and roughens (%.0f%% harmonic)", 100 * slow);
    }

    std::printf ("Playing\n");
    {
        // velocity: a hard key bites at once, a soft one swells in
        auto rise = [] (double vel) {
            Rig r (quiet());
            r.c->noteOn (50, vel);
            r.run (0.6);
            const double full = r.rms (0.45, 0.55);
            for (double t = 0.0; t < 0.5; t += 0.005)
                if (r.rms (t, t + 0.005) > 0.5 * full) return t;
            return 0.5;
        };
        const double hard = rise (1.0), soft = rise (0.15);
        CHECK (hard < 0.06 && soft > 2.0 * hard, "a hard key bites (%.0f ms to half level), a soft one swells (%.0f ms)", 1000 * hard, 1000 * soft);
    }
    {
        // the bow lifts: the string rings on a moment, then dies
        Rig r (quiet());
        r.c->noteOn (50, 0.7);
        r.run (1.0);
        r.c->noteOff (50);
        r.run (2.0);
        CHECK (r.rms (1.02, 1.1) > 0.1 * r.rms (0.8, 0.95) && r.rms (2.6, 3.0) < 0.01 * r.rms (0.8, 0.95) && r.c->activeVoices() == 0,
               "the bow lifts: it rings on briefly, then dies away, and the voice is freed");
    }
    {
        // mono: legato glides under one bow; poly: separate bows
        auto p = quiet();
        p.mono = true;
        Rig r (p);
        r.c->noteOn (50, 0.7);
        r.run (0.6);
        r.c->noteOn (55, 0.7);
        r.run (0.03);
        const double mid = r.c->voicePitch (55);
        r.run (0.6);
        CHECK (mid > 50.2 && mid < 54.8 && std::fabs (r.c->voicePitch (55) - 55) < 0.05 && r.c->activeVoices() == 1,
               "mono: a legato note glides under the same bow (%.1f on the way, then 55)", mid);
        r.c->noteOff (55);
        r.run (0.6);
        CHECK (std::fabs (r.c->voicePitch (50) - 50) < 0.05, "letting go glides back to the note still held");
        Rig q (quiet());
        for (int n : { 36, 43, 50, 57 })
            q.c->noteOn (n, 0.7);
        q.run (0.5);
        CHECK (q.c->activeVoices() == 4, "poly: four notes, four bows");
    }

    std::printf ("MPE\n");
    {
        Rig r (quiet());
        r.send ({ 0x91, 45, 90 }); // channel 2
        r.send ({ 0x92, 52, 90 }); // channel 3
        r.run (0.6);
        const int up3 = 8192 + (int) std::lround (8191 * 3.0 / 48.0);
        r.send ({ 0xe2, up3 & 0x7f, up3 >> 7 }); // channel 3 bends up 3
        r.send ({ 0xd1, 127 });                   // channel 2 presses all the way
        r.run (1.4);
        CHECK (std::fabs (r.c->voicePitch (52) - 55.0) < 0.05 && std::fabs (r.c->voicePitch (45) - 45.0) < 0.05,
               "per-note bend: channel 3's E3 bends to G3, channel 2's A2 stays");
        // channel 2 grinds, channel 3 sings: look at each one's pitch region
        const double pa = r.purity (midiHz (45), 1.2, 0.6);
        Rig solo (quiet());
        solo.send ({ 0x91, 45, 90 });
        solo.run (1.8);
        CHECK (pa < 0.7 * solo.purity (midiHz (45), 1.2, 0.6), "per-note pressure: channel 2's note grinds on its own");
    }

    std::printf ("Robustness\n");
    {
        Rig r (quiet());
        Rng g;
        for (int i = 0; i < 300; ++i)
        {
            const int n = 30 + (int) ((g.next() * 0.5 + 0.5) * 60);
            if (g.next() > 0) r.c->noteOn (n, 0.5 + 0.5 * g.next()); else r.c->noteOff (n);
            r.send ({ 0xd0, (int) ((g.next() * 0.5 + 0.5) * 127) });
            r.send ({ 0xb0, 1, (int) ((g.next() * 0.5 + 0.5) * 127) });
            r.send ({ 0xb0, 11, (int) ((g.next() * 0.5 + 0.5) * 127) });
            r.run (0.05);
        }
        bool finite = true;
        float peak = 0;
        for (float v : r.out) { finite = finite && std::isfinite (v); peak = std::max (peak, std::fabs (v)); }
        CHECK (finite && peak < 4.0f, "15 s of random notes, pressure, position and speed: finite, peak %.2f", peak);
        Rig c (quiet());
        for (int n : { 36, 43, 50, 57 })
            c.c->noteOn (n, 0.8);
        c.send ({ 0xd0, 127 });
        const auto t0 = std::chrono::steady_clock::now();
        c.run (5.0);
        const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 5.0;
        CHECK (cpu < 0.2, "four notes grinding: %.1f%% of one core", 100 * cpu);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
