// Tests for the Steelwave string model (no JUCE needed).
#include "../core/SteelString.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <random>

using namespace sw;

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

static double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// Hann-windowed DFT magnitude at frequency f over [t0, t0+len)
static double dftMag (const std::vector<float>& x, double f, double t0, double len, double fs = kFs)
{
    const size_t i0 = (size_t) (t0 * fs), L = (size_t) (len * fs);
    double re = 0, im = 0;
    for (size_t j = 0; j < L && i0 + j < x.size(); ++j)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * kPi * (j + 0.5) / L);
        const double ph = 2 * kPi * f * (double) j / fs;
        re += w * x[i0 + j] * std::cos (ph);
        im -= w * x[i0 + j] * std::sin (ph);
    }
    return std::sqrt (re * re + im * im) * 4.0 / (double) L;
}

// frequency of the strongest peak within +-range cents of `guess` (0.05 cent resolution)
static double peakHz (const std::vector<float>& x, double guess, double t0, double len, double range = 60, double fs = kFs)
{
    double best = guess, bestM = -1;
    for (double c = -range; c <= range; c += 1.0)
    {
        const double f = guess * std::exp2 (c / 1200.0);
        const double m = dftMag (x, f, t0, len, fs);
        if (m > bestM) { bestM = m; best = f; }
    }
    // golden-section refine within +-1 cent
    double a = best * std::exp2 (-1.0 / 1200), b = best * std::exp2 (1.0 / 1200);
    for (int it = 0; it < 30; ++it)
    {
        const double c1 = b - (b - a) * 0.618, c2 = a + (b - a) * 0.618;
        if (dftMag (x, c1, t0, len, fs) > dftMag (x, c2, t0, len, fs)) b = c2; else a = c1;
    }
    return 0.5 * (a + b);
}

static double rms (const std::vector<float>& x, double t0, double t1, double fs = kFs)
{
    double s = 0;
    size_t n = 0;
    for (size_t i = (size_t) (t0 * fs); i < (size_t) (t1 * fs) && i < x.size(); ++i, ++n)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (n, 1));
}
static double db (double v) { return 20.0 * std::log10 (v + 1e-15); }

// Render one string; `ctl` is called every sample with the time in seconds (for scripted moves).
static std::vector<float> render (int stringIdx, double seconds, double vel, StringParams prm,
                                  const std::function<void (SteelString&, double)>& ctl = nullptr, double fs = kFs)
{
    SteelString s;
    s.prepare (fs, e9Strings()[(size_t) stringIdx]);
    s.setParams (prm);
    std::vector<float> out ((size_t) (seconds * fs));
    if (! ctl)
        s.pluck (vel);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (ctl)
            ctl (s, (double) i / fs);
        out[i] = s.process();
    }
    return out;
}

static bool finiteBounded (const std::vector<float>& x, float lim)
{
    for (float v : x)
        if (! std::isfinite (v) || std::fabs (v) > lim)
            return false;
    return true;
}

int main()
{
    const auto strings = e9Strings();
    StringParams quiet;
    quiet.barNoise = 0;

    std::printf ("Tuning: every E9 string, bar at 0/5/12/19/24\n");
    {
        double worst = 0;
        for (int si = 0; si < 10; ++si)
            for (int bar : { 0, 5, 12, 19, 24 })
            {
                auto x = render (si, 1.0, 0.8, quiet, [&] (SteelString& s, double t) {
                    if (t == 0) { s.setBar (bar, 0); s.pluck (0.8); }
                });
                const double target = midiHz (strings[(size_t) si].openMidi + bar);
                const double f = peakHz (x, target, 0.3, 0.6);
                worst = std::max (worst, std::fabs (cents (f, target)));
            }
        CHECK (worst < 1.0, "worst fundamental error %.3f cents (< 1)", worst);
    }

    std::printf ("Tuning at other sample rates\n");
    for (double fs : { 44100.0, 96000.0 })
    {
        double worst = 0;
        for (int si : { 0, 4, 9 })
            for (int bar : { 0, 12, 24 })
            {
                auto x = render (si, 1.0, 0.8, quiet, [&] (SteelString& s, double t) {
                    if (t == 0) { s.setBar (bar, 0); s.pluck (0.8); }
                }, fs);
                const double target = midiHz (strings[(size_t) si].openMidi + bar);
                worst = std::max (worst, std::fabs (cents (peakHz (x, target, 0.3, 0.6, 60, fs), target)));
            }
        CHECK (worst < 1.0, "%.1f kHz: worst error %.3f cents", fs / 1000, worst);
    }

    std::printf ("Glides land in tune\n");
    {
        auto x = render (3, 1.4, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.8);
            if (std::fabs (t - 0.3) < 0.5 / kFs) s.setBar (2, 0.2);
        });
        const double e = cents (peakHz (x, midiHz (66), 0.8, 0.5), midiHz (66));
        CHECK (std::fabs (e) < 1.0, "bar slide E4 -> F#4 lands within %.3f cents", e);
        auto y = render (5, 1.4, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.8);
            if (std::fabs (t - 0.3) < 0.5 / kFs) s.setPedal (1, 0.15);
        });
        const double e2 = cents (peakHz (y, midiHz (57), 0.8, 0.5), midiHz (57));
        CHECK (std::fabs (e2) < 1.0, "pedal bend G#3 -> A3 (B pedal) lands within %.3f cents", e2);
    }

    std::printf ("Glides are click-free\n");
    {
        // high-frequency energy (2nd difference) during a slow slide vs. the same note held
        auto hf = [] (const std::vector<float>& x, double t0, double t1) {
            double s = 0, n = 0;
            for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs); ++i, ++n)
            {
                const double d2 = x[i] - 2.0 * x[i - 1] + x[i - 2];
                s += d2 * d2;
            }
            return std::sqrt (s / n);
        };
        auto held = render (4, 2.0, 0.8, quiet);
        auto slid = render (4, 2.0, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.8);
            if (std::fabs (t - 0.5) < 0.5 / kFs) s.setBar (-0.0, 0), s.setBar (7, 0.9);
        });
        // normalise by overall level in the same window
        const double rHeld = hf (held, 0.6, 1.3) / rms (held, 0.6, 1.3);
        const double rSlid = hf (slid, 0.6, 1.3) / rms (slid, 0.6, 1.3);
        // a higher note is naturally a bit brighter; anything click-like would add far more
        CHECK (db (rSlid / rHeld) < 6.0, "HF/level during slide vs held: %+.2f dB (< +6)", db (rSlid / rHeld));
        double maxJump = 0;
        for (size_t i = (size_t) (0.6 * kFs); i < (size_t) (1.3 * kFs); ++i)
            maxJump = std::max (maxJump, (double) std::fabs (slid[i] - slid[i - 1]));
        double maxJumpHeld = 0;
        for (size_t i = (size_t) (0.6 * kFs); i < (size_t) (1.3 * kFs); ++i)
            maxJumpHeld = std::max (maxJumpHeld, (double) std::fabs (held[i] - held[i - 1]));
        CHECK (maxJump / rms (slid, 0.6, 1.3) < 3.0 * maxJumpHeld / rms (held, 0.6, 1.3),
               "largest sample step during slide within 3x of a held note's");
        // fast wheel-style bend with MIDI-rate steps (one step per 128 samples, 5 ms de-zipper)
        auto wheel = render (4, 2.0, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.8);
            const int blk = (int) (t * kFs) / 128;
            if ((int) (t * kFs) % 128 == 0 && t > 0.5 && t < 1.0)
                s.setPedal (std::floor (2.0 * (t - 0.5) / 0.5 * 127.0) / 127.0, 0.005), (void) blk;
        });
        const double rWheel = hf (wheel, 0.55, 1.0) / rms (wheel, 0.55, 1.0);
        CHECK (db (rWheel / rHeld) < 6.0, "stepped wheel bend: HF/level %+.2f dB vs held (< +6)", db (rWheel / rHeld));
    }

    std::printf ("Decay\n");
    {
        StringParams p = quiet;
        auto x = render (3, 4.0, 0.8, p); // E4, t60Low ~ 7.6 s
        const double f = midiHz (64);
        const double a1 = dftMag (x, f, 0.5, 0.5), a2 = dftMag (x, f, 3.0, 0.5);
        const double t60 = 60.0 / ((db (a1) - db (a2)) / 2.5);
        const double spec = strings[3].t60Low;
        CHECK (t60 > 0.6 * spec && t60 < 1.4 * spec, "fundamental T60 %.2f s (spec %.2f)", t60, spec);
        const double h1 = dftMag (x, 10 * f * 1.002, 0.1, 0.3), h2 = dftMag (x, 10 * f * 1.002, 1.0, 0.3);
        CHECK (db (h1) - db (h2) > db (a1) - db (a2), "upper partials die faster than the fundamental");
        p.sustain = 0.5;
        auto y = render (3, 4.0, 0.8, p);
        const double t60b = 60.0 / ((db (dftMag (y, f, 0.5, 0.5)) - db (dftMag (y, f, 3.0, 0.5))) / 2.5);
        CHECK (t60b < 0.7 * t60, "Sustain 0.5 shortens decay (%.2f s)", t60b);
    }

    std::printf ("Damping\n");
    {
        auto x = render (9, 1.5, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.8);
            if (std::fabs (t - 0.5) < 0.5 / kFs) s.setDamp (1);
        });
        const double drop = db (rms (x, 0.4, 0.5)) - db (rms (x, 0.85, 0.95));
        CHECK (drop > 40.0, "hand block: %.1f dB down 0.4 s later", drop);
        double maxStep = 0;
        for (size_t i = (size_t) (0.49 * kFs); i < (size_t) (0.6 * kFs); ++i)
            maxStep = std::max (maxStep, (double) std::fabs (x[i] - x[i - 1]));
        double maxStepBefore = 0;
        for (size_t i = (size_t) (0.38 * kFs); i < (size_t) (0.49 * kFs); ++i)
            maxStepBefore = std::max (maxStepBefore, (double) std::fabs (x[i] - x[i - 1]));
        CHECK (maxStep <= 1.05 * maxStepBefore, "no click when the block starts");
    }

    std::printf ("Picking\n");
    {
        auto soft = render (3, 1.0, 0.25, quiet), hard = render (3, 1.0, 1.0, quiet);
        auto centroid = [] (const std::vector<float>& x) {
            double num = 0, den = 0;
            for (int k = 1; k <= 30; ++k)
            {
                const double m = dftMag (x, k * midiHz (64), 0.02, 0.15);
                num += k * m; den += m;
            }
            return num / den;
        };
        CHECK (rms (hard, 0, 0.5) > 2.5 * rms (soft, 0, 0.5), "harder = louder (%.1f dB)", db (rms (hard, 0, 0.5) / rms (soft, 0, 0.5)));
        CHECK (centroid (hard) > centroid (soft) + 0.5, "harder = brighter (centroid %.2f vs %.2f harmonics)", centroid (hard), centroid (soft));
        // re-picking a ringing string doesn't pile up energy
        auto rep = render (3, 3.0, 1.0, quiet, [&] (SteelString& s, double t) {
            if ((int) (t * kFs) % (int) (0.1 * kFs) == 0) s.pluck (1.0);
        });
        double pk1 = 0, pkN = 0;
        for (size_t i = 0; i < (size_t) (0.1 * kFs); ++i) pk1 = std::max (pk1, (double) std::fabs (hard[i]));
        for (size_t i = (size_t) (2.0 * kFs); i < rep.size(); ++i) pkN = std::max (pkN, (double) std::fabs (rep[i]));
        CHECK (pkN < 3.0 * pk1, "30 re-picks at 10/s: peak %.2fx a single pick (< 3x)", pkN / pk1);
    }

    std::printf ("Stiffness\n");
    {
        auto x = render (3, 1.0, 0.9, quiet);
        const double f = midiHz (64);
        const double f10 = peakHz (x, 10 * f, 0.1, 0.4, 80);
        const double c10 = cents (f10, 10 * f);
        CHECK (std::fabs (c10 - strings[3].inharmCents10) < 1.0, "10th partial of open E4 %.2f cents sharp (spec %.2f)", c10, strings[3].inharmCents10);
        auto y = render (3, 1.0, 0.9, quiet, [&] (SteelString& s, double t) { if (t == 0) { s.setBar (12, 0); s.pluck (0.9); } });
        const double c5 = cents (peakHz (y, 5 * 2 * f, 0.1, 0.4, 80), 5 * 2 * f);
        CHECK (c5 > 0.2, "an octave up the bar the string is stiffer (5th partial %.2f cents sharp)", c5);
    }

    std::printf ("Bar noise\n");
    {
        StringParams p;
        p.barNoise = 1.0;
        auto slide = [&] (double noise, int si) {
            StringParams q = p;
            q.barNoise = noise;
            return render (si, 1.2, 0.0, q, [&] (SteelString& s, double t) {
                if (t == 0) s.setDamp (1); // muted, so we hear the contact noise and not the string ringing on
                if (std::fabs (t - 0.2) < 0.5 / kFs) s.setBar (5, 0.6);
            });
        };
        auto withN = slide (1.0, 7), without = slide (0.0, 7);
        CHECK (rms (withN, 0.3, 0.7) > 1e-4 && rms (without, 0.3, 0.7) < 1e-9,
               "wound string slide with an unpicked string: %.1f dB noise vs silence", db (rms (withN, 0.3, 0.7)));
        CHECK (rms (withN, 1.1, 1.2) < 0.2 * rms (withN, 0.3, 0.7), "noise stops when the bar stops");
        auto plain = slide (1.0, 2);
        CHECK (rms (plain, 0.3, 0.7) < rms (withN, 0.3, 0.7), "plain string squeaks less than wound");
    }

    std::printf ("Stability\n");
    {
        std::mt19937 g (7);
        std::uniform_real_distribution<double> u (0, 1);
        bool ok = true;
        for (int si = 0; si < 10 && ok; ++si)
        {
            StringParams p;
            p.hardness = u (g); p.brightness = u (g); p.sustain = 0.25 + 1.75 * u (g);
            auto x = render (si, 20.0, 0, p, [&] (SteelString& s, double t) {
                const int i = (int) (t * kFs);
                if (i % 2400 == 0)
                {
                    const double r = u (g);
                    if (r < 0.3) s.pluck (u (g));
                    else if (r < 0.5) s.setBar (u (g) * 24, u (g) * 0.5);
                    else if (r < 0.7) s.setPedal (-2 + 4 * u (g), u (g) * 0.3);
                    else if (r < 0.8) s.setDamp (u (g) < 0.5 ? 1 : 0);
                    else s.setVibrato ((u (g) - 0.5) * 0.4);
                }
            });
            ok = finiteBounded (x, 20.0f);
        }
        CHECK (ok, "20 s of random picks, slides, bends, blocks on all strings: finite and bounded");
        SteelString s;
        s.prepare (kFs, strings[0]);
        s.pluck (1.0);
        for (int i = 0; i < 1000; ++i) s.process();
        s.reset();
        double mx = 0;
        for (int i = 0; i < 1000; ++i) mx = std::max (mx, (double) std::fabs (s.process()));
        CHECK (mx == 0.0, "reset() silences");
    }

    std::printf ("Performance\n");
    {
        std::array<SteelString, 10> ss;
        for (int i = 0; i < 10; ++i) { ss[(size_t) i].prepare (kFs, strings[(size_t) i]); ss[(size_t) i].pluck (0.8); }
        const int N = (int) (10 * kFs);
        double acc = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int n = 0; n < N; ++n)
        {
            if (n == N / 2) for (auto& s : ss) s.setBar (5, 0.3);
            for (auto& s : ss) acc += s.process();
        }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (std::isfinite (acc) && sec < 10.0, "10 strings x 10 s rendered in %.2f s (%.1f%% of real time)", sec, 100 * sec / 10.0);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
