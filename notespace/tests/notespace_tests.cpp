// Tests for the Note Space DSP core (no JUCE needed).
#include "../core/NoteSpace.h"
#include "../../bass-leveler/tests/synth.h"

#include <chrono>
#include <limits>
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

static Run run (const std::vector<float>& in, const Params& p, int block = 480, double fs = kFs, bool taps = false, const std::vector<float>* sc = nullptr)
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
        const float* s[1] = { sc ? sc->data() + i : nullptr };
        d.process (c, 1, (int) std::min<size_t> ((size_t) block, in.size() - i), sc ? s : nullptr, sc ? 1 : 0);
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


// what a plug-in adds above 1 kHz, against the dry signal's own energy above 1 kHz (dB): a zipper or click train shows up here
static double highAddedDb (const std::vector<float>& dry, const std::vector<float>& out, int lat, double fs, bool vsTotal = false)
{
    const size_t N = dry.size();
    std::vector<double> add (N, 0.0), in (N, 0.0);
    for (size_t i = (size_t) lat; i < N; ++i)
    {
        add[i] = (double) out[i] - dry[i - (size_t) lat];
        in[i] = dry[i - (size_t) lat];
    }
    auto hp = [&] (std::vector<double> y) {
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int st = 0; st < 2; ++st)
            {
                const double q = st ? 1.3065630 : 0.5411961, w = 2 * 3.14159265358979 * 1000.0 / fs, cw = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
                const double b0 = (1 + cw) / 2 / a0, b1 = -(1 + cw) / a0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
                double z1 = 0, z2 = 0;
                for (auto& v : y)
                {
                    const double o = b0 * v + z1;
                    z1 = b1 * v - a1 * o + z2;
                    z2 = b0 * v - a2 * o;
                    v = o;
                }
            }
            std::reverse (y.begin(), y.end());
        }
        return y;
    };
    const auto H = hp (add), Hin = hp (in);
    double e = 0, ei = 0;
    for (size_t i = (size_t) lat + 4800; i + 4800 < N; ++i)
    {
        e += H[i] * H[i];
        ei += vsTotal ? (double) in[i] * in[i] : Hin[i] * Hin[i];
    }
    return 10.0 * std::log10 (e / (ei + 1e-30) + 1e-20);
}

static std::vector<float> delayed (const std::vector<float>& x, int lat)
{
    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = (size_t) lat; i < x.size(); ++i)
        y[i] = x[i - (size_t) lat];
    return y;
}

// Step one knob between two extremes every 250 ms while a line plays; the largest sample-to-sample step of what the plug-in adds
// (output minus the delayed dry signal). A knob applied instantly instead of eased in shows up as a jump here.
static double addedStep (const std::vector<float>& x, int which, bool stepping, float va, float vb)
{
    NoteSpace d;
    d.prepare (kFs);
    Params p;
    p.contrast = p.toneLock = p.fundamentalDb = p.repair = p.translate = 0.0f;
    auto set = [&] (float v) {
        switch (which)
        {
            case 0: p.contrast = v; break;
            case 1: p.toneLock = v; break;
            case 2: p.fundamentalDb = v; break;
            case 3: p.repair = v; break;
            case 4: p.translate = v; break;
            case 5: p.rangeHz = v; p.contrast = 1.0f; break;
            case 6: p.punch = v; break;
            default: p.sustain = v; break;
        }
    };
    set (va);
    std::vector<float> o = x;
    for (size_t i = 0; i < o.size(); i += 480)
    {
        if (stepping && (i / 480) % 25 == 24)
            set (((i / 480) / 25) % 2 ? vb : va);
        d.setParams (p);
        float* c[1] = { o.data() + i };
        d.process (c, 1, (int) std::min<size_t> (480, o.size() - i));
    }
    const size_t lat = (size_t) d.latencySamples(), s0 = lat + 24000;
    double mx = 0, prev = o[s0 - 1] - x[s0 - 1 - lat];
    for (size_t i = s0; i < o.size(); ++i)
    {
        const double a = o[i] - x[i - lat];
        mx = std::max (mx, std::fabs (a - prev));
        prev = a;
    }
    return mx;
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


    std::printf ("No zipper noise (what is added above 1 kHz)\n");
    {
        std::vector<synth::Ev> ev;
        double t = 0.3;
        std::mt19937 g (5);
        std::uniform_real_distribution<double> u (-1.0, 1.0);
        for (int rep = 0; rep < 3; ++rep)
            for (int m : { 28, 28, 35, 31, 33, 33, 40, 36, 38, 38, 31, 43, 41, 40, 36, 33 })
            {
                ev.push_back ({ t, 0.30, m, u (g) * 2.0 });
                t += 0.35;
            }
        std::array<double, 128> res {};
        std::mt19937 gr (11);
        for (auto& r : res)
            r = u (gr) * 6.0;
        const auto sig = synth::render (kFs, ev, t + 1.0, res);
        auto hf = [&] (const Params& p) {
            const auto r = run (sig, p);
            return highAddedDb (sig, r.out, r.lat, kFs);
        };
        Params c = neutral, f = neutral, rp = neutral, tl = neutral, tr = neutral;
        c.contrast = 1.0f;
        f.fundamentalDb = 6.0f;
        rp.repair = 1.0f;
        tl.toneLock = 1.0f;
        tr.translate = 1.0f;
        const double a = hf (c), b = hf (f), d = hf (rp), e = hf (tl), h = hf (tr);
        CHECK (a < -28.0 && b < -30.0 && d < -33.0 && e < -40.0 && h < -25.0,
               "plucked line, above 1 kHz: Contrast %.1f, Fundamental %.1f, Repair %.1f, Tone lock %.1f, Translate %.1f dB", a, b, d, e, h);
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
            p.fundamentalDb = u (g) * 36 - 18;
            p.repair = u (g);
            p.translate = 2.0f * u (g);
            p.rangeHz = 50.0f + 950.0f * u (g);
            d.setParams (p);
            float* c[1] = { a.data() };
            d.process (c, 1, n);
            for (int i = 0; i < n; ++i)
            {
                fin = fin && std::isfinite (a[(size_t) i]);
                peak = std::max (peak, std::fabs (a[(size_t) i]));
            }
        }
        CHECK (fin && peak < 40.0f, "fuzz (800 random blocks and settings): finite, peak %.2f", peak);
    }
    {
        // fuzz the sidechain paths: NaN/inf/huge sidechains, a sidechain that comes and goes, odd blocks, all rates and settings
        std::mt19937 g (77);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        bool fin = true;
        float peak = 0.0f;
        for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            NoteSpace d;
            d.prepare (fs);
            std::vector<float> a (8192), b (8192), s0 (8192), s1 (8192);
            for (int blk = 0; blk < 120; ++blk)
            {
                Params p;
                p.contrast = u (g) * 2 - 1;
                p.toneLock = u (g);
                p.fundamentalDb = (u (g) * 2 - 1) * 18;
                p.repair = u (g);
                p.translate = u (g) * 2;
                p.rangeHz = 50 + u (g) * 950;
                p.punch = u (g) * 2 - 1;
                p.sustain = u (g) * 2 - 1;
                p.kick = u (g);
                d.setParams (p);
                const int n = 1 + (int) (u (g) * 4000);
                const float f = 30.0f + 100.0f * u (g);
                for (int i = 0; i < n; ++i)
                {
                    const float x = (float) (0.4 * std::sin (2 * kPi * f * (double) (i + blk * 131) / fs));
                    a[(size_t) i] = x + 0.02f * (u (g) - 0.5f);
                    b[(size_t) i] = a[(size_t) i];
                    float k = 0.8f * (u (g) - 0.5f);
                    const float r = u (g);
                    if (r < 0.01f) k = std::numeric_limits<float>::quiet_NaN();
                    else if (r < 0.02f) k = std::numeric_limits<float>::infinity();
                    else if (r < 0.03f) k = 1e30f;
                    s0[(size_t) i] = k;
                    s1[(size_t) i] = k;
                }
                float* c[2] = { a.data(), b.data() };
                const float* sc[2] = { s0.data(), s1.data() };
                const int mode = blk % 3; // 0: no sidechain, 1: stereo, 2: mono
                d.process (c, 2, n, mode == 0 ? nullptr : sc, mode == 0 ? 0 : (mode == 1 ? 2 : 1));
                for (int i = 0; i < n; ++i)
                {
                    fin = fin && std::isfinite (a[(size_t) i]) && std::isfinite (b[(size_t) i]);
                    peak = std::max (peak, std::fabs (a[(size_t) i]));
                }
            }
        }
        CHECK (fin && peak < 40.0f, "fuzz with NaN/inf/huge sidechains, 5 sample rates: finite, peak %.2f", peak);
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

    std::printf ("Knobs that move while a note plays\n");
    {
        std::vector<synth::Ev> ev;
        for (int i = 0; i < 10; ++i)
            ev.push_back ({ 0.3 + 0.6 * i, 0.55, 28 + (i * 5) % 16, 0.0 });
        std::array<double, 128> flat {};
        auto x = synth::render (kFs, ev, 6.5, flat, 3);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += (float) (0.05 * std::sin (2 * kPi * 63.0 * (double) i / kFs)); // mud, so the residual controls have work
        const char* names[] = { "Contrast", "Tone lock", "Fundamental", "Repair", "Translate", "Range", "Punch", "Sustain" };
        const float A[] = { -1, 0, -18, 0, 0, 50, -1, -1 }, B[] = { 1, 1, 18, 1, 2, 1000, 1, 1 };
        for (int w = 0; w < 8; ++w)
        {
            const double still = std::max (addedStep (x, w, false, A[w], B[w]), addedStep (x, w, false, B[w], A[w]));
            const double step = addedStep (x, w, true, A[w], B[w]);
            CHECK (step < 1.5 * still + 1e-4, "%-11s jumping end to end every 250 ms: largest step of what is added %.5f (held still: %.5f)", names[w], step, still);
        }
    }

    std::printf ("Weak or missing fundamental\n");
    {
        // Real bass often has little energy at its fundamental (an amp or cab that cuts the lowest octave, a filtered sub): a
        // 41.6 Hz note measured on a real recording had 3 % of its energy there and 90 % on harmonics 2-4. It must be tracked.
        const double weak[6] = { 0.12, 0.9, 1.0, 0.7, 0.1, 0.05 };
        for (double f0 : { 41.6, 46.4, 58.3 })
        {
            const auto x = held (f0, 3.0, 0, 0, weak);
            NoteSpace d;
            d.prepare (kFs);
            d.setParams (neutral);
            std::vector<float> y = x;
            double pitchSum = 0;
            int voiced = 0, total = 0;
            for (size_t i = 0; i + 480 <= y.size(); i += 480)
            {
                float* c[1] = { y.data() + i };
                d.process (c, 1, 480);
                if (i > (size_t) (1.5 * kFs) + (size_t) d.latencySamples() && i < (size_t) (2.9 * kFs))
                {
                    ++total;
                    if (d.pitchHz() > 20.0f)
                    {
                        ++voiced;
                        pitchSum += d.pitchHz();
                    }
                }
            }
            CHECK (total > 0 && voiced == total && std::fabs (pitchSum / std::max (1, voiced) - f0) < 0.3,
                   "%.1f Hz note, fundamental 18 dB under the 2nd-4th harmonics: tracked in %d of %d blocks at %.2f Hz", f0, voiced, total,
                   pitchSum / std::max (1, voiced));
        }
    }

    std::printf ("Punch, sustain, kick\n");
    {
        std::vector<synth::Ev> ev;
        std::mt19937 g (9);
        std::vector<int> pitches;
        for (int p = 28; p <= 43; ++p)
            pitches.push_back (p);
        std::shuffle (pitches.begin(), pitches.end(), g);
        double t = 0.3;
        for (int p : pitches)
        {
            ev.push_back ({ t, 0.45, p, 0.0 });
            t += 0.55;
        }
        std::array<double, 128> flat {};
        const auto sig = synth::render (kFs, ev, t + 0.5, flat, 3);
        auto lowp = [] (std::vector<float> y, double fc) {
            for (int stage = 0; stage < 2; ++stage)
            {
                const double q = stage == 0 ? 0.5411961 : 1.3065630, w = 2.0 * kPi * fc / kFs, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
                const double b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
                double z1 = 0, z2 = 0;
                for (auto& v : y)
                {
                    const double o = b0 * v + z1;
                    z1 = b1 * v - a1 * o + z2;
                    z2 = b2 * v - a2 * o;
                    v = (float) o;
                }
            }
            return y;
        };
        const int lat = run (sig, neutral).lat;
        auto score = [&] (const Params& p) {
            const auto o = lowp (run (sig, p).out, 200.0);
            double early = 0, late = 0;
            int n = 0;
            for (size_t i = 3; i < ev.size(); ++i)
            {
                const size_t s0 = (size_t) (ev[i].t * kFs) + (size_t) lat;
                const size_t na = (size_t) (0.030 * kFs);
                double pk = 0, e2 = 0;
                for (size_t j = 0; j < na; ++j)
                    pk += (double) o[s0 + j] * o[s0 + j];
                pk = std::sqrt (pk / (double) na);
                const size_t a = s0 + (size_t) (0.15 * kFs), b = s0 + (size_t) (0.35 * kFs);
                for (size_t j = a; j < b; ++j)
                    e2 += (double) o[j] * o[j];
                early += 20 * std::log10 (pk + 1e-9);
                late += 10 * std::log10 (e2 / (double) (b - a) + 1e-12);
                ++n;
            }
            return std::array<double, 2> { early / n, late / n };
        };
        const auto base = score (neutral);
        Params pp = neutral, pm = neutral, sp = neutral, sm = neutral;
        pp.punch = 1.0f;
        pm.punch = -1.0f;
        sp.sustain = 1.0f;
        sm.sustain = -1.0f;
        const auto a = score (pp), b = score (pm), c = score (sp), e = score (sm);
        const double br = base[0] - base[1] / 2.0;
        auto ratio = [&] (const std::array<double, 2>& v) { return v[0] - v[1] / 2.0 - br; };
        CHECK (ratio (a) > 2.0, "punch +100%%: attack vs body %+.1f dB", ratio (a));
        CHECK (ratio (b) < -1.5, "punch -100%%: attack vs body %+.1f dB", ratio (b));
        CHECK (c[1] - base[1] > 1.5 && std::fabs (c[0] - base[0]) < 1.5, "sustain +100%%: body %+.1f dB, attack %+.1f dB", c[1] - base[1], c[0] - base[0]);
        CHECK (e[1] - base[1] < -1.5 && std::fabs (e[0] - base[0]) < 1.5, "sustain -100%%: body %+.1f dB, attack %+.1f dB", e[1] - base[1], e[0] - base[0]);

        Params all = neutral;
        all.punch = 1.0f;
        all.sustain = 1.0f;
        // A +10 dB pulse with a 3 ms rise modulates the low band, which has to put some energy into the upper harmonics. How that
        // compares with the dry line's own (tiny) energy above 1 kHz depends on how many attacks there are (-11 to -19 dB across
        // note orders, and across standard libraries), so punch is bounded against the whole signal instead; sustain moves
        // slowly and adds nearly none. Neither may click.
        const auto ra = run (sig, all), rs = run (sig, sp);
        CHECK (highAddedDb (sig, rs.out, rs.lat, kFs) < -30.0, "sustain at 100%%: %.1f dB added above 1 kHz (re the dry's own)", highAddedDb (sig, rs.out, rs.lat, kFs));
        CHECK (highAddedDb (sig, ra.out, ra.lat, kFs, true) < -48.0, "punch + sustain at 100%%: %.1f dB added above 1 kHz (re the whole dry signal)", highAddedDb (sig, ra.out, ra.lat, kFs, true));

        // block-size independence
        const auto r1 = run (sig, all, 1), r2 = run (sig, all, 333);
        double md = 0;
        for (size_t i = 0; i < sig.size(); ++i)
            md = std::max (md, (double) std::fabs (r1.out[i] - r2.out[i]));
        CHECK (md == 0.0, "punch + sustain: block size 1 and 333 are identical (%.1e)", md);
    }
    {
        // kick: a steady note and a steady mud tone; a 64 Hz kick burst every 0.6 s. The residual (mud) ducks, the note does not.
        const double f0 = 41.2, mudHz = 64.0;
        const auto bass = held (f0, 4.0, mudHz, 0.1);
        std::vector<float> kick (bass.size(), 0.0f);
        for (int bI = 0; bI < 6; ++bI)
        {
            const size_t n0 = (size_t) ((0.5 + 0.6 * bI) * kFs);
            for (size_t i = 0; i < (size_t) (0.18 * kFs) && n0 + i < kick.size(); ++i)
            {
                const double t = (double) i / kFs;
                kick[n0 + i] = (float) (0.8 * std::exp (-t / 0.06) * std::sin (2 * kPi * mudHz * t));
            }
        }
        Params off = neutral, on = neutral;
        on.kick = 1.0f;
        const auto base = run (bass, off, 480, kFs, false, &kick);
        const auto duck = run (bass, on, 160, kFs, false, &kick);
        const int lat = base.lat;
        double md = 0;
        for (size_t i = (size_t) lat; i < bass.size(); ++i)
            md = std::max (md, (double) std::fabs (base.out[i] - bass[i - (size_t) lat]));
        CHECK (md == 0.0, "kick amount 0 with a sidechain playing: bit-exact delay (%.1e)", md);
        const auto nosc = run (bass, on);
        md = 0;
        for (size_t i = (size_t) lat; i < bass.size(); ++i)
            md = std::max (md, (double) std::fabs (nosc.out[i] - bass[i - (size_t) lat]));
        CHECK (md == 0.0, "kick amount 100%% but no sidechain connected: bit-exact delay (%.1e)", md);
        const double tHit = 0.5 + 0.6 * 3 + 0.04, tQuiet = 0.5 + 0.6 * 3 + 0.50, dl = (double) lat / kFs;
        const double mudHit = ampDb (duck.out, mudHz, tHit + dl, 6) - ampDb (bass, mudHz, tHit, 6);
        const double fundHit = ampDb (duck.out, f0, tHit + dl, 4) - ampDb (bass, f0, tHit, 4);
        const double mudQuiet = ampDb (duck.out, mudHz, tQuiet + dl, 4) - ampDb (bass, mudHz, tQuiet, 4);
        CHECK (mudHit < -6.0, "the residual under a kick hit is ducked by %.1f dB", -mudHit);
        CHECK (std::fabs (fundHit) < 0.5, "the note's own fundamental is never ducked (%+.1f dB)", fundHit);
        CHECK (std::fabs (mudQuiet) < 1.5, "it lets go between hits (%+.1f dB)", mudQuiet);
        // the bass as its own sidechain, at its own level or after its fader (a scaled copy): nothing to make room for
        double mdS = 0;
        for (float gain : { 1.0f, 0.5f, 0.1f })
        {
            std::vector<float> sc = bass;
            for (auto& v : sc)
                v *= gain;
            const auto self = run (bass, on, 480, kFs, false, &sc);
            for (size_t i = (size_t) lat; i < bass.size(); ++i)
                mdS = std::max (mdS, (double) std::fabs (self.out[i] - bass[i - (size_t) lat]));
        }
        CHECK (mdS < 1e-6, "the bass track as its own sidechain (also at -6 and -20 dB): left alone (%.1e)", mdS);
        const auto r1 = run (bass, on, 1, kFs, false, &kick), r2 = run (bass, on, 333, kFs, false, &kick);
        md = 0;
        for (size_t i = 0; i < bass.size(); ++i)
            md = std::max (md, (double) std::fabs (r1.out[i] - r2.out[i]));
        CHECK (md == 0.0, "kick duck: block size 1 and 333 are identical (%.1e)", md);
    }
    {
        // The usual case: the kick lands on the bass note's attack, where the split is not trusted yet. The duck must still
        // make room there (it ducks everything below Range until the note is tracked), and let go between hits.
        std::vector<synth::Ev> ev;
        for (int i = 0; i < 10; ++i)
            ev.push_back ({ 0.3 + 0.6 * i, 0.5, 28 + (i * 5) % 16, 0.0 });
        std::array<double, 128> flat {};
        const auto sig = synth::render (kFs, ev, 6.5, flat, 3);
        std::vector<float> kick (sig.size(), 0.0f);
        for (const auto& e : ev)
            for (size_t i = 0; i < (size_t) (0.15 * kFs); ++i)
            {
                const size_t n = (size_t) (e.t * kFs) + i;
                const double tt = (double) i / kFs;
                if (n < kick.size())
                    kick[n] = (float) (0.8 * std::exp (-tt / 0.05) * std::sin (2 * kPi * 50.0 * tt));
            }
        Params on = neutral;
        on.kick = 1.0f;
        const auto r = run (sig, on, 480, kFs, false, &kick);
        double eIn = 0, eOut = 0, qIn = 0, qOut = 0;
        for (size_t k = 2; k < ev.size(); ++k)
        {
            const size_t s0 = (size_t) (ev[k].t * kFs);
            for (size_t i = s0 + (size_t) (0.005 * kFs); i < s0 + (size_t) (0.06 * kFs); ++i) // under the hit
            {
                eIn += (double) sig[i] * sig[i];
                eOut += (double) r.out[i + (size_t) r.lat] * r.out[i + (size_t) r.lat];
            }
            for (size_t i = s0 + (size_t) (0.4 * kFs); i < s0 + (size_t) (0.5 * kFs); ++i) // well after it
            {
                qIn += (double) sig[i] * sig[i];
                qOut += (double) r.out[i + (size_t) r.lat] * r.out[i + (size_t) r.lat];
            }
        }
        const double hit = 10 * std::log10 (eOut / eIn), quiet = 10 * std::log10 (qOut / qIn);
        CHECK (hit < -6.0 && std::fabs (quiet) < 1.0, "kick on every note's attack: the bass under the hit %+.1f dB, later in the note %+.1f dB", hit, quiet);
    }
    {
        // a plucked line, the kick on every beat: the duck must not add a click train
        std::vector<synth::Ev> ev;
        for (int i = 0; i < 12; ++i)
            ev.push_back ({ 0.3 + 0.5 * i, 0.4, 28 + (i * 5) % 16, 0.0 });
        std::array<double, 128> flat {};
        const auto sig = synth::render (kFs, ev, 6.5, flat, 3);
        std::vector<float> kick (sig.size(), 0.0f);
        for (int bI = 0; bI < 12; ++bI)
            for (size_t i = 0; i < (size_t) (0.2 * kFs); ++i)
            {
                const size_t n = (size_t) ((0.3 + 0.5 * bI) * kFs) + i;
                if (n < kick.size())
                    kick[n] = (float) (0.8 * std::exp (-(double) i / kFs / 0.06) * std::sin (2 * kPi * 60.0 * (double) i / kFs));
            }
        Params p = neutral;
        p.kick = 1.0f;
        p.contrast = 1.0f;
        const auto r = run (sig, p, 480, kFs, false, &kick);
        CHECK (highAddedDb (sig, r.out, r.lat, kFs) < -25.0, "kick duck + contrast on a plucked line: nothing added above 1 kHz (%.1f dB)", highAddedDb (sig, r.out, r.lat, kFs));
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
