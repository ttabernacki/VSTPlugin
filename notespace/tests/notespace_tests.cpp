// Tests for the Note Space DSP core (no JUCE needed).
#include "../core/NoteSpace.h"
#include "../../bass-leveler/tests/synth.h"

#include <chrono>
#include <cstdio>
#include <random>

using namespace nsp;

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

struct Run
{
    std::vector<float> out;
    std::vector<double> parts, res; // the split, aligned with the input
    int lat = 0;
};

static Run run (const std::vector<float>& in, const Params& p, int block = 480, double fs = kFs, bool taps = false)
{
    NoteSpace d;
    d.prepare (fs);
    d.setParams (p);
    Run r;
    r.lat = d.latencySamples();
    r.out = in;
    if (taps)
    {
        r.parts.assign (in.size(), 0.0);
        r.res.assign (in.size(), 0.0);
        for (size_t i = 0; i < in.size(); ++i)
        {
            float* c[1] = { r.out.data() + i };
            d.process (c, 1, 1);
            if (i >= (size_t) r.lat)
            {
                r.parts[i - (size_t) r.lat] = d.lastPartials();
                r.res[i - (size_t) r.lat] = d.lastResidual();
            }
        }
        return r;
    }
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        float* c[1] = { r.out.data() + i };
        d.process (c, 1, (int) std::min<size_t> ((size_t) block, in.size() - i));
    }
    return r;
}

// amplitude (dB) of one frequency over a whole number of its cycles
template <typename T>
static double ampDb (const std::vector<T>& x, double f, double t0, double cycles, double fs = kFs)
{
    const size_t i0 = (size_t) (t0 * fs), L = (size_t) std::llround (cycles * fs / f);
    double re = 0, im = 0;
    for (size_t j = 0; j < L && i0 + j < x.size(); ++j)
    {
        re += (double) x[i0 + j] * std::cos (2 * kPi * cycles * (double) j / (double) L);
        im -= (double) x[i0 + j] * std::sin (2 * kPi * cycles * (double) j / (double) L);
    }
    return 20.0 * std::log10 (2.0 / (double) L * std::sqrt (re * re + im * im) + 1e-12);
}

static const double kHarm[6] = { 1.0, 0.6, 0.4, 0.25, 0.15, 0.1 };

// a held note with steady harmonics, plus an optional steady "mud" tone
static std::vector<float> held (double f0, double secs, double mudHz = 0, double mudAmp = 0, const double* harm = kHarm, double fs = kFs)
{
    std::vector<float> x ((size_t) (secs * fs));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / fs, env = std::min (1.0, t / 0.01);
        double s = 0;
        for (int h = 1; h <= 6; ++h)
            s += harm[h - 1] * std::sin (2 * kPi * h * f0 * t + 0.3 * h);
        x[i] = (float) (0.3 * env * s + mudAmp * std::sin (2 * kPi * mudHz * t + 1.0));
    }
    return x;
}

static std::vector<float> delayed (const std::vector<float>& x, int lat)
{
    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = (size_t) lat; i < x.size(); ++i)
        y[i] = x[i - (size_t) lat];
    return y;
}

int main()
{
    Params neutral;
    neutral.contrast = neutral.toneLock = neutral.fundamentalDb = neutral.repair = neutral.translate = 0.0f;

    std::printf ("Latency and transparency\n");
    {
        double total;
        std::vector<synth::Ev> ev;
        for (int i = 0; i < 12; ++i)
            ev.push_back ({ 0.3 + 0.4 * i, 0.35, 28 + (i * 5) % 16, 0.0 });
        std::array<double, 128> flat {};
        total = 5.5;
        const auto sig = synth::render (kFs, ev, total, flat);
        const auto r = run (sig, neutral);
        double md = 0;
        const auto ref = delayed (sig, r.lat);
        for (size_t i = 0; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (r.out[i] - ref[i]));
        CHECK (r.lat < (int) (0.12 * kFs), "look-ahead %d samples (%.0f ms)", r.lat, 1000.0 * r.lat / kFs);
        CHECK (md == 0.0, "every control at neutral: the output is the input, delayed (max diff %.1e)", md);
    }

    std::printf ("The split\n");
    for (double f0 : { 31.0, 41.2, 61.7, 98.0, 146.8 })
    {
        const double mud = f0 * 1.52;
        const auto x = held (f0, 3.0, mud, 0.06);
        const auto r = run (x, neutral, 480, kFs, true);
        double worstH = -200;
        for (int h = 1; h <= 6; ++h)
            worstH = std::max (worstH, ampDb (r.res, h * f0, 1.5, 20 * h) - ampDb (x, h * f0, 1.5, 20 * h));
        const double mudIn = ampDb (x, mud, 1.5, 40), mudRes = ampDb (r.res, mud, 1.5, 40) - mudIn, mudPart = ampDb (r.parts, mud, 1.5, 40) - mudIn;
        CHECK (worstH < -30.0 && std::fabs (mudRes) < 2.0 && mudPart < -12.0,
               "%5.1f Hz note: harmonics left in the residual <= %.0f dB; a tone between harmonics goes to the residual (%+.1f dB), not the partials (%.0f dB)",
               f0, worstH, mudRes, mudPart);
    }
    {
        // nothing pitched: the split is not trusted and nothing is processed
        std::mt19937 g (3);
        std::vector<float> n (96000);
        double lp = 0;
        for (auto& v : n)
        {
            lp += 0.05 * (std::normal_distribution<double> (0, 1) (g) - lp);
            v = (float) (0.3 * lp);
        }
        Params p;
        p.contrast = 1.0f;
        p.toneLock = 1.0f;
        p.translate = 1.0f;
        p.repair = 1.0f;
        const auto r = run (n, p);
        const auto ref = delayed (n, r.lat);
        double md = 0;
        for (size_t i = 0; i < n.size(); ++i)
            md = std::max (md, (double) std::fabs (r.out[i] - ref[i]));
        CHECK (md < 1e-6, "low rumble with no pitch: passed through untouched with everything up (%.1e)", md);
    }

    std::printf ("Contrast\n");
    {
        const double f0 = 41.2, mud = 63.0;
        const auto x = held (f0, 3.0, mud, 0.06);
        for (float c : { 1.0f, -1.0f })
        {
            Params p = neutral;
            p.contrast = c;
            const auto r = run (x, p);
            const double sh = (double) r.lat / kFs;
            const double dm = ampDb (r.out, mud, 1.5 + sh, 40) - ampDb (x, mud, 1.5, 40);
            double worst = 0;
            for (int h = 1; h <= 6; ++h)
                worst = std::max (worst, std::fabs (ampDb (r.out, h * f0, 1.5 + sh, 20 * h) - ampDb (x, h * f0, 1.5, 20 * h)));
            if (c > 0)
                CHECK (dm < -9.0 && worst < 0.5, "+100%%: mud between the harmonics %+.1f dB, the note's harmonics within %.2f dB", dm, worst);
            else
                CHECK (dm > 4.0 && worst < 0.5, "-100%%: mud %+.1f dB, harmonics within %.2f dB", dm, worst);
        }
        // above Range nothing changes
        const auto x2 = held (f0, 3.0, 800.0, 0.05);
        Params p = neutral;
        p.contrast = 1.0f;
        p.rangeHz = 300.0f;
        const auto r = run (x2, p);
        const double d520 = ampDb (r.out, 800.0, 1.5 + (double) r.lat / kFs, 300) - ampDb (x2, 800.0, 1.5, 300);
        CHECK (std::fabs (d520) < 0.5, "a residual tone at 800 Hz (Range 300 Hz) is left alone (%+.2f dB)", d520);
    }

    std::printf ("Tone lock\n");
    {
        // 32 notes whose 2nd harmonic is randomly 6 dB up or down
        std::mt19937 g (5);
        std::vector<float> x ((size_t) (14 * kFs), 0.0f);
        std::vector<double> starts, f0s, h2;
        double t = 0.3;
        while (t < 13.0)
        {
            const double f0 = 41.2 * std::pow (2.0, (double) (g() % 8) / 12.0), dev = (g() % 2) ? 6.0 : -6.0;
            double hm[6];
            for (int h = 0; h < 6; ++h)
                hm[h] = kHarm[h];
            hm[1] *= std::pow (10.0, dev / 20.0);
            const size_t n0 = (size_t) (t * kFs), n = (size_t) (0.38 * kFs);
            for (size_t i = 0; i < n; ++i)
            {
                const double tt = (double) i / kFs, env = std::min (1.0, tt / 0.01) * std::min (1.0, (0.38 - tt) / 0.02);
                double s = 0;
                for (int h = 1; h <= 6; ++h)
                    s += hm[h - 1] * std::sin (2 * kPi * h * f0 * tt + 0.3 * h);
                x[n0 + i] += (float) (0.3 * env * s);
            }
            starts.push_back (t);
            f0s.push_back (f0);
            t += 0.4;
        }
        auto spread = [&] (const std::vector<float>& y, double sh) {
            std::vector<double> v;
            for (size_t i = 8; i < starts.size(); ++i)
                v.push_back (ampDb (y, 2 * f0s[i], starts[i] + 0.2 + sh, 12) - ampDb (y, f0s[i], starts[i] + 0.2 + sh, 6));
            double m = 0, s = 0;
            for (double a : v)
                m += a;
            m /= (double) v.size();
            for (double a : v)
                s += (a - m) * (a - m);
            return std::sqrt (s / (double) v.size());
        };
        Params p = neutral;
        p.toneLock = 1.0f;
        const auto r = run (x, p);
        const double before = spread (x, 0.0), after = spread (r.out, (double) r.lat / kFs);
        CHECK (after < 0.5 * before, "2nd harmonic vs fundamental across notes: spread %.1f dB -> %.1f dB", before, after);
    }

    std::printf ("Fundamental, repair, translate\n");
    {
        const double f0 = 55.0;
        const auto x = held (f0, 3.0);
        Params p = neutral;
        p.fundamentalDb = 6.0f;
        const auto r = run (x, p);
        const double sh = (double) r.lat / kFs;
        const double d1 = ampDb (r.out, f0, 1.5 + sh, 20) - ampDb (x, f0, 1.5, 20), d2 = ampDb (r.out, 2 * f0, 1.5 + sh, 40) - ampDb (x, 2 * f0, 1.5, 40);
        CHECK (std::fabs (d1 - 6.0) < 0.5 && std::fabs (d2) < 0.3, "Fundamental +6 dB: fundamental %+.2f dB, 2nd harmonic %+.2f dB", d1, d2);

        // a fundamental that beats (a second, detuned component 3 Hz above it): repair makes it a steady tone
        std::vector<float> b ((size_t) (3 * kFs));
        for (size_t i = 0; i < b.size(); ++i)
        {
            const double t = (double) i / kFs, env = std::min (1.0, t / 0.01);
            b[i] = (float) (0.3 * env * (std::sin (2 * kPi * f0 * t) + 0.35 * std::sin (2 * kPi * (f0 + 3.0) * t) + 0.5 * std::sin (2 * kPi * 2 * f0 * t)));
        }
        auto wobble = [&] (const std::vector<float>& y, double sh) {
            // level of the fundamental in 40 ms slices over one second: max - min, dB
            double lo = 1e9, hi = -1e9;
            for (double t = 1.2; t < 2.2; t += 0.04)
            {
                const double a = ampDb (y, f0, t + sh, 2);
                lo = std::min (lo, a);
                hi = std::max (hi, a);
            }
            return hi - lo;
        };
        Params pr = neutral;
        pr.repair = 1.0f;
        const auto rr = run (b, pr);
        const double wIn = wobble (b, 0.0), wOut = wobble (rr.out, (double) rr.lat / kFs);
        CHECK (wOut < 0.5 * wIn, "Repair: a beating fundamental wobbles %.1f dB before, %.1f dB after", wIn, wOut);

        // a pure sine bass: translate gives it harmonics 2-4, locked to it
        std::vector<float> s ((size_t) (3 * kFs));
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (float) (0.4 * std::min (1.0, (double) i / kFs / 0.01) * std::sin (2 * kPi * f0 * (double) i / kFs));
        Params pt = neutral;
        pt.translate = 1.0f;
        const auto rt = run (s, pt);
        const double st = (double) rt.lat / kFs;
        const double a1 = ampDb (rt.out, f0, 1.5 + st, 20), a2 = ampDb (rt.out, 2 * f0, 1.5 + st, 40) - a1, a3 = ampDb (rt.out, 3 * f0, 1.5 + st, 60) - a1,
                     a4 = ampDb (rt.out, 4 * f0, 1.5 + st, 80) - a1;
        const double a2b = ampDb (rt.out, 2 * f0, 2.3 + st, 40) - ampDb (rt.out, f0, 2.3 + st, 20);
        CHECK (std::fabs (a2 + 6.0) < 1.5 && std::fabs (a3 + 9.0) < 1.5 && std::fabs (a4 + 12.0) < 1.5 && std::fabs (a2b - a2) < 0.3,
               "Translate on a pure sine: harmonics 2/3/4 at %.1f / %.1f / %.1f dB re the fundamental, steady (%.2f dB later)", a2, a3, a4, a2b - a2);
    }

    std::printf ("Note changes\n");
    {
        double total = 6.0;
        std::vector<synth::Ev> ev;
        for (int i = 0; i < 14; ++i)
            ev.push_back ({ 0.3 + 0.4 * i, 0.38, 28 + (i * 7) % 16, 0.0 });
        std::array<double, 128> flat {};
        const auto sig = synth::render (kFs, ev, total, flat);
        Params p;
        p.contrast = 1.0f;
        p.toneLock = 0.5f;
        p.translate = 0.5f;
        const auto r = run (sig, p);
        const auto ref = delayed (sig, r.lat);
        double stepAdded = 0, stepIn = 0;
        for (size_t i = (size_t) r.lat + 2; i < sig.size(); ++i)
        {
            stepAdded = std::max (stepAdded, std::fabs ((double) (r.out[i] - ref[i]) - (double) (r.out[i - 1] - ref[i - 1])));
            stepIn = std::max (stepIn, (double) std::fabs (ref[i] - ref[i - 1]));
        }
        CHECK (stepAdded < 0.5 * stepIn, "a plucked line with everything on: what is added never jumps (%.4f vs %.4f in the dry line)", stepAdded, stepIn);
        double eIn = 0, eOut = 0;
        for (size_t i = (size_t) r.lat; i < sig.size(); ++i)
        {
            eIn += (double) ref[i] * ref[i];
            eOut += (double) r.out[i] * r.out[i];
        }
        CHECK (std::fabs (10 * std::log10 (eOut / eIn)) < 3.0, "level stays in the same place (%+.1f dB)", 10 * std::log10 (eOut / eIn));
    }

    std::printf ("Block sizes, channels, rates, robustness\n");
    {
        double total = 4.0;
        std::vector<synth::Ev> ev;
        for (int i = 0; i < 9; ++i)
            ev.push_back ({ 0.3 + 0.4 * i, 0.38, 30 + (i * 3) % 12, 0.0 });
        std::array<double, 128> flat {};
        const auto sig = synth::render (kFs, ev, total, flat);
        Params p;
        p.contrast = 0.8f;
        p.toneLock = 0.6f;
        p.repair = 0.5f;
        p.translate = 0.5f;
        p.fundamentalDb = 2.0f;
        const auto ref = run (sig, p, 512);
        for (int bs : { 1, 7, 333, 4096 })
        {
            const auto r = run (sig, p, bs);
            double md = 0;
            for (size_t i = 0; i < sig.size(); ++i)
                md = std::max (md, (double) std::fabs (r.out[i] - ref.out[i]));
            CHECK (md == 0.0, "block size %d: identical to 512-sample blocks (%.1e)", bs, md);
        }
        NoteSpace d;
        d.prepare (kFs);
        d.setParams (p);
        std::vector<float> L = sig, R = sig;
        for (size_t i = 0; i < sig.size(); i += 480)
        {
            float* c[2] = { L.data() + i, R.data() + i };
            d.process (c, 2, (int) std::min<size_t> (480, sig.size() - i));
        }
        double md = 0, mr = 0;
        for (size_t i = 0; i < sig.size(); ++i)
        {
            md = std::max (md, (double) std::fabs (L[i] - R[i]));
            mr = std::max (mr, (double) std::fabs (L[i] - ref.out[i]));
        }
        CHECK (md == 0.0 && mr < 1e-6, "identical channels stay identical and match mono (%.1e / %.1e)", md, mr);
    }
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        Params p;
        p.contrast = 1.0f;
        p.toneLock = 1.0f;
        p.translate = 1.0f;
        p.repair = 1.0f;
        p.fundamentalDb = 6.0f;
        auto x = held (55.0, 3.0, 82.0, 0.05, kHarm, sr);
        x[1000] = std::numeric_limits<float>::quiet_NaN();
        x[2000] = 4.0f;
        const auto r = run (x, p, 256, sr);
        bool fin = true;
        float peak = 0;
        for (float v : r.out)
        {
            fin = fin && std::isfinite (v);
            peak = std::max (peak, std::fabs (v));
        }
        const double mud = ampDb (r.out, 82.0, 2.0 + (double) r.lat / sr, 40, sr) - ampDb (x, 82.0, 2.0, 40, sr);
        CHECK (fin && peak < 6.0f && mud < -6.0, "%.0f Hz: finite (peak %.2f), mud %+.1f dB, look-ahead %.0f ms", sr, peak, mud, 1000.0 * r.lat / sr);
    }
    {
        // fuzz: random material and parameters, odd blocks
        std::mt19937 g (11);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        NoteSpace d;
        d.prepare (kFs);
        bool fin = true;
        float peak = 0;
        std::vector<float> a (4096);
        for (int blk = 0; blk < 800; ++blk)
        {
            const int n = 1 + (int) (u (g) * 3000);
            const int mode = (int) (u (g) * 4);
            const double f = 30.0 + 200.0 * u (g);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) (blk * 3000 + i) / kFs;
                a[(size_t) i] = mode == 0 ? 0.5f * (u (g) - 0.5f) : mode == 1 ? (float) (0.6 * std::sin (2 * kPi * f * t)) : mode == 2 ? ((i % 997) == 0 ? 0.9f : 0.0f) : 1e-30f;
            }
            Params p;
            p.contrast = u (g) * 2 - 1;
            p.toneLock = u (g);
            p.fundamentalDb = u (g) * 12 - 6;
            p.repair = u (g);
            p.translate = u (g);
            p.rangeHz = 100.0f + 400.0f * u (g);
            d.setParams (p);
            float* c[1] = { a.data() };
            d.process (c, 1, n);
            for (int i = 0; i < n; ++i)
            {
                fin = fin && std::isfinite (a[(size_t) i]);
                peak = std::max (peak, std::fabs (a[(size_t) i]));
            }
        }
        CHECK (fin && peak < 8.0f, "fuzz (800 random blocks and settings): finite, peak %.2f", peak);
    }
    {
        // CPU
        auto x = held (55.0, 20.0);
        std::vector<float> y = x;
        NoteSpace d;
        d.prepare (kFs);
        Params p;
        p.contrast = 0.6f;
        p.toneLock = 0.5f;
        p.translate = 0.5f;
        p.repair = 0.5f;
        d.setParams (p);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < x.size(); i += 512)
        {
            float* c[2] = { x.data() + i, y.data() + i };
            d.process (c, 2, (int) std::min<size_t> (512, x.size() - i));
        }
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  [INFO] CPU: 20 s of stereo in %.2f s (%.1f%% of one core)\n", s, 100.0 * s / 20.0);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
