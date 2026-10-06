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

// a bass line over 16 pitches (E1..G2), several repeats, random velocities
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
    std::vector<float> scratch ((size_t) block);
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

// per-pitch mean of a metric over the notes of a line
static std::vector<double> perPitch (const std::vector<float>& sig, int shiftSamples, const std::vector<synth::Ev>& ev, bool balance)
{
    std::map<int, std::vector<double>> acc;
    for (const auto& e : ev)
    {
        // measurement window starts 40 ms after the onset, in the (delayed) signal
        auto m = synth::measure (sig, kFs, e.midi, e.t + (double) shiftSamples / kFs);
        acc[e.midi].push_back (balance ? m.balDb : m.lvlDb);
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

int main()
{
    std::printf ("Pitch and note tracking\n");
    {
        double total;
        const auto ev = makeLine (1, 3, 3.0, total);
        const auto res = makeResonances (7, 0.0);
        const auto sig = synth::render (kFs, ev, total, res);
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        Params p;
        p.learn = true;
        run (l, sig, 256, p);
        int matched = 0, right = 0;
        for (const auto& e : ev)
            for (const auto& n : l.notesLog)
                if (std::fabs (n.startSec - e.t) < 0.06)
                {
                    ++matched;
                    right += std::fabs (n.midi - e.midi) < 0.3f;
                    break;
                }
        CHECK (matched >= (int) ev.size() * 95 / 100, "notes found: %d of %zu", matched, ev.size());
        CHECK (right >= matched * 95 / 100, "pitch correct: %d of %d", right, matched);
        CHECK (l.notesLog.size() <= ev.size() + 2, "no phantom notes (%zu logged for %zu played)", l.notesLog.size(), ev.size());
        std::printf ("  latency %d samples = %.1f ms\n", l.latencySamples(), 1000.0 * l.latencySamples() / kFs);
    }

    std::printf ("Harder playing\n");
    {
        // (a) fast repeated notes on one pitch and alternating pitches (8th notes at ~143 bpm)
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
        Params p;
        p.learn = true;
        run (l, sig, 256, p);
        int ok = 0;
        for (const auto& e : ev)
            for (const auto& n : l.notesLog)
                if (std::fabs (n.startSec - e.t) < 0.06 && std::fabs (n.midi - e.midi) < 0.3f)
                {
                    ++ok;
                    break;
                }
        CHECK (ok >= (int) ev.size() * 9 / 10, "fast 8ths: %d of %zu notes tracked (%zu logged)", ok, ev.size(), l.notesLog.size());
    }
    {
        // (b) hammer-ons: the second note of each pair has no pick and no new energy onset
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
        Params p;
        p.learn = true;
        run (l, sig, 256, p);
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
        // (c) a slide: stays one note, bell follows the pitch (output stays finite and sane)
        std::vector<synth::Ev> ev;
        synth::Ev s1 { 0.3, 1.2, 31, 0.0 };
        s1.glideTo = 36.0;
        ev.push_back (s1);
        const auto sig = synth::render (kFs, ev, 2.2, makeResonances (1, 0.0));
        Leveler l;
        l.prepare (kFs);
        l.logNotes = true;
        Params p;
        p.learn = true;
        run (l, sig, 256, p);
        CHECK (l.notesLog.size() == 1, "a 5-semitone slide stays one note (%zu logged)", l.notesLog.size());
    }
    {
        // (d) Rider: per-note variation inside one pitch (the table cannot fix this, Rider can)
        std::mt19937 g (21);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        auto line = [&] (double& total) {
            std::vector<synth::Ev> ev;
            double t = 0.3;
            for (int i = 0; i < 40; ++i)
            {
                ev.push_back ({ t, 0.42, 30 + (i % 4), u (g) * 4.0 });
                t += 0.55;
            }
            total = t + 0.5;
            return ev;
        };
        double tA, tB;
        const auto evA = line (tA), evB = line (tB);
        const auto flat = makeResonances (1, 0.0);
        const auto sigA = synth::render (kFs, evA, tA, flat, 1), sigB = synth::render (kFs, evB, tB, flat, 2);
        auto noteStd = [&] (const std::vector<float>& sig, int shift, const std::vector<synth::Ev>& ev, size_t skip) {
            std::vector<double> v;
            for (size_t i = skip; i < ev.size(); ++i)
                v.push_back (synth::measure (sig, kFs, ev[i].midi, ev[i].t + (double) shift / kFs).lvlDb);
            return stddev (v);
        };
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.mode = 1;
        p.learn = true;
        run (l, sigA, 512, p);
        p.learn = false;
        p.strength = 1.0f;
        p.rider = 1.0f;
        l.reset();
        const auto out = run (l, sigB, 512, p);
        const double before = noteStd (sigB, 0, evB, 8);
        const double after = noteStd (out, l.latencySamples(), evB, 8);
        CHECK (after < before * 0.75, "Rider: note-to-note spread %.2f dB -> %.2f dB", before, after);
    }

    std::printf ("Transparency and latency\n");
    {
        double total;
        const auto ev = makeLine (2, 2, 3.0, total);
        const auto sig = synth::render (kFs, ev, total, makeResonances (3, 6.0));
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.strength = 1.0f;
        const auto out = run (l, sig, 128, p); // empty table: nothing to correct
        const int lat = l.latencySamples();
        double md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (out[i] - sig[i - (size_t) lat]));
        CHECK (md < 1e-7, "empty table: output is the input delayed by exactly the reported latency (max diff %.1e)", md);

        Leveler l2;
        l2.prepare (kFs);
        Params pl;
        pl.learn = true;
        const auto out2 = run (l2, sig, 128, pl);
        md = 0;
        for (size_t i = (size_t) lat; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (out2[i] - sig[i - (size_t) lat]));
        CHECK (md < 1e-7, "learn mode passes the audio through unchanged (max diff %.1e)", md);
    }

    std::printf ("Evening out resonances\n");
    for (int mode = 0; mode < 2; ++mode)
    {
        double total, total2;
        const auto resonances = makeResonances (11, 7.0);
        const auto evA = makeLine (4, 3, 3.0, total);
        const auto evB = makeLine (5, 3, 3.0, total2);
        const auto sigA = synth::render (kFs, evA, total, resonances, 1);
        const auto sigB = synth::render (kFs, evB, total2, resonances, 2);
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.mode = mode;
        p.learn = true;
        run (l, sigA, 512, p);
        p.learn = false;
        p.strength = 1.0f;
        p.maxBoostDb = 12.0f;
        p.maxCutDb = 18.0f;
        l.reset(); // keep the learned table; clear audio state
        const auto outB = run (l, sigB, 512, p);
        const bool bal = mode == 0;
        const double before = stddev (perPitch (sigB, 0, evB, bal));
        const double after = stddev (perPitch (outB, l.latencySamples(), evB, bal));
        CHECK (after < before * (mode == 0 ? 0.40 : 0.60), "%s mode: spread between pitches %.2f dB -> %.2f dB (-%.0f%%)",
               bal ? "Balance" : "Level", before, after, 100.0 * (1.0 - after / before));
    }

    std::printf ("Already-even bass is left alone\n");
    {
        double total, total2;
        const auto flat = makeResonances (1, 0.0);
        const auto evA = makeLine (6, 3, 0.0, total);
        const auto evB = makeLine (7, 2, 0.0, total2);
        const auto sigA = synth::render (kFs, evA, total, flat, 1);
        const auto sigB = synth::render (kFs, evB, total2, flat, 2);
        Leveler l;
        l.prepare (kFs);
        Params p;
        p.learn = true;
        run (l, sigA, 512, p);
        p.learn = false;
        p.strength = 1.0f;
        l.reset();
        const auto outB = run (l, sigB, 512, p);
        const auto a = perPitch (sigB, 0, evB, true), b = perPitch (outB, l.latencySamples(), evB, true);
        double worst = 0;
        for (size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, std::fabs (a[i] - b[i]));
        CHECK (worst < 1.0, "largest change on even bass: %.2f dB", worst);
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
            p.learn = true;
            run (l, sig, block, p);
            p.learn = false;
            p.strength = 1.0f;
            p.rider = 0.5f;
            p.focus2 = true;
            l.reset();
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
        p.strength = 1.0f;
        p.rider = 1.0f;
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
            Params pl;
            pl.learn = true;
            const auto sig = synth::render (sr, ev, total, makeResonances (1, 0.0));
            lr.logNotes = true;
            run (lr, sig, 256, pl);
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

    std::printf ("Table persistence\n");
    {
        PitchTable t;
        for (int p = 30; p < 40; ++p)
            for (int i = 0; i < 5; ++i)
                t.add (p, (float) p * 0.1f + i, (float) p * 0.2f - i);
        t.rebuild();
        std::vector<uint8_t> blob;
        t.serialise (blob);
        PitchTable u;
        const bool ok = u.deserialise (blob.data(), blob.size());
        bool same = ok;
        for (int p = 28; p < 42; ++p)
            for (int m = 0; m < 2; ++m)
                same = same && std::fabs (t.correction (m, (float) p + 0.3f) - u.correction (m, (float) p + 0.3f)) < 1e-6f;
        CHECK (same, "table survives a save/restore round trip (%zu bytes)", blob.size());
        PitchTable w;
        CHECK (! w.deserialise (blob.data(), blob.size() / 2), "truncated data is rejected");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
