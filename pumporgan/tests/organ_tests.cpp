// Pump organ core tests: the reeds, the wind, the stops (no JUCE).
#include "../core/ReedOrgan.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <memory>

using namespace ro;

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

static OrganParams only (int stop, Wind w = Wind::Easy)
{
    OrganParams p;
    p.model = Model::Parlour; // the physics tests: the steady parlour wind; the chord organ has its own
    p.wind = w;
    p.stops.fill (false);
    p.stops[(size_t) stop] = true;
    p.airNoise = 0.0;
    p.tuningAge = 0.0;
    return p;
}

struct Rig
{
    std::unique_ptr<ReedOrgan> o = std::make_unique<ReedOrgan>();
    std::vector<float> out;
    explicit Rig (const OrganParams& p) { o->prepare (kFs); o->setParams (p); }
    void run (double sec)
    {
        const size_t n = (size_t) (sec * kFs), at = out.size();
        out.resize (at + n);
        for (size_t i = 0; i < n; i += 64)
            o->process (out.data() + at + i, (int) std::min<size_t> (64, n - i));
    }
    double rms (double t0, double t1) const
    {
        double s = 0;
        size_t a = (size_t) (t0 * kFs), b = std::min (out.size(), (size_t) (t1 * kFs));
        for (size_t i = a; i < b; ++i) s += out[i] * out[i];
        return std::sqrt (s / std::max<size_t> (1, b - a));
    }
    // the strongest frequency near f (Hz), by a fine DFT scan
    double peakNear (double f, double t0, double len) const
    {
        double best = f, bestM = -1;
        const size_t a = (size_t) (t0 * kFs), n = (size_t) (len * kFs);
        for (double c = -40; c <= 40; c += 0.5)
        {
            const double ff = f * std::pow (2.0, c / 1200.0);
            std::complex<double> acc = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / n);
                acc += w * out[a + i] * std::polar (1.0, -2 * kPi * ff * i / kFs);
            }
            if (std::abs (acc) > bestM) { bestM = std::abs (acc); best = ff; }
        }
        // then finer, around it
        const double coarse = best;
        for (double c = -0.5; c <= 0.5; c += 0.05)
        {
            const double ff = coarse * std::pow (2.0, c / 1200.0);
            std::complex<double> acc = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / n);
                acc += w * out[a + i] * std::polar (1.0, -2 * kPi * ff * i / kFs);
            }
            if (std::abs (acc) > bestM) { bestM = std::abs (acc); best = ff; }
        }
        return best;
    }
    // spectral brightness: share of energy above 4x the fundamental
    double brightness (double f, double t0, double len) const
    {
        const size_t a = (size_t) (t0 * kFs), n = (size_t) (len * kFs);
        double lo = 0, hi = 0;
        for (int h = 1; h <= 16; ++h)
        {
            std::complex<double> acc = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / n);
                acc += w * out[a + i] * std::polar (1.0, -2 * kPi * f * h * i / kFs);
            }
            (h <= 3 ? lo : hi) += std::norm (acc);
        }
        return hi / (lo + hi);
    }
};

static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }
static double midiHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

int main()
{
    std::printf ("Reeds\n");
    {
        int off = 0;
        double worst = 0;
        for (int n = 36; n <= 96; n += 6)
        {
            Rig r (only (Diapason));
            r.o->pedal (0.6);
            r.run (0.2);
            r.o->noteOn (n);
            r.run (1.5);
            const double f = r.peakNear (midiHz (n), 1.1, 0.5);
            const double c = cents (f, midiHz (n));
            worst = std::max (worst, std::fabs (c));
            off += std::fabs (c) > 6.0;
        }
        CHECK (off == 0, "C2..C7 in tune at moderate wind (worst %.1f cents; a driven reed sags a few cents)", worst);
    }
    {
        Rig r (only (Diapason));
        r.o->pedal (0.0);
        r.o->noteOn (60);
        r.run (1.0);
        CHECK (r.rms (0.2, 1.0) < 1e-5, "no wind, no sound (%.2g)", r.rms (0.2, 1.0));
    }
    {
        // speech: a bass reed is slower to speak than a treble one, and both slower in a gentle wind
        auto speak = [] (int note, double pedal) {
            Rig r (only (Diapason));
            r.o->pedal (pedal);
            r.run (0.3);
            r.o->noteOn (note);
            double t = 0;
            while (t < 3.0 && r.o->reedAmplitude (note, Diapason) < 0.3)
            {
                r.run (0.002);
                t += 0.002;
            }
            return t;
        };
        const double bass = speak (36, 0.8), treble = speak (84, 0.8), gentle = speak (60, 0.35), firm = speak (60, 0.8);
        CHECK (bass > 3 * treble && gentle > 1.3 * firm, "reed speech: C2 %.0f ms, C6 %.0f ms; C4 gentle wind %.0f ms, firm %.0f ms",
               1000 * bass, 1000 * treble, 1000 * gentle, 1000 * firm);
    }
    {
        // more wind: louder and brighter
        auto at = [] (double pedal, double& b) {
            Rig r (only (Diapason));
            r.o->pedal (pedal);
            r.run (0.3);
            r.o->noteOn (57);
            r.run (1.5);
            b = r.brightness (r.peakNear (midiHz (57), 1.0, 0.5), 1.0, 0.5);
            return r.rms (1.0, 1.5);
        };
        double b1, b2, b3;
        const double l1 = at (0.3, b1), l2 = at (0.6, b2), l3 = at (1.0, b3);
        CHECK (l3 > l2 && l2 > l1 && 20 * std::log10 (l3 / l1) > 8.0, "louder with more wind (%.1f dB from 30%% to full pedal)", 20 * std::log10 (l3 / l1));
        CHECK (b3 > b2 && b2 > b1, "and brighter (upper harmonics %.0f%%, %.0f%%, %.0f%%)", 100 * b1, 100 * b2, 100 * b3);
    }
    {
        // stops: Melodia mellower than the Diapason; the Celeste beats against the Dulciana
        Rig d (only (Diapason)), m (only (Melodia));
        for (auto* r : { &d, &m })
        {
            r->o->pedal (0.7);
            r->run (0.2);
            r->o->noteOn (57);
            r->run (1.5);
        }
        const double bd = d.brightness (d.peakNear (midiHz (57), 1.0, 0.5), 1.0, 0.5), bm = m.brightness (m.peakNear (midiHz (57), 1.0, 0.5), 1.0, 0.5);
        CHECK (bm < bd, "Melodia mellower than Diapason (%.0f%% vs %.0f%% upper harmonics)", 100 * bm, 100 * bd);
        auto p = only (Dulciana);
        p.stops[Celeste] = true;
        Rig c (p);
        c.o->pedal (0.7);
        c.run (0.2);
        c.o->noteOn (69);
        c.run (3.0);
        // the beat: the envelope of A4 swings at ~7 cents = 1.8 Hz
        double lo = 1e9, hi = 0;
        for (double t = 1.0; t < 3.0; t += 0.05)
        {
            const double v = c.rms (t, t + 0.05);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        CHECK (hi > 1.5 * lo, "Celeste beats against the Dulciana (level swings %.1f dB)", 20 * std::log10 (hi / lo));
    }
    std::printf ("Character\n");
    {
        // the Celeste is tuned by beat rate, not cents: a slow beat everywhere, a little quicker up top
        Rig r (only (Dulciana));
        const double lo = r.o->reedFrequency (48, Celeste) - r.o->reedFrequency (48, Dulciana);
        const double hi = r.o->reedFrequency (96, Celeste) - r.o->reedFrequency (96, Dulciana);
        CHECK (lo > 1.0 && lo < 1.6 && hi > 1.6 && hi < 2.4, "Celeste beats %.2f Hz at C3, %.2f Hz at C7 (not 7 cents: that was 4+ Hz up top)", lo, hi);
        // worn tuning: keys a few cents off their marks, every reed a little off its key
        auto worn = only (Diapason);
        worn.tuningAge = 1.0;
        Rig w (worn);
        double spread = 0, rankSpread = 0;
        for (int n = 36; n <= 96; ++n)
        {
            spread = std::max (spread, std::fabs (cents (w.o->reedFrequency (n, Diapason), midiHz (n))));
            rankSpread = std::max (rankSpread, std::fabs (cents (w.o->reedFrequency (n, Diapason), w.o->reedFrequency (n, Melodia))));
        }
        CHECK (spread > 2.0 && spread < 6.0 && rankSpread > 0.5 && rankSpread < 3.0,
               "worn in: keys up to %.1f cents off, a key's stops up to %.1f cents apart", spread, rankSpread);
        // a living pitch: with age, a held note's pitch wanders a little; new, it holds still
        auto drift = [] (double age) {
            auto p = only (Diapason);
            p.tuningAge = age;
            Rig r (p);
            r.o->pedal (0.6);
            r.run (0.2);
            r.o->noteOn (69);
            r.run (6.0);
            double lo = 1e9, hi = -1e9;
            for (double t = 1.0; t < 5.5; t += 0.5)
            {
                const double f = r.peakNear (r.o->reedFrequency (69, Diapason), t, 0.5);
                lo = std::min (lo, f);
                hi = std::max (hi, f);
            }
            return cents (hi, lo);
        };
        const double aged = drift (1.0), fresh = drift (0.0);
        CHECK (aged > 0.4 && aged < 4.0 && fresh < 0.25 && aged > 2.0 * fresh, "pitch wander over 5 s: %.2f cents worn, %.2f cents new", aged, fresh);
    }
    std::printf ("Wind\n");
    {
        // Reservoir: pumping fills it; with the feet still, the reeds empty it and fall silent
        Rig r (only (Diapason, Wind::Reservoir));
        r.o->noteOn (48);
        r.o->noteOn (55);
        r.o->noteOn (60);
        r.o->noteOn (64);
        double t = 0;
        for (; t < 5.0; t += 0.01)
        {
            r.o->pedal (0.5 + 0.45 * std::sin (2 * kPi * 0.9 * t));
            r.run (0.01);
        }
        const double filled = r.o->reservoir(), playing = r.rms (4.0, 5.0);
        for (; t < 25.0; t += 0.01)
            r.run (0.01);
        CHECK (filled > 0.5 && playing > 0.01 && r.rms (24.0, 25.0) < 0.05 * playing,
               "Reservoir: treadling fills it (%.2f) and the chord sounds; feet still, the wind runs out (%.1f dB)", filled,
               20 * std::log10 (r.rms (24.0, 25.0) / playing + 1e-9));
    }
    {
        // Expression: no strokes, no wind; strokes, wind
        Rig r (only (Diapason, Wind::Expression));
        r.o->noteOn (60);
        r.o->pedal (0.8);
        r.run (3.0);
        const double still = r.rms (2.5, 3.0);
        double t = 0;
        for (; t < 2.0; t += 0.01)
        {
            r.o->pedal (0.5 + 0.45 * std::sin (2 * kPi * 0.8 * t));
            r.run (0.01);
        }
        CHECK (still < 1e-3 * r.rms (4.0, 5.0), "Expression: a still pedal is no wind, strokes are (%.2g -> %.2g)", still, r.rms (4.0, 5.0));
    }
    {
        // the wind sags under a big chord: one held note gets quieter when many come in
        auto p = only (Diapason);
        p.stops[Melodia] = p.stops[SubBass] = true;
        Rig r (p);
        r.o->pedal (0.6);
        r.run (0.2);
        r.o->noteOn (72);
        r.run (1.5);
        const double alone = r.o->reedAmplitude (72, Diapason);
        for (int n : { 36, 40, 43, 48, 52, 55, 60, 64, 67 })
            r.o->noteOn (n);
        r.run (1.5);
        const double loaded = r.o->reedAmplitude (72, Diapason);
        CHECK (loaded < 0.95 * alone, "a big chord sags the wind: the held C5's swing %.2f -> %.2f", alone, loaded);
    }
    std::printf ("Chord organ\n");
    {
        auto co = [] (std::initializer_list<int> stops) {
            OrganParams p;
            p.stops.fill (false);
            for (int st : stops) p.stops[(size_t) st] = true;
            p.airNoise = 0.0;
            p.tuningAge = 0.0;
            return p;
        };
        // switched on, the fan spins up: the first moments are weak
        {
            Rig r (co ({ Diapason }));
            r.o->pedal (0.8);
            r.o->noteOn (64);
            r.run (4.0);
            CHECK (r.rms (0.3, 0.6) < 0.5 * r.rms (3.0, 4.0), "the fan spins up from switch-on (%.1f dB at 0.3 s)", 20 * std::log10 (r.rms (0.3, 0.6) / r.rms (3.0, 4.0)));
        }
        // less powerful: at full pedal the reeds swing less than the parlour organ's
        double coAmp, parAmp;
        {
            Rig r (co ({ Diapason }));
            r.o->pedal (1.0);
            r.run (3.0);
            r.o->noteOn (64);
            r.run (1.5);
            coAmp = r.o->reedAmplitude (64, Diapason);
            Rig q (only (Diapason));
            q.o->pedal (1.0);
            q.run (0.3);
            q.o->noteOn (64);
            q.run (1.5);
            parAmp = q.o->reedAmplitude (64, Diapason);
            CHECK (coAmp < 0.75 * parAmp, "less powerful: full wind swings a reed %.2f, against the parlour organ's %.2f", coAmp, parAmp);
        }
        // saggy: a big chord drags the wind down; the held note droops flat and quieter, and slowly
        {
            Rig r (co ({ Diapason, Melodia }));
            r.o->pedal (0.85);
            r.run (3.0);
            r.o->noteOn (76);
            r.run (1.5);
            const double f0 = r.peakNear (r.o->reedFrequency (76, Diapason), 4.0, 0.5), a0 = r.o->reedAmplitude (76, Diapason);
            const double s0 = r.o->chestSuction();
            for (int n : { 41, 53, 55, 60, 65, 67, 72 }) // nothing with a harmonic near E5, which would fool the pitch measurement
                r.o->noteOn (n);
            r.run (0.05);
            const double sQuick = r.o->chestSuction();
            r.run (2.0);
            const double f1 = r.peakNear (r.o->reedFrequency (76, Diapason), 6.0, 0.5), a1 = r.o->reedAmplitude (76, Diapason);
            const double s1 = r.o->chestSuction();
            CHECK (s1 < 0.7 * s0 && sQuick > s1 + 0.3 * (s0 - s1), "a big chord sags the wind to %.0f%%, settling over a moment, not at once", 100 * s1 / s0);
            CHECK (cents (f1, f0) < -6.0 && a1 < 0.85 * a0, "the held E5 droops %.1f cents and its swing falls %.0f%%", cents (f1, f0), 100 * (1 - a1 / a0));
        }
        // struggling on little wind: the pitch warbles and the level wavers far more than on full wind
        {
            auto wobble = [&] (double pedal, double& pitchRange, double& levelRange) {
                OrganParams p = co ({ Diapason, Melodia });
                p.airNoise = 1.0;
                p.tuningAge = 1.0;
                Rig r (p);
                r.o->pedal (pedal);
                r.run (2.5);
                for (int n : { 57, 60, 64 })
                    r.o->noteOn (n);
                r.run (8.0);
                double flo = 1e9, fhi = -1e9, llo = 1e9, lhi = 0;
                for (double t = 4.0; t < 10.5; t += 0.25)
                {
                    const double f = r.peakNear (r.o->reedFrequency (64, Diapason), t, 0.25);
                    flo = std::min (flo, f);
                    fhi = std::max (fhi, f);
                    const double l = r.rms (t, t + 0.25);
                    llo = std::min (llo, l);
                    lhi = std::max (lhi, l);
                }
                pitchRange = cents (fhi, flo);
                levelRange = 20 * std::log10 (lhi / llo);
                return r.o->struggle();
            };
            double pLow, lLow, pHigh, lHigh;
            const double sLow = wobble (0.4, pLow, lLow), sHigh = wobble (1.0, pHigh, lHigh);
            CHECK (sLow > 0.3 && sHigh < 0.15, "it labours on little wind (struggle %.2f at 40%% pedal, %.2f at full)", sLow, sHigh);
            CHECK (pLow > 2.0 * pHigh && pLow > 6.0, "and warbles: the pitch wanders %.1f cents at 40%% pedal, %.1f at full", pLow, pHigh);
            CHECK (lLow > 1.5 * lHigh && lLow > 3.0, "and sputters: the level wavers %.1f dB at 40%% pedal, %.1f dB at full", lLow, lHigh);
        }
        // more worn: keys well out
        {
            OrganParams p = co ({ Diapason });
            p.tuningAge = 1.0;
            Rig r (p);
            double spread = 0;
            int lazy = 0;
            for (int n = 36; n <= 96; ++n)
                spread = std::max (spread, std::fabs (cents (r.o->reedFrequency (n, Diapason), midiHz (n))));
            (void) lazy;
            CHECK (spread > 5.0 && spread < 14.0, "worn: keys up to %.1f cents off (the parlour organ: ~4)", spread);
        }
    }
    std::printf ("Robustness\n");
    {
        auto p = only (Diapason);
        p.stops.fill (true);
        p.voxHumana = p.forte = true;
        p.airNoise = 1.0;
        Rig r (p);
        Rng g;
        bool finite = true;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 400; ++i)
        {
            const int n = 36 + (int) ((g.next() * 0.5 + 0.5) * 60);
            if (g.next() > 0) r.o->noteOn (n); else r.o->noteOff (n);
            r.o->pedal (0.5 + 0.5 * g.next());
            if (i % 100 == 0) { auto q = p; q.wind = (Wind) ((i / 100) % 3); r.o->setParams (q); }
            r.run (0.05);
        }
        for (float v : r.out) finite = finite && std::isfinite (v);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (finite, "20 s of random playing on all stops, every wind mode: finite");
        // CPU: 10 notes on all six stops
        Rig c (p);
        c.o->pedal (0.8);
        for (int n : { 36, 43, 48, 52, 55, 60, 64, 67, 72, 76 })
            c.o->noteOn (n);
        const auto t1 = std::chrono::steady_clock::now();
        c.run (5.0);
        const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count() / 5.0;
        CHECK (cpu < 0.5, "10 notes x 6 stops (%d reeds): %.0f%% of one core (random test took %.1f s)", c.o->activeReeds(), 100 * cpu, sec);
    }
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
