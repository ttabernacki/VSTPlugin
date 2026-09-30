// Numerical sanity tests for the spatialiser DSP (no JUCE required).
// Usage: dsp_tests path/to/hrtf_d1_5deg.bin
#include "../Source/dsp/Spatializer.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

using namespace spat;

static int failures = 0;
#define CHECK(cond, ...)                                        \
    do {                                                        \
        const bool ok_ = (cond);                                \
        std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL");         \
        std::printf (__VA_ARGS__);                              \
        std::printf ("\n");                                     \
        if (! ok_) ++failures;                                  \
    } while (0)

static std::vector<char> blob;

struct Render
{
    std::vector<float> l, r;
};

static std::vector<float> noise (int n, unsigned seed = 1)
{
    std::mt19937 g (seed);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    std::vector<float> v ((size_t) n);
    for (auto& s : v)
        s = d (g);
    return v;
}

static Render run (const std::vector<float>& in, const SpatParams& p, double fs = 48000.0, int block = 256)
{
    Spatializer s;
    if (! s.prepare (fs, block, blob.data(), blob.size()))
    {
        std::printf ("prepare failed\n");
        std::exit (2);
    }
    Render r;
    r.l.resize (in.size());
    r.r.resize (in.size());
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - i);
        s.process (in.data() + i, nullptr, r.l.data() + i, r.r.data() + i, n, p);
    }
    return r;
}

static double rms (const std::vector<float>& v, size_t from = 0)
{
    double a = 0;
    for (size_t i = from; i < v.size(); ++i)
        a += (double) v[i] * v[i];
    return std::sqrt (a / (double) (v.size() - from));
}

// lag (samples, fractional) by which a is delayed relative to b
static double lagOf (const std::vector<float>& a, const std::vector<float>& b, int maxLag = 160)
{
    std::vector<double> xc ((size_t) (2 * maxLag + 1));
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (size_t i = 2000; i + (size_t) maxLag + 1 < a.size(); ++i)
            s += (double) a[i] * b[(size_t) ((long) i - lag)];
        xc[(size_t) (lag + maxLag)] = s;
    }
    size_t k = (size_t) (std::max_element (xc.begin(), xc.end()) - xc.begin());
    double lag = (double) k - maxLag;
    if (k > 0 && k + 1 < xc.size())
    {
        const double y0 = xc[k - 1], y1 = xc[k], y2 = xc[k + 1];
        lag += 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
    }
    return lag;
}

// energy above ~5 kHz relative to total, via first-difference cascade
static double highShare (const std::vector<float>& v)
{
    double hi = 0, all = 0;
    float p1 = 0, p2 = 0;
    for (size_t i = 2000; i < v.size(); ++i)
    {
        const float d1 = v[i] - p1, d2 = d1 - p2;
        p1 = v[i];
        p2 = d1;
        hi += (double) d2 * d2;
        all += (double) v[i] * v[i];
    }
    return hi / all;
}

// mean |dB| difference between two signals' spectra over 2-14 kHz (Goertzel at ~1/6-octave steps)
static double spectralDistance (const std::vector<float>& a, const std::vector<float>& b)
{
    auto power = [] (const std::vector<float>& v, double f) {
        const double w = 2.0 * 3.14159265358979 * f / 48000.0, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (size_t i = 4000; i < v.size(); ++i)
        {
            const double s0 = v[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - c * s1 * s2;
    };
    double sum = 0;
    int n = 0;
    for (double f = 2000.0; f < 14000.0; f *= 1.1225)
    {
        sum += std::abs (10.0 * std::log10 (power (a, f) / power (b, f)));
        ++n;
    }
    return sum / n;
}

static SpatParams dry (float az, float el, float dist = 0.2f)
{
    SpatParams p;
    p.azimuthDeg = az;
    p.elevationDeg = el;
    p.distance = dist;
    p.room = 0.0f;
    return p;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: dsp_tests hrtf.bin\n");
        return 2;
    }
    std::ifstream f (argv[1], std::ios::binary);
    blob.assign (std::istreambuf_iterator<char> (f), {});
    const auto x = noise (48000 * 2);

    std::printf ("Interaural time difference\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const auto r = run (noise ((int) fs * 2), dry (90, 0), fs);
        const double itdMs = 1000.0 * lagOf (r.l, r.r) / fs;
        CHECK (itdMs > 0.55 && itdMs < 0.95, "fs=%.0f source right: left ear lags right by %.3f ms", fs, itdMs);
    }
    {
        const auto r = run (x, dry (-90, 0));
        const double itdMs = 1000.0 * lagOf (r.l, r.r) / 48000.0;
        CHECK (itdMs < -0.55 && itdMs > -0.95, "source left: itd %.3f ms (mirror)", itdMs);
        const auto c = run (x, dry (0, 0));
        CHECK (std::abs (lagOf (c.l, c.r)) < 1.0, "front centre: itd %.2f samples", lagOf (c.l, c.r));
    }

    std::printf ("Interaural level difference\n");
    {
        const auto r = run (x, dry (90, 0));
        const double ild = 20.0 * std::log10 (rms (r.r, 2000) / rms (r.l, 2000));
        CHECK (ild > 6.0, "source right: right ear louder by %.1f dB", ild);
        const auto c = run (x, dry (0, 0));
        const double ildC = 20.0 * std::log10 (rms (c.r, 2000) / rms (c.l, 2000));
        CHECK (std::abs (ildC) < 1.5, "front centre: |ILD| %.2f dB", std::abs (ildC));
    }

    std::printf ("Spectral cues (front/back, elevation)\n");
    {
        const auto fr = run (x, dry (0, 0)), bk = run (x, dry (180, 0));
        const double sf = highShare (fr.l), sb = highShare (bk.l);
        CHECK (sf != sb && std::abs (10 * std::log10 (sf / sb)) > 1.0, "front vs back HF share differ by %.1f dB", 10 * std::log10 (sf / sb));
        const auto up = run (x, dry (0, 60)), dn = run (x, dry (0, -40));
        const double su = highShare (up.l), sd = highShare (dn.l);
        CHECK (std::abs (10 * std::log10 (su / sd)) > 1.0, "up vs down HF share differ by %.1f dB", 10 * std::log10 (su / sd));
    }

    std::printf ("Focus control\n");
    {
        auto withFocus = [] (float az, float el, float focus) {
            SpatParams p = dry (az, el);
            p.focus = focus;
            return p;
        };
        auto spectralGap = [&] (float azA, float elA, float azB, float elB, float focus) {
            const auto a = run (x, withFocus (azA, elA, focus)), b = run (x, withFocus (azB, elB, focus));
            return spectralDistance (a.l, b.l);
        };
        const double fb0 = spectralGap (0, 0, 180, 0, 0.0f), fb1 = spectralGap (0, 0, 180, 0, 1.0f);
        const double ud0 = spectralGap (0, 60, 0, -40, 0.0f), ud1 = spectralGap (0, 60, 0, -40, 1.0f);
        CHECK (fb1 > fb0, "front/back spectral distance focus 0 -> 1: %.1f -> %.1f dB", fb0, fb1);
        CHECK (ud1 > ud0, "up/down spectral distance focus 0 -> 1: %.1f -> %.1f dB", ud0, ud1);
        const auto r0 = run (x, withFocus (90, 0, 0.0f)), r1 = run (x, withFocus (90, 0, 1.0f));
        const double ild0 = 20 * std::log10 (rms (r0.r, 2000) / rms (r0.l, 2000));
        const double ild1 = 20 * std::log10 (rms (r1.r, 2000) / rms (r1.l, 2000));
        CHECK (std::abs (ild1 - ild0) < 2.5, "ILD barely changes with focus (%.1f vs %.1f dB)", ild0, ild1);
        CHECK (std::abs (lagOf (r0.l, r0.r) - lagOf (r1.l, r1.r)) < 2.0, "ITD unchanged by focus");
    }

    std::printf ("Rear cues\n");
    {
        auto withRear = [] (float az, float el, float rear) {
            SpatParams p = dry (az, el);
            p.rear = rear;
            return p;
        };
        const auto bk0 = run (x, withRear (180, 0, 0.0f)), bk1 = run (x, withRear (180, 0, 1.0f));
        const auto fr0 = run (x, withRear (0, 0, 0.0f)), fr1 = run (x, withRear (0, 0, 1.0f));
        const auto rt0 = run (x, withRear (90, 0, 0.0f)), rt1 = run (x, withRear (90, 0, 1.0f));
        const double dHf = 10 * std::log10 (highShare (bk0.l) / highShare (bk1.l));
        CHECK (dHf > 3.0, "directly behind: HF share drops %.1f dB with Rear 0 -> 1", dHf);
        CHECK (spectralDistance (fr0.l, fr1.l) < 0.05, "front unaffected by Rear (%.3f dB)", spectralDistance (fr0.l, fr1.l));
        CHECK (spectralDistance (rt0.l, rt1.l) < 0.05 && spectralDistance (rt0.r, rt1.r) < 0.05, "sides unaffected by Rear");
        const double fbGap0 = spectralDistance (fr0.l, bk0.l), fbGap1 = spectralDistance (fr1.l, bk1.l);
        CHECK (fbGap1 > fbGap0 + 2.0, "front/back spectral distance Rear 0 -> 1: %.1f -> %.1f dB", fbGap0, fbGap1);
        CHECK (rms (bk1.l, 2000) < rms (bk0.l, 2000), "behind is slightly less direct (%.2f dB)",
               20 * std::log10 (rms (bk1.l, 2000) / rms (bk0.l, 2000)));
        // sweep through the back with rear on: no clicks from coefficient updates
        Spatializer s;
        s.prepare (48000.0, 64, blob.data(), blob.size());
        std::vector<float> sine (96000), ol (96000), orr (96000);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5f * std::sin (2.0f * 3.14159265f * 700.0f * (float) i / 48000.0f);
        for (size_t i = 0; i < sine.size(); i += 64)
        {
            SpatParams p = withRear (0, 0, 1.0f);
            p.orbitHz = 1.0f;
            s.process (sine.data() + i, nullptr, ol.data() + i, orr.data() + i, 64, p);
        }
        float maxStep = 0;
        for (size_t i = 20000; i < ol.size(); ++i)
            maxStep = std::max (maxStep, std::abs (ol[i] - ol[i - 1]));
        CHECK (maxStep < 0.2f, "orbit through the back is click-free (max step %.3f)", maxStep);
    }

    std::printf ("Depth (exaggerated push/pull)\n");
    {
        auto withDepth = [] (float az, float depth, float room = 0.0f) {
            SpatParams p = dry (az, 0);
            p.depth = depth;
            p.room = room;
            return p;
        };
        const auto mid = run (x, withDepth (0, 0.0f)), nr = run (x, withDepth (0, 1.0f)), fr = run (x, withDepth (0, -1.0f));
        auto db = [] (double a, double b) { return 20 * std::log10 (a / b); };
        CHECK (db (rms (nr.l, 2000), rms (mid.l, 2000)) > 2.0, "close is louder: %+.1f dB vs neutral", db (rms (nr.l, 2000), rms (mid.l, 2000)));
        CHECK (db (rms (fr.l, 2000), rms (mid.l, 2000)) < -15.0, "far is much quieter: %+.1f dB vs neutral", db (rms (fr.l, 2000), rms (mid.l, 2000)));
        const double darker = 10 * std::log10 (highShare (mid.l) / highShare (fr.l));
        CHECK (darker > 8.0, "far is much darker: HF share %.1f dB lower", darker);
        auto bandPower = [] (const std::vector<float>& v, double f) {
            const double w = 2.0 * 3.14159265358979 * f / 48000.0, c = 2.0 * std::cos (w);
            double s1 = 0, s2 = 0;
            for (size_t i = 4000; i < v.size(); ++i)
            {
                const double s0 = v[i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            return s1 * s1 + s2 * s2 - c * s1 * s2;
        };
        const double lfNear = 10 * std::log10 ((bandPower (nr.l, 100) / bandPower (nr.l, 2000)) / (bandPower (mid.l, 100) / bandPower (mid.l, 2000)));
        CHECK (lfNear > 5.0, "close has proximity bass: +%.1f dB at 100 Hz relative to 2 kHz", lfNear);

        // direct-to-reverberant ratio falls with depth
        std::vector<float> imp (48000 * 2, 0.0f);
        imp[100] = 1.0f;
        auto drr = [&] (float depth) {
            const auto r = run (imp, withDepth (0, depth, 0.6f));
            double direct = 0, reverb = 0;
            for (size_t i = 0; i < r.l.size(); ++i)
                (i < 600 ? direct : reverb) += (double) r.l[i] * r.l[i] + (double) r.r[i] * r.r[i];
            return 10 * std::log10 (direct / reverb);
        };
        const double dNear = drr (1.0f), dMid = drr (0.0f), dFar = drr (-1.0f);
        CHECK (dNear > dMid + 6.0 && dMid > dFar + 6.0, "direct/reverb ratio close %.1f > neutral %.1f > far %.1f dB", dNear, dMid, dFar);

        // near-field ILD at 90 degrees
        const auto s0 = run (x, withDepth (90, 0.0f)), s1 = run (x, withDepth (90, 1.0f));
        const double ild0 = db (rms (s0.r, 2000), rms (s0.l, 2000)), ild1 = db (rms (s1.r, 2000), rms (s1.l, 2000));
        CHECK (ild1 > ild0 + 6.0, "close lateral source has larger ILD: %.1f -> %.1f dB", ild0, ild1);

        // sweep depth far -> close -> far with a sine: no clicks
        Spatializer sp;
        sp.prepare (48000.0, 64, blob.data(), blob.size());
        std::vector<float> sine (96000), ol (96000), orr (96000);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = 0.5f * std::sin (2.0f * 3.14159265f * 700.0f * (float) i / 48000.0f);
        for (size_t i = 0; i < sine.size(); i += 64)
        {
            SpatParams p = dry (0, 0);
            p.depth = std::cos (2.0f * 3.14159265f * (float) i / 48000.0f); // +-1, 1 Hz
            sp.process (sine.data() + i, nullptr, ol.data() + i, orr.data() + i, 64, p);
        }
        float maxStep = 0;
        bool finite = true;
        for (size_t i = 10; i < ol.size(); ++i)
        {
            maxStep = std::max (maxStep, std::abs (ol[i] - ol[i - 1]));
            finite = finite && std::isfinite (ol[i]);
        }
        CHECK (finite && maxStep < 0.5f, "depth sweep is click-free (max step %.3f)", maxStep);
    }

    std::printf ("Distance\n");
    {
        const auto near_ = run (x, dry (0, 0, 0.05f)), far_ = run (x, dry (0, 0, 0.9f));
        CHECK (rms (near_.l, 2000) > 3.0 * rms (far_.l, 2000), "near/far level ratio %.1f", rms (near_.l, 2000) / rms (far_.l, 2000));
        CHECK (highShare (far_.l) < highShare (near_.l), "far source is darker");
        SpatParams wet = dry (0, 0, 0.9f);
        wet.room = 1.0f;
        std::vector<float> imp (48000 * 2, 0.0f);
        imp[100] = 1.0f;
        const auto ir = run (imp, wet);
        double early = 0, late = 0;
        for (size_t i = 0; i < ir.l.size(); ++i)
            (i < 4800 ? early : late) += (double) ir.l[i] * ir.l[i] + (double) ir.r[i] * ir.r[i];
        CHECK (late > 0.0 && std::isfinite (late), "room tail present (late/early %.3f)", late / early);
        // correlation of L/R tail should be low (decorrelated room)
        double num = 0, dl = 0, dr = 0;
        for (size_t i = 6000; i < ir.l.size(); ++i)
        {
            num += (double) ir.l[i] * ir.r[i];
            dl += (double) ir.l[i] * ir.l[i];
            dr += (double) ir.r[i] * ir.r[i];
        }
        CHECK (std::abs (num / std::sqrt (dl * dr)) < 0.3, "room L/R correlation %.2f", num / std::sqrt (dl * dr));
    }

    std::printf ("Automation robustness\n");
    {
        // 500 Hz sine, azimuth jumps front -> back -> hard left; compare max step to steady state
        const int n = 48000;
        std::vector<float> sine ((size_t) n);
        for (int i = 0; i < n; ++i)
            sine[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 500.0f * (float) i / 48000.0f);

        Spatializer s;
        s.prepare (48000.0, 128, blob.data(), blob.size());
        std::vector<float> ol ((size_t) n), orr ((size_t) n);
        for (int i = 0; i < n; i += 128)
        {
            SpatParams p = dry (i < 16000 ? 170.0f : (i < 32000 ? -170.0f : 90.0f), 0);
            s.process (sine.data() + i, nullptr, ol.data() + i, orr.data() + i, std::min (128, n - i), p);
        }
        auto maxStep = [] (const std::vector<float>& v, size_t a, size_t b) {
            float m = 0;
            for (size_t i = a + 1; i < b; ++i)
                m = std::max (m, std::abs (v[i] - v[i - 1]));
            return m;
        };
        const float steady = maxStep (ol, 8000, 15000);
        float worst = 0;
        bool finite = true;
        for (size_t i = 0; i < ol.size(); ++i)
            finite = finite && std::isfinite (ol[i]) && std::isfinite (orr[i]);
        for (size_t a = 15000; a < 20000; a += 500)
            worst = std::max (worst, maxStep (ol, a, a + 500));
        for (size_t a = 31000; a < 36000; a += 500)
            worst = std::max (worst, maxStep (ol, a, a + 500));
        CHECK (finite, "output finite");
        CHECK (worst < 2.0f * std::max (steady, 0.05f), "no clicks across jumps (max step %.3f vs steady %.3f)", worst, steady);
    }

    std::printf ("Orbit\n");
    {
        SpatParams p = dry (0, 0);
        p.orbitHz = 1.0f;
        const auto r = run (x, p);
        // 1 Hz orbit: channel balance must swing between left- and right-heavy
        double minB = 1e9, maxB = -1e9;
        for (size_t a = 4800; a + 4800 < r.l.size(); a += 4800)
        {
            double el = 0, er = 0;
            for (size_t i = a; i < a + 4800; ++i)
            {
                el += (double) r.l[i] * r.l[i];
                er += (double) r.r[i] * r.r[i];
            }
            const double b = 10 * std::log10 (er / el);
            minB = std::min (minB, b);
            maxB = std::max (maxB, b);
        }
        CHECK (minB < -4.0 && maxB > 4.0, "balance swings %.1f .. %.1f dB", minB, maxB);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
