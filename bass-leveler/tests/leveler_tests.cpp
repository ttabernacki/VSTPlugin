// Tests for the Bass Note Leveler DSP core (no JUCE needed).
#include "../core/Leveler.h"
#include "synth.h"

#include <cstdio>
#include <map>
#include <random>

using namespace bnl;

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

// a bass line over 16 pitches (E1..G2), shuffled repeats, random velocities
static std::vector<synth::Ev> makeLine (unsigned seed, int repeats, double velSpreadDb, double& total)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    std::vector<synth::Ev> ev;
    double t = 0.3;
    for (int r = 0; r < repeats; ++r)
    {
        std::vector<int> pitches;
        for (int p = 28; p <= 43; ++p)
            pitches.push_back (p);
        std::shuffle (pitches.begin(), pitches.end(), g);
        for (int p : pitches)
        {
            ev.push_back ({ t, 0.42, p, u (g) * velSpreadDb });
            t += 0.55;
        }
    }
    total = t + 0.5;
    return ev;
}

static std::array<double, 128> makeResonances (unsigned seed, double spreadDb)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    std::array<double, 128> r {};
    for (auto& v : r)
        v = u (g) * spreadDb;
    return r;
}

static std::vector<float> run (Leveler& l, const std::vector<float>& in, int block, const Params& p)
{
    l.setParams (p);
    std::vector<float> out = in;
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - i);
        float* ch[1] = { out.data() + i };
        l.process (ch, 1, n);
    }
    return out;
}

static double stddev (const std::vector<double>& v)
{
    double m = 0;
    for (double x : v)
        m += x;
    m /= (double) v.size();
    double s = 0;
    for (double x : v)
        s += (x - m) * (x - m);
    return std::sqrt (s / (double) v.size());
}

// per-pitch mean of a metric over the notes of a line (skipping the first `skip` notes, where the
// automatic reference is still warming up)
static std::vector<double> perPitch (const std::vector<float>& sig, int shift, const std::vector<synth::Ev>& ev, size_t skip)
{
    std::map<int, std::vector<double>> acc;
    for (size_t i = skip; i < ev.size(); ++i)
    {
        auto m = synth::measure (sig, kFs, ev[i].midi, ev[i].t + (double) shift / kFs);
        acc[ev[i].midi].push_back (m.lvlDb);
    }
    std::vector<double> out;
    for (auto& kv : acc)
    {
        double s = 0;
        for (double v : kv.second)
            s += v;
        out.push_back (s / (double) kv.second.size());
    }
    return out;
}

static int matchNotes (const Leveler& l, const std::vector<synth::Ev>& ev, bool pitchToo = true)
{
    int ok = 0;
    for (const auto& e : ev)
        for (const auto& n : l.notesLog)
            if (std::fabs (n.startSec - e.t) < 0.06 && (! pitchToo || std::fabs (n.midi - e.midi) < 0.3f))
            {
                ++ok;
                break;
            }
    return ok;
}

int main()
{
    std::printf ("Pitch and note tracking\n");
    {
        double total;
        const auto ev = makeLine (1, 3, 3.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (7, 0.0));
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        run (l, sig, 256, Params {});
        CHECK (matchNotes (l, ev, false) >= (int) ev.size() * 95 / 100, "notes found: %d of %zu", matchNotes (l, ev, false), ev.size());
        CHECK (matchNotes (l, ev) >= (int) ev.size() * 95 / 100, "notes found with the right pitch: %d of %zu", matchNotes (l, ev), ev.size());
        CHECK (l.notesLog.size() <= ev.size() + 2, "no phantom notes (%zu logged for %zu played)", l.notesLog.size(), ev.size());
        std::printf ("  latency %d samples = %.1f ms\n", l.latencySamples(), 1000.0 * l.latencySamples() / kFs);
    }

    std::printf ("Harder playing\n");
    {
        std::vector<synth::Ev> ev;
        double t = 0.3;
        for (int i = 0; i < 24; ++i)
        {
            ev.push_back ({ t, 0.17, i % 3 == 2 ? 33 : 31, 0.0 });
            t += 0.21;
        }
        const auto sig = synth::render (kFs, ev, t + 0.5, makeResonances (1, 0.0));
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        run (l, sig, 256, Params {});
        CHECK (matchNotes (l, ev) >= (int) ev.size() * 9 / 10, "fast 8ths: %d of %zu notes tracked (%zu logged)", matchNotes (l, ev), ev.size(), l.notesLog.size());
    }
    {
        std::vector<synth::Ev> ev;
        double t = 0.3;
        for (int i = 0; i < 8; ++i)
        {
            synth::Ev a { t, 0.32, 31 + i % 3, 0.0 }; // the old string stops as the new note starts
            synth::Ev b { t + 0.30, 0.50, 34 + i % 3, 0.0 };
            b.attack = 0.02;
            b.pick = false;
            ev.push_back (a);
            ev.push_back (b);
            t += 1.3;
        }
        const auto sig = synth::render (kFs, ev, t + 0.5, makeResonances (1, 0.0));
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        run (l, sig, 256, Params {});
        int hit = 0;
        for (size_t i = 1; i < ev.size(); i += 2)
            for (const auto& n : l.notesLog)
                if (n.startSec > ev[i].t - 0.02 && n.startSec < ev[i].t + 0.25 && std::fabs (n.midi - ev[i].midi) < 0.3f)
                {
                    ++hit;
                    break;
                }
        CHECK (hit >= 7, "hammer-ons picked up as new notes with the right pitch: %d of 8", hit);
    }
    {
        std::vector<synth::Ev> ev;
        synth::Ev s1 { 0.3, 1.2, 31, 0.0 };
        s1.glideTo = 36.0;
        ev.push_back (s1);
        const auto sig = synth::render (kFs, ev, 2.2, makeResonances (1, 0.0));
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        run (l, sig, 256, Params {});
        CHECK (l.notesLog.size() == 1, "a 5-semitone slide stays one note (%zu logged)", l.notesLog.size());
    }

    std::printf ("Automatic leveling (no learning pass)\n");
    {
        double total;
        const auto ev = makeLine (5, 4, 3.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (11, 7.0), 2);
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 1.0f;
        p.maxBoostDb = 12.0f;
        p.maxCutDb = 18.0f;
        const auto out = run (l, sig, 512, p);
        const double before = stddev (perPitch (sig, 0, ev, 8));
        const double after = stddev (perPitch (out, l.latencySamples(), ev, 8));
        CHECK (after < before * 0.65, "spread between pitches %.2f dB -> %.2f dB (-%.0f%%)", before, after, 100.0 * (1.0 - after / before));
    }
    {
        // within-pitch variation (playing dynamics)
        std::mt19937 g (21);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        std::vector<synth::Ev> ev;
        double t = 0.3;
        for (int i = 0; i < 48; ++i)
        {
            ev.push_back ({ t, 0.42, 30 + (i % 4), u (g) * 4.0 });
            t += 0.55;
        }
        const auto sig = synth::render (kFs, ev, t + 0.5, makeResonances (1, 0.0), 1);
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 1.0f;
        const auto out = run (l, sig, 512, p);
        auto noteStd = [&] (const std::vector<float>& s, int shift) {
            std::vector<double> v;
            for (size_t i = 10; i < ev.size(); ++i)
                v.push_back (synth::measure (s, kFs, ev[i].midi, ev[i].t + (double) shift / kFs).lvlDb);
            return stddev (v);
        };
        const double before = noteStd (sig, 0), after = noteStd (out, l.latencySamples());
        CHECK (after < before * 0.6, "evens out playing dynamics: note-to-note spread %.2f dB -> %.2f dB", before, after);
    }
    {
        // the reference needs a few notes before it is trusted
        double total;
        const auto ev = makeLine (9, 1, 0.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (3, 7.0));
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        Params p;
        p.amount = 1.0f;
        run (l, sig, 256, p);
        CHECK (l.notesLog.size() > 6 && l.notesLog[0].corrDb == 0.0f && l.notesLog[1].corrDb == 0.0f,
               "the first two notes are left alone while the reference forms");
    }

    {
        // Notes whose energy sits on the 2nd-4th harmonics (weak fundamental) next to notes with a strong fundamental, all
        // equally loud. Measured by the fundamental alone, the strong notes looked 8 dB too loud and were cut by that much
        // (found on a real recording); measured as whole notes, nothing here needs correcting.
        std::vector<float> x ((size_t) (14.0 * kFs), 0.0f);
        const double strong[4] = { 1.0, 0.55, 0.32, 0.18 }, weak[4] = { 0.12, 0.9, 1.0, 0.7 };
        auto energy = [] (const double* h) { double e = 0; for (int i = 0; i < 4; ++i) e += h[i] * h[i]; return e; };
        const double ks = 0.3 / std::sqrt (energy (strong)), kw = 0.3 / std::sqrt (energy (weak));
        int count = 0;
        for (double t0 = 0.3; t0 + 0.5 < 14.0; t0 += 0.6, ++count)
        {
            const bool isStrong = count % 3 == 0; // the weak-fundamental notes are the majority, as on the recording
            const double f0 = isStrong ? 77.8 : 41.2;
            const double* h = isStrong ? strong : weak;
            const double k = isStrong ? ks : kw;
            for (size_t i = 0; i < (size_t) (0.5 * kFs); ++i)
            {
                const double t = (double) i / kFs, env = std::min (1.0, t / 0.005) * std::exp (-t / 0.8);
                double s = 0;
                for (int j = 0; j < 4; ++j)
                    s += h[j] * std::sin (2 * kPi * (j + 1) * f0 * t + 0.4 * j);
                x[(size_t) (t0 * kFs) + i] += (float) (k * env * s);
            }
        }
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        Params p;
        p.amount = 1.0f;
        run (l, x, 512, p);
        float worst = 0.0f;
        for (const auto& n : l.notesLog)
            worst = std::max (worst, std::fabs (n.corrDb));
        CHECK (l.notesLog.size() > 15 && worst < 2.0f, "equally loud notes, some with weak fundamentals: largest correction %.1f dB over %zu notes", worst,
               l.notesLog.size());
    }

    std::printf ("Natural-sounding correction (no brick wall)\n");
    {
        // (1) the correction curve is smooth everywhere: no corner where it suddenly stops
        const float boost = 6.0f, cut = 9.0f, amount = 1.0f;
        float prev = Leveler::softCorrection (-30.0f, amount, boost, cut), prevSlope = 0.0f, maxSlope = 0.0f, maxSlopeJump = 0.0f;
        bool monotonic = true, belowCeiling = true;
        for (float d = -29.99f; d <= 30.0f; d += 0.01f)
        {
            const float c = Leveler::softCorrection (d, amount, boost, cut);
            const float slope = (c - prev) / 0.01f;
            monotonic = monotonic && slope >= -1e-4f;
            belowCeiling = belowCeiling && c < boost && c > -cut;
            maxSlope = std::max (maxSlope, slope);
            if (d > -29.9f)
                maxSlopeJump = std::max (maxSlopeJump, std::fabs (slope - prevSlope));
            prev = c;
            prevSlope = slope;
        }
        CHECK (monotonic && belowCeiling, "correction grows steadily with the deviation and stays under the ceilings");
        CHECK (maxSlope <= 1.001f, "never more than 1 dB of correction per dB of deviation (steepest %.2f)", maxSlope);
        // a hard clamp changes slope from 1 to 0 in one step (jump of 1.0 per 0.01 dB step = 100 dB/dB^2)
        CHECK (maxSlopeJump < 0.05f, "no corner anywhere in the curve (largest slope change per 0.01 dB step %.4f)", maxSlopeJump);
        CHECK (std::fabs (Leveler::softCorrection (0.4f, 1.0f, boost, cut)) < 0.15f, "tiny deviations are barely touched (0.4 dB off -> %.2f dB)",
               Leveler::softCorrection (0.4f, 1.0f, boost, cut));
        CHECK (Leveler::softCorrection (4.0f, 0.5f, boost, cut) < 2.0f && Leveler::softCorrection (4.0f, 0.5f, boost, cut) > 1.0f,
               "amount 0.5 corrects roughly half (4 dB off -> %.2f dB)", Leveler::softCorrection (4.0f, 0.5f, boost, cut));
    }
    {
        // (2) the bell's gain moves smoothly: no steps, never past the ceilings
        double total;
        const auto ev = makeLine (12, 3, 3.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (13, 7.0), 3);
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 1.0f;
        p.maxBoostDb = 6.0f;
        p.maxCutDb = 9.0f;
        l.setParams (p);
        std::vector<float> out = sig;
        std::vector<float> g0 (sig.size()), g1 (sig.size());
        for (size_t i = 0; i < sig.size(); ++i)
        {
            float* ch[1] = { out.data() + i };
            l.process (ch, 1, 1);
            g0[i] = l.voiceGainDb (0);
            g1[i] = l.voiceGainDb (1);
        }
        // two bell voices take turns, so check each voice's own gain trajectory
        double maxStep = 0, maxG = -1e9, minG = 1e9;
        for (const auto* g : { &g0, &g1 })
            for (size_t i = 48; i < g->size(); ++i)
            {
                maxStep = std::max (maxStep, (double) std::fabs ((*g)[i] - (*g)[i - 48])); // dB moved within 1 ms
                maxG = std::max (maxG, (double) (*g)[i]);
                minG = std::min (minG, (double) (*g)[i]);
            }
        CHECK (maxStep < 1.5, "gain never jumps: the most it moves in 1 ms is %.2f dB", maxStep);
        CHECK (maxG < 6.0 && minG > -9.0, "gain stays inside the soft ceilings (%.2f .. %.2f dB)", minG, maxG);

        // (3) the correction adds no clicks: the difference between output and (delayed) input has almost no HF content
        const int lat = l.latencySamples();
        double eAll = 0, eHf = 0, lp = 0, prevd = 0;
        for (size_t i = (size_t) lat + 1; i < sig.size(); ++i)
        {
            const double d = (double) out[i] - sig[i - (size_t) lat];
            lp += 0.08 * (d - lp); // ~600 Hz one-pole low-pass
            const double hf = d - lp;
            eAll += d * d;
            eHf += hf * hf;
            prevd = d;
        }
        (void) prevd;
        CHECK (eAll > 0 && eHf / eAll < 0.02, "the correction is smooth: only %.2f%% of what it adds lies above ~600 Hz", 100.0 * eHf / eAll);
    }
    {
        // (4) recent-note display data: deviation and correction have opposite signs
        double total;
        const auto ev = makeLine (14, 3, 0.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (15, 7.0));
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 1.0f;
        run (l, sig, 256, p);
        RecentNote r[32];
        const int n = l.copyRecent (r, 32);
        bool ok = n > 20;
        for (int i = 0; i < n; ++i)
            ok = ok && (std::fabs (r[i].deviationDb) < 1.0f || r[i].deviationDb * r[i].correctionDb <= 0.0f);
        CHECK (ok, "recent-note data: %d notes, corrections oppose the deviations", n);
    }

    std::printf ("Transparency and latency\n");
    {
        double total;
        const auto ev = makeLine (2, 2, 3.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (3, 6.0));
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 0.0f;
        const auto out = run (l, sig, 128, p);
        const int lat = l.latencySamples();
        double md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (out[i] - sig[i - (size_t) lat]));
        CHECK (md < 1e-7, "amount 0: output is the input delayed by exactly the reported latency (max diff %.1e)", md);
    }
    {
        double total;
        const auto ev = makeLine (6, 4, 0.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (1, 0.0), 1);
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 1.0f;
        const auto out = run (l, sig, 512, p);
        const auto a = perPitch (sig, 0, ev, 6), b = perPitch (out, l.latencySamples(), ev, 6);
        double worst = 0;
        for (size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, std::fabs (a[i] - b[i]));
        CHECK (worst < 1.0, "already-even bass: largest change %.2f dB (amount 1.0)", worst);
    }

    std::printf ("Block-size independence\n");
    {
        double total;
        const auto ev = makeLine (8, 2, 3.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (9, 6.0));
        auto go = [&] (int block) {
            Leveler l;
            l.prepare (kFs);
            Params p;
            p.amount = 1.0f;
            return run (l, sig, block, p);
        };
        const auto ref = go (512);
        for (int block : { 1, 7, 16, 100, 333, 4096 })
        {
            const auto o = go (block);
            double md = 0;
            for (size_t i = 0; i < o.size(); ++i)
                md = std::max (md, (double) std::fabs (o[i] - ref[i]));
            CHECK (md < 1e-6, "block %d: max difference %.1e", block, md);
        }
    }

    std::printf ("Robustness\n");
    {
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.amount = 1.0f;
        std::mt19937 g (3);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        std::vector<float> junk (48000 * 4);
        for (size_t i = 0; i < junk.size(); ++i)
            junk[i] = i < 48000 ? 0.0f : (i < 96000 ? 0.5f * u (g) : (i < 144000 ? std::sin (0.01f * (float) i * (float) i * 1e-3f) : 0.3f * u (g)));
        junk[60000] = std::numeric_limits<float>::quiet_NaN();
        junk[60001] = std::numeric_limits<float>::infinity();
        const auto o = run (l, junk, 333, p);
        bool finite = true;
        float peak = 0;
        for (float v : o)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::fabs (v));
        }
        CHECK (finite && peak < 4.0f, "silence, noise, chirp, NaN and inf in: output finite (peak %.2f)", peak);
        for (double sr : { 44100.0, 88200.0, 96000.0 })
        {
            double total;
            const auto ev = makeLine (1, 1, 0.0, total);
            Leveler lr;
            lr.prepare (sr);
            lr.logNotes = true;
            const auto sig = synth::render (sr, ev, total, makeResonances (1, 0.0));
            run (lr, sig, 256, Params {});
            int ok = 0;
            for (const auto& e : ev)
                for (const auto& n : lr.notesLog)
                    if (std::fabs (n.startSec - e.t) < 0.06 && std::fabs (n.midi - e.midi) < 0.3f)
                    {
                        ++ok;
                        break;
                    }
            CHECK (ok >= (int) ev.size() * 9 / 10, "sample rate %.0f: %d of %zu notes tracked correctly", sr, ok, ev.size());
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
