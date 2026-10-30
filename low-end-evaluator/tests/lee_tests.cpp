// Tests for the Low-End Evaluator core (no JUCE): DSP conformance, then synthetic mixes with one known defect each,
// judged against a profile learned from clean variants. Each defect must trigger its metric and its fix, the simulated
// fix must help, and clean mixes must not trigger anything.
#include "../core/Recommend.h"
#include "mixsynth.h"

#include <cstdio>

using namespace lee;

static int failures = 0;
#define CHECK(cond, ...)                                        \
    do {                                                        \
        const bool ok_ = (cond);                                \
        std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL");         \
        std::printf (__VA_ARGS__);                              \
        std::printf ("\n");                                     \
        if (! ok_) ++failures;                                  \
    } while (0)

static const Rec* findRec (const Recommendations& r, const std::string& prefix)
{
    for (auto& x : r.recs)
        if (x.id.rfind (prefix, 0) == 0)
            return &x;
    return nullptr;
}

static void dspConformance()
{
    std::printf ("DSP conformance\n");
    {
        // Welch PSD: a 100 Hz sine of amplitude 0.5 has power 0.125
        const double fs = 6000;
        std::vector<double> x (60000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5 * std::sin (2 * kPi * 100 * (double) i / fs);
        const Spectra s = welch (x, x, fs, 16384);
        const double p = s.midPower (90, 110);
        CHECK (std::fabs (10 * std::log10 (p / 0.125)) < 0.1, "Welch band power of a sine: %.4f (expect 0.125)", p);
    }
    {
        // 44.1 -> 48 kHz keeps a 1 kHz sine's level and frequency
        std::vector<float> x (44100);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (0.5 * std::sin (2 * kPi * 1000 * (double) i / 44100));
        const auto y = resample (x, 44100, 48000);
        double re = 0, im = 0, e = 0;
        for (size_t i = 4800; i < 43200; ++i)
        {
            re += y[i] * std::cos (2 * kPi * 1000 * (double) i / 48000);
            im += y[i] * std::sin (2 * kPi * 1000 * (double) i / 48000);
            e += (double) y[i] * y[i];
        }
        const double amp = 2 * std::sqrt (re * re + im * im) / 38400, rms = std::sqrt (e / 38400);
        CHECK (y.size() == 48000 && std::fabs (20 * std::log10 (amp / 0.5)) < 0.05 && std::fabs (rms - 0.5 / std::sqrt (2.0)) < 0.002,
               "resample 44.1 -> 48 kHz: %zu samples, 1 kHz amplitude %.4f (0.5), rms %.4f", y.size(), amp, rms);
    }
    {
        // true peak of a full-scale sine at fs/4 sampled 45 degrees off its peaks: samples at -3 dB, true peak 0 dBTP
        std::vector<float> x (4800);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) std::sin (kPi / 2 * (double) i + kPi / 4);
        int overs = 0;
        const double tp = 20 * std::log10 (detail::truePeak (x, overs));
        CHECK (std::fabs (tp) < 0.3, "true peak of an fs/4 sine sampled at -3 dB: %.2f dBTP (0)", tp);
    }
    {
        // ISO 226: 1 kHz is its own reference; 40 phon at 100 Hz needs ~62 dB SPL (weight ~ -22 dB)
        const double w1k = equalLoudnessWeightDb (1000, 40), w100 = equalLoudnessWeightDb (100, 40);
        CHECK (std::fabs (w1k) < 0.05 && w100 < -19 && w100 > -25, "equal loudness: 1 kHz %.2f dB, 100 Hz at 40 phon %.1f dB", w1k, w100);
    }
    {
        // WAV round trip
        Stereo s;
        s.L = { 0.0f, 0.5f, -0.25f, 1.0f };
        s.R = { 0.1f, -0.5f, 0.25f, -1.0f };
        const char* path = "lee_test_roundtrip.wav";
        Stereo r;
        std::string err;
        const bool ok = writeWav (path, s) && readWav (path, r, err);
        std::remove (path);
        CHECK (ok && r.L == s.L && r.R == s.R && r.fs == 48000, "WAV write/read round trip");
    }
}

int main()
{
    dspConformance();

    std::printf ("\nProfile from 10 clean reference mixes\n");
    std::vector<Analysis> refs;
    for (unsigned seed = 1; seed <= 10; ++seed)
    {
        mix::Params p;
        p.seed = seed;
        p.bassDb = 1.0 * ((int) (seed % 3) - 1); // a little natural variation between references
        refs.push_back (analyze (mix::render (p), "ref" + std::to_string (seed)));
    }
    const Analysis& r0 = refs[0];
    CHECK (r0.notes.size() >= 40, "notes tracked in a 16 s reference: %zu", r0.notes.size());
    CHECK (r0.onsets >= 26 && r0.onsets <= 48, "low-band onsets in 16 s with a kick every 0.5 s: %d (32 kicks)", r0.onsets);
    CHECK (r0.get ("S2").v > 0.95, "clean mix low-band correlation p5: %.3f", r0.get ("S2").v);
    CHECK (r0.get ("T2").v == 0, "clean mix has no clipping events");
    std::vector<std::string> warn;
    const Profile prof = buildProfile ("clean", refs, &warn);
    CHECK (prof.targets.size() >= 30, "profile has %zu metric targets", prof.targets.size());
    for (auto& w : warn)
        std::printf ("    note: %s\n", w.c_str());

    {
        Json j = toJson (prof);
        std::string err;
        Json back;
        Profile p2;
        const bool ok = parse (dump (j), back, err) && fromJson (back, p2, err);
        bool same = ok && p2.targets.size() == prof.targets.size();
        for (auto& [id, t] : prof.targets)
            same = same && p2.targets.count (id) && std::fabs (p2.targets.at (id).center - t.center) < 1e-6 * (1 + std::fabs (t.center));
        CHECK (same, "profile JSON round trip%s%s", err.empty() ? "" : ": ", err.c_str());
    }

    auto judge = [&] (const mix::Params& p, const char* what, Recommendations& rs, Analysis& a) {
        const Stereo s = mix::render (p);
        a = analyze (s, what);
        const Score sc = score (a, prof, Stage::Mix);
        rs = recommend (s, a, prof, sc);
        std::printf ("  %s: composite %.1f %s (clarity %.1f, dynamics %.1f, translation %.1f)%s\n", what, sc.composite, grade (sc.composite), sc.cat[0],
                     sc.cat[1], sc.cat[2], sc.gates.empty() ? "" : "  GATED");
        for (auto& r : rs.recs)
            std::printf ("      rec %-14s %+5.1f %s\n", r.id.c_str(), r.delta, r.title.c_str());
        return sc;
    };

    std::printf ("\nClean held-out mix\n");
    {
        mix::Params p;
        p.seed = 77;
        Recommendations rs;
        Analysis a;
        const Score sc = judge (p, "clean", rs, a);
        CHECK (sc.composite >= 85 && sc.gates.empty(), "clean mix scores %.1f (>= 85) with no gate", sc.composite);
        double worst = 0;
        for (auto& m : sc.ms)
            if (m.used)
                worst = std::max (worst, m.d);
        CHECK (worst < 1.0, "no metric of a clean mix is a sigma outside its band (worst %.2f)", worst);
        CHECK (rs.recs.empty(), "clean mix gets no recommendations (%zu)", rs.recs.size());
    }

    std::printf ("\nDefect: +6 dB bell at 220 Hz\n");
    {
        mix::Params p;
        p.seed = 78;
        p.mudDb = 6;
        Recommendations rs;
        Analysis a;
        const Score sc = judge (p, "mud", rs, a);
        CHECK (sc.find ("B4a")->d >= 1, "mud vs bass flagged: %.2f dB, %.1f sigma", a.get ("B4a").v, sc.find ("B4a")->d);
        const Rec* r = findRec (rs, "eq@");
        CHECK (r && r->fix.gainDb < 0 && r->fix.hz > 150 && r->fix.hz < 330, "EQ cut suggested in the mud range: %s",
               r ? describe (r->fix).c_str() : "none");
        CHECK (r && r->simulated && r->delta > 3, "simulated cut raises the composite by %.1f (> 3)", r ? r->delta : 0.0);
    }

    std::printf ("\nDefect: anti-phase low end\n");
    {
        mix::Params p;
        p.seed = 79;
        p.bassDb = -3;
        p.kickDb = -6;
        p.sideLowDb = 6; // bass L = 2.1x, R = -0.7x: anti-phase between kicks
        Recommendations rs;
        Analysis a;
        const Score sc = judge (p, "antiphase", rs, a);
        CHECK (a.get ("S2").v < -0.5 && a.get ("T2").v == 0 && sc.composite <= 64, "correlation %.2f trips the gate (composite %.1f, uncapped %.1f)",
               a.get ("S2").v, sc.composite, sc.uncapped);
        const Rec* r = findRec (rs, "mono");
        CHECK (r && r->fix.kind == Fix::MonoBelow, "mono-below suggested: %s", r ? describe (r->fix).c_str() : "none");
        CHECK (r && r->after > 64 && r->delta > 15, "simulated mono fix lifts the gate: composite -> %.1f (delta %.1f)", r ? r->after : 0.0,
               r ? r->delta : 0.0);
        CHECK (! rs.recs.empty() && rs.recs[0].id == "mono", "the phase fix ranks first");
    }

    std::printf ("\nDefect: 12 Hz rumble\n");
    {
        mix::Params p;
        p.seed = 80;
        p.rumbleDb = -20;
        Recommendations rs;
        Analysis a;
        const Score sc = judge (p, "rumble", rs, a);
        CHECK (sc.find ("T4")->d >= 1, "infrasonic energy flagged: %.1f dB, %.1f sigma", a.get ("T4").v, sc.find ("T4")->d);
        const Rec* r = findRec (rs, "infrasonic");
        CHECK (r && r->fix.kind == Fix::HighPass && r->delta > 1, "high-pass suggested and helps (%+.1f)", r ? r->delta : 0.0);
    }

    std::printf ("\nDefect: clipped mix\n");
    {
        mix::Params p;
        p.seed = 81;
        p.driveDb = 15;
        Analysis a;
        Recommendations rs;
        const Score sc = judge (p, "clipped", rs, a);
        CHECK (a.get ("T2").v > 0 && sc.composite <= 59, "%.0f clipping events cap the composite at %.1f", a.get ("T2").v, sc.composite);
        CHECK (! rs.recs.empty() && rs.recs[0].id == "clipping", "headroom fix ranks first");
    }

    std::printf ("\nDefect: uneven note levels (+-6 dB)\n");
    {
        mix::Params p;
        p.seed = 82;
        p.velSpreadDb = 6;
        Analysis a;
        Recommendations rs;
        const Score sc = judge (p, "uneven", rs, a);
        CHECK (sc.find ("D3")->d >= 1, "note consistency flagged: %.2f dB vs band [%.2f, %.2f]", a.get ("D3").v, prof.targets.at ("D3").lo,
               prof.targets.at ("D3").hi);
        const Rec* r = findRec (rs, "notes");
        CHECK (r && ! r->simulated, "compression advice given (estimated)");
    }

    std::printf ("\nDefect: bass 6 dB too loud\n");
    {
        mix::Params p;
        p.seed = 83;
        p.bassDb = 6;
        Analysis a;
        Recommendations rs;
        const Score sc = judge (p, "loud bass", rs, a);
        const Rec* r = findRec (rs, "eq@");
        CHECK (r && r->fix.gainDb < -2 && r->fix.hz < 300 && (r->fix.kind == Fix::LowShelf || r->fix.q < 1.5) && r->delta > 2, "a broad low cut is suggested and helps: %s (%+.1f)", r ? describe (r->fix).c_str() : "none",
               r ? r->delta : 0.0);
        (void) sc;
    }

    std::printf ("\nDeterminism and mix stage\n");
    {
        mix::Params p;
        p.seed = 5;
        const Stereo s = mix::render (p);
        const Analysis a1 = analyze (s), a2 = analyze (s);
        bool same = true;
        for (auto& [id, m] : a1.m)
            same = same && (m.v == a2.m.at (id).v || (std::isnan (m.v) && std::isnan (a2.m.at (id).v)));
        CHECK (same, "same input, same metrics (bit-exact)");
        // an unmastered mix that is more dynamic than the references is not penalised in mix stage
        auto m2 = a1.m;
        m2["D1"].v = prof.targets.at ("D1").hi + 5 * prof.targets.at ("D1").sigma;
        const double mixS = scoreMetrics (m2, prof, Stage::Mix).cat[Dynamics], masterS = scoreMetrics (m2, prof, Stage::Master).cat[Dynamics];
        CHECK (mixS > masterS + 5, "high crest factor: dynamics %.1f in mix stage vs %.1f in master stage", mixS, masterS);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
