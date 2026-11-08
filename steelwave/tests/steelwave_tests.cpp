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

    std::printf ("Tuning: every E9 string, bar at 0/5/12/19/24 (picked moderately: loud notes are a little sharp)\n");
    {
        double worst = 0;
        for (int si = 0; si < 10; ++si)
            for (int bar : { 0, 5, 12, 19, 24 })
            {
                auto x = render (si, 1.0, 0.4, quiet, [&] (SteelString& s, double t) {
                    if (t == 0) { s.setBar (bar, 0); s.pluck (0.4); }
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
                auto x = render (si, 1.0, 0.4, quiet, [&] (SteelString& s, double t) {
                    if (t == 0) { s.setBar (bar, 0); s.pluck (0.4); }
                }, fs);
                const double target = midiHz (strings[(size_t) si].openMidi + bar);
                worst = std::max (worst, std::fabs (cents (peakHz (x, target, 0.3, 0.6, 60, fs), target)));
            }
        CHECK (worst < 1.0, "%.1f kHz: worst error %.3f cents", fs / 1000, worst);
    }

    std::printf ("Glides land in tune\n");
    {
        auto x = render (3, 1.4, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.4);
            if (std::fabs (t - 0.3) < 0.5 / kFs) s.setBar (2, 0.2);
        });
        const double e = cents (peakHz (x, midiHz (66), 0.8, 0.5), midiHz (66));
        CHECK (std::fabs (e) < 1.0, "bar slide E4 -> F#4 lands within %.3f cents", e);
        auto y = render (5, 1.4, 0.8, quiet, [&] (SteelString& s, double t) {
            if (t == 0) s.pluck (0.4);
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
        const double f10 = peakHz (x, 10 * f, 0.1, 0.3, 30);
        const double h1 = dftMag (x, f10, 0.1, 0.3), h2 = dftMag (x, f10, 1.0, 0.3);
        const double rate10 = (db (h1) - db (h2)) / 0.9, rate1 = (db (a1) - db (a2)) / 2.5;
        CHECK (rate10 > 1.5 * rate1, "upper partials die faster: 10th %.1f dB/s vs fundamental %.1f dB/s", rate10, rate1);
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
        // calibrated against picking: a background texture, not an effect
        auto slide = [&] (double noise, int si) {
            StringParams q;
            q.barNoise = noise;
            return render (si, 1.2, 0.0, q, [&] (SteelString& s, double t) {
                if (t == 0) s.setDamp (1); // muted, so we hear the contact noise and not the string ringing on
                if (std::fabs (t - 0.2) < 0.5 / kFs) s.setBar (5, 0.6);
            });
        };
        auto pickRef = [&] (int si) { return rms (render (si, 0.6, 0.7, StringParams {}), 0.0, 0.5); };
        auto withN = slide (0.5, 7), without = slide (0.0, 7);
        const double rel = db (rms (withN, 0.3, 0.7) / pickRef (7));
        CHECK (rms (without, 0.3, 0.7) < 1e-9, "Bar noise 0: silent slide");
        CHECK (rel > -54 && rel < -42, "wound string slide (default level): %.1f dB below a medium pick (42..54: through a pickup)", -rel);
        auto plain = slide (0.5, 2);
        const double relP = db (rms (plain, 0.3, 0.7) / pickRef (2));
        CHECK (relP < rel - 6, "plain string: %.1f dB below a pick, quieter than wound", -relP);
        CHECK (rms (withN, 1.1, 1.2) < 0.2 * rms (withN, 0.3, 0.7), "noise stops when the bar stops");
        // band-limited: little above 6 kHz, no narrow whistle
        double band = 0, high = 0, peak = 0, sum = 0;
        int nb = 0;
        for (double f = 400; f < 3500; f *= 1.03, ++nb)
        {
            const double m = dftMag (withN, f, 0.3, 0.4);
            band += m * m; sum += m; peak = std::max (peak, m);
        }
        for (double f = 6000; f < 12000; f *= 1.03) { const double m = dftMag (withN, f, 0.3, 0.4); high += m * m; }
        CHECK (db (std::sqrt (high / band)) < -15, "energy above 6 kHz %.1f dB vs 400-3500 Hz (< -15)", db (std::sqrt (high / band)));
        CHECK (peak < 8.0 * sum / nb, "no narrow resonance in the noise (peak %.1fx the mean)", peak / (sum / nb));
    }

    std::printf ("Tension modulation\n");
    {
        auto x = render (3, 2.0, 1.0, quiet);
        const double early = cents (peakHz (x, midiHz (64), 0.005, 0.04, 30), midiHz (64));
        const double late = cents (peakHz (x, midiHz (64), 1.0, 0.6, 30), midiHz (64));
        CHECK (early > 2.0 && early < 9.0, "hard pick starts %.1f cents sharp (2..9)", early);
        // (coupled planes push their frequencies apart: the long-tail mode sits a fraction of a cent low)
        CHECK (late > -0.6 && late < 0.8, "and comes down as the note decays (%.2f cents after 1 s)", late);
        auto soft = render (3, 0.3, 0.3, quiet);
        const double se = cents (peakHz (soft, midiHz (64), 0.005, 0.04, 30), midiHz (64));
        CHECK (se < 1.0, "a soft pick hardly stretches the string (%.2f cents)", se);
        StringParams lin = quiet;
        lin.nonlinear = 0;
        lin.pickupNonlinear = 0; // the string's own nonlinearity is what's being tested here
        auto y = render (3, 2.0, 1.0, lin);
        const double le = cents (peakHz (y, midiHz (64), 0.005, 0.04, 30), midiHz (64));
        // (the pickup hears mostly the vertical plane, tuned 1.1 cents above the horizontal for the beating)
        CHECK (std::fabs (le) < 1.0, "Nonlinear 0: a linear string, in tune from the start (%.2f cents)", le);
        // growl: what the nonlinearity adds, apart from the pitch, relative to the note
        auto growl = [&] (double vel) {
            StringParams on = quiet, off = quiet;
            off.nonlinear = 0;
            auto a = render (9, 1.0, vel, on), b = render (9, 1.0, vel, off);
            double d = 0, r = 0;
            for (size_t i = 0; i < (size_t) (0.05 * kFs); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); r += (double) b[i] * b[i]; }
            return 10 * std::log10 (d / r);
        };
        const double gHard = growl (1.0), gSoft = growl (0.3);
        CHECK (gHard > -40 && gHard < -12, "hard pick on the low B: the nonlinearity changes the first 50 ms by %.1f dB re the note", gHard);
        CHECK (gSoft < gHard - 10, "a soft pick much less (%.1f dB)", gSoft);
    }

    std::printf ("Pickup\n");
    {
        // the field strengthening near the pole: what it adds to the sound, growing with playing strength
        auto added = [&] (int si, double vel) {
            StringParams on = quiet, off = quiet;
            on.pickVariation = off.pickVariation = 0;
            off.pickupNonlinear = 0;
            auto a = render (si, 0.4, vel, on), b = render (si, 0.4, vel, off);
            double d = 0, r = 0;
            for (size_t i = 0; i < a.size(); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); r += (double) b[i] * b[i]; }
            return 10 * std::log10 (d / r);
        };
        const double hard = added (3, 1.0), soft = added (3, 0.3), hardLow = added (9, 1.0);
        CHECK (hard > -32 && hard < -14, "hard pick: the pickup's curve adds %.1f dB (re the note)", hard);
        CHECK (soft < hard - 8, "soft pick: much less (%.1f dB)", soft);
        CHECK (hardLow > -32 && hardLow < -14, "low B, hard: %.1f dB (pole set lower under the wound strings)", hardLow);
    }

    std::printf ("Picking hand\n");
    {
        // no two picks quite alike: same string, same velocity, slightly different each time
        SteelString st;
        st.prepare (kFs, strings[3]);
        StringParams q = quiet;
        st.setParams (q);
        std::vector<float> a ((size_t) (0.3 * kFs)), b ((size_t) (0.3 * kFs));
        st.pluck (0.7);
        for (auto& v : a) v = st.process();
        st.setDamp (1);
        for (int i = 0; i < (int) (0.5 * kFs); ++i) st.process();
        st.pluck (0.7);
        for (auto& v : b) v = st.process();
        double d = 0, r = 0;
        for (size_t i = 0; i < a.size(); ++i) { d += (double) (a[i] - b[i]) * (a[i] - b[i]); r += (double) a[i] * a[i]; }
        const double lvl = db (rms (a, 0, 0.3)) - db (rms (b, 0, 0.3));
        CHECK (10 * std::log10 (d / r) > -30 && std::fabs (lvl) < 1.5, "two picks differ in detail (%.1f dB) but not in level (%.2f dB)", 10 * std::log10 (d / r), lvl);
        // the pick drags before it lets go: the note starts ~1-3 ms after the pick lands
        auto onset = [&] (int si, double vel) {
            auto x = render (si, 0.05, vel, q);
            for (size_t i = 0; i < x.size(); ++i)
                if (std::fabs (x[i]) > 0.02) return 1000.0 * i / kFs;
            return 99.0;
        };
        const double fingerHard = onset (3, 1.0), fingerSoft = onset (3, 0.4), thumb = onset (8, 0.4);
        CHECK (fingerHard > 0.5 && fingerHard < 2.0 && fingerSoft > fingerHard && fingerSoft < 3.5,
               "finger pick lets go after %.1f ms (hard) / %.1f ms (soft)", fingerHard, fingerSoft);
        CHECK (thumb > fingerSoft * 0.95 && thumb < 4.5, "thumb pick (string 9) drags a little longer: %.1f ms", thumb);
        // the thumb (plastic, low strings) is softer than the metal finger picks
        auto bright = [&] (int si) {
            StringParams r2 = quiet;
            r2.pickVariation = 0;
            auto x = render (si, 0.3, 0.8, r2);
            const double f = midiHz (strings[(size_t) si].openMidi);
            double num = 0, den = 0;
            for (int k = 1; k <= 20; ++k) { const double m = dftMag (x, k * f, 0.01, 0.12); num += k * m; den += m; }
            return num / den;
        };
        CHECK (bright (4) > bright (5), "finger pick on string 5 brighter than the thumb on string 6 (centroid %.2f vs %.2f harmonics)", bright (4), bright (5));
    }

    std::printf ("Bridge and body\n");
    {
        // two-stage decay: a fast early drop, then a long tail
        auto x = render (3, 4.0, 0.7, quiet);
        const double early = (db (rms (x, 0.05, 0.15)) - db (rms (x, 0.35, 0.45))) / 0.3;
        const double late = (db (rms (x, 1.5, 1.7)) - db (rms (x, 3.5, 3.7))) / 2.0;
        CHECK (early > 1.5 * late, "two-stage decay: %.1f dB/s early, %.1f dB/s in the tail", early, late);
        // a fundamental sitting on a body resonance (340 Hz) gives its energy away faster than one between
        auto rate = [&] (double hz) {
            const double semis = 12 * std::log2 (hz / midiHz (64));
            auto y = render (3, 3.0, 0.4, quiet, [&] (SteelString& s, double t) { if (t == 0) { s.setBar (semis, 0); s.pluck (0.4); } });
            return (db (dftMag (y, hz, 0.5, 0.5)) - db (dftMag (y, hz, 2.3, 0.5))) / 1.8;
        };
        const double on = rate (340.0), off = rate (420.0);
        CHECK (on > off + 1.0, "on a body resonance the note decays faster (%.1f vs %.1f dB/s)", on, off);
    }

    std::printf ("Behind the bar\n");
    {
        // bar at fret 4 on the E string: the 1/5 behind the bar rings near 1598 Hz, between the note's partials
        auto at = [&] (double behind, double bar) {
            StringParams q = quiet;
            q.behindBar = behind;
            return render (3, 1.0, 0.7, q, [&] (SteelString& s, double t) { if (t == 0) { s.setBar (bar, 0); s.pluck (0.7); } });
        };
        const double fb = midiHz (64) / (1.0 - std::exp2 (-4.0 / 12.0));
        auto on = at (1.0, 4), off = at (0.0, 4), half = at (0.5, 4);
        const double rel = db (dftMag (on, fb, 0.01, 0.1)) - db (dftMag (on, midiHz (68), 0.01, 0.1));
        CHECK (db (dftMag (on, fb, 0.01, 0.1)) - db (dftMag (off, fb, 0.01, 0.1)) > 15, "fret 4: the segment behind the bar rings at %.0f Hz (%.1f dB re the note)", fb, rel);
        CHECK (rel > -36 && rel < -20, "faint: %.1f dB below the note at Behind bar 100%%", -rel);
        const double hr = db (dftMag (half, fb, 0.01, 0.1)) - db (dftMag (on, fb, 0.01, 0.1));
        CHECK (hr > -8 && hr < -4, "Behind bar 50%%: %.1f dB", hr);
        auto open1 = at (1.0, 0), open0 = at (0.0, 0);
        bool same = true;
        for (size_t i = 0; i < open1.size(); ++i) same &= open1[i] == open0[i];
        CHECK (same, "open string (bar at the nut): nothing behind it");
        // at fret 12 both halves share a pitch: they trade a little energy, the note isn't drained or pulled
        auto u1 = at (1.0, 12), u0 = at (0.0, 12);
        const double drain = db (rms (u0, 0.3, 0.9)) - db (rms (u1, 0.3, 0.9));
        const double pull = cents (peakHz (u1, midiHz (76), 0.3, 0.6), midiHz (76));
        CHECK (std::fabs (drain) < 2.0 && std::fabs (pull) < 1.0, "fret 12 (unison behind the bar): level %+.1f dB, pitch %+.2f cents", -drain, pull);
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
