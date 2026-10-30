#pragma once

// Measures one stereo mix (48 kHz) and returns every catalog metric with a confidence, plus the detail the recommendation
// engine needs (notes, resonances, a 1/6-octave curve, per-band stereo correlation). Deterministic: same samples in,
// same numbers out.
//
// Paths: true peak, clipping and DC on the 48 kHz input; everything spectral and low-band on a 6 kHz decimated copy
// (16384-point Welch = 0.37 Hz bins, enough for 1/12 octave at 25 Hz); pitch on the 48 kHz mid with the shared
// bass::PitchTracker (it judges notes by harmonics 1-4, so weak-fundamental bass is still tracked).

#include "Metrics.h"
#include "../../bass-common/PitchTracker.h"

#include <array>
#include <map>
#include <numeric>

namespace lee
{
struct Metric
{
    double v = NAN, conf = 0.0;
    bool valid() const { return std::isfinite (v); }
};

struct Note
{
    double t0 = 0, t1 = 0, f0 = 0; // seconds, Hz
    double levelDb = 0, hrDb = 0, residualDb = 0;
    std::array<double, 9> p {};    // power of harmonics 1..8 (index = harmonic)
};

struct Resonance
{
    double hz = 0, promDb = 0, q = 0;
};

inline const std::vector<double>& curveFreqs() // 1/6-octave grid, 20 Hz - 2 kHz (each point a 1/3-octave band)
{
    static const std::vector<double> f = [] { std::vector<double> v; for (int k = 0; k <= 40; ++k) v.push_back (20.0 * std::pow (2.0, k / 6.0)); return v; }();
    return f;
}
inline const std::vector<double>& corrFreqs() // 1/3 octave, 25 - 250 Hz
{
    static const std::vector<double> f = [] { std::vector<double> v; for (int k = 0; k <= 10; ++k) v.push_back (25.0 * std::pow (2.0, k / 3.0)); return v; }();
    return f;
}

struct Analysis
{
    std::string source;
    double seconds = 0;
    std::map<std::string, Metric> m;
    std::vector<Note> notes;
    std::vector<double> curve;    // dB re the 500 Hz - 2 kHz band (clear of low-mid moves), at curveFreqs()
    std::vector<double> bandCorr; // L/R correlation per 1/3 octave, at corrFreqs()
    std::vector<Resonance> resonances;
    double monoBelowHz = 0;       // mono-ize below this to make the low end mono-safe (0 = already safe)
    double s2Median = NAN;
    int onsets = 0;

    Metric get (const std::string& id) const
    {
        auto it = m.find (id);
        return it == m.end() ? Metric {} : it->second;
    }
};

namespace detail
{
inline double db (double p) { return 10.0 * std::log10 (std::max (p, 1e-30)); }

inline double percentile (std::vector<double> v, double q)
{
    if (v.empty())
        return NAN;
    std::sort (v.begin(), v.end());
    const double pos = q * (double) (v.size() - 1);
    const size_t i = (size_t) pos;
    return i + 1 < v.size() ? v[i] + (pos - (double) i) * (v[i + 1] - v[i]) : v[i];
}

inline double median (std::vector<double> v) { return percentile (std::move (v), 0.5); }

// windowed RMS (power) over [i, i + len) from prefix sums of squares
struct Energy
{
    std::vector<double> c;
    explicit Energy (const std::vector<double>& x) : c (x.size() + 1, 0.0)
    {
        for (size_t i = 0; i < x.size(); ++i)
            c[i + 1] = c[i] + x[i] * x[i];
    }
    double mean (long a, long b) const
    {
        a = std::clamp (a, 0L, (long) c.size() - 1);
        b = std::clamp (b, 0L, (long) c.size() - 1);
        return b > a ? (c[(size_t) b] - c[(size_t) a]) / (double) (b - a) : 0.0;
    }
};

// 4x-oversampled true peak, evaluated only around samples within 6 dB of the sample peak (the inter-sample overshoot of
// real material is far below that). Also counts inter-sample overs (interpolated > 1 between two samples that are not).
inline double truePeak (const std::vector<float>& x, int& overs)
{
    constexpr int H = 24;
    static const std::array<std::array<double, 2 * H>, 3> h = [] {
        std::array<std::array<double, 2 * H>, 3> t {};
        for (int p = 0; p < 3; ++p)
            for (int k = -H + 1; k <= H; ++k)
            {
                const double u = k - (p + 1) / 4.0, r = u / (H + 1);
                const double sinc = std::sin (kPi * u) / (kPi * u);
                t[(size_t) p][(size_t) (k + H - 1)] = sinc * std::pow (std::cos (0.5 * kPi * r), 2.0);
            }
        return t;
    }();
    double sp = 0;
    for (float v : x)
        sp = std::max (sp, (double) std::fabs (v));
    double tp = sp;
    const double thr = sp * 0.5;
    const long n = (long) x.size();
    bool inOver = false;
    for (long i = 0; i + 1 < n; ++i)
    {
        if (std::fabs (x[(size_t) i]) < thr && std::fabs (x[(size_t) i + 1]) < thr)
        {
            inOver = false;
            continue;
        }
        for (int p = 0; p < 3; ++p)
        {
            double acc = 0;
            for (int k = -H + 1; k <= H; ++k)
            {
                const long j = i + k;
                if (j >= 0 && j < n)
                    acc += x[(size_t) j] * h[(size_t) p][(size_t) (k + H - 1)];
            }
            const double a = std::fabs (acc);
            tp = std::max (tp, a);
            const bool over = a > 1.0 && std::fabs (x[(size_t) i]) <= 1.0 && std::fabs (x[(size_t) i + 1]) <= 1.0;
            if (over && ! inOver)
                ++overs;
            inOver = over;
        }
    }
    return tp;
}

// runs of >= 8 identical samples at >= 0.99 FS (a clean 20 Hz sine at full scale repeats a 16-bit value ~7 times)
inline int clipRuns (const std::vector<float>& x)
{
    int events = 0, run = 1;
    for (size_t i = 1; i < x.size(); ++i)
    {
        if (x[i] == x[i - 1] && std::fabs (x[i]) >= 0.99f)
        {
            if (++run == 8)
                ++events;
        }
        else
            run = 1;
    }
    return events;
}

// harmonic powers of a note: exactly K periods, so the harmonics are orthogonal and the kick (rarely harmonic to the
// note) leaks little
inline bool harmonics (const std::vector<double>& x, double fs, double f0, long start, long maxLen, std::array<double, 9>& p)
{
    const int K = (int) std::floor ((double) maxLen * f0 / fs);
    if (K < 3 || start < 0)
        return false;
    const long L = std::lround (K * fs / f0);
    if (start + L > (long) x.size())
        return false;
    p.fill (0.0);
    for (int hm = 1; hm <= 8; ++hm)
    {
        if (hm * f0 > 2500.0)
            break;
        const double w = 2 * kPi * K * hm / (double) L;
        double re = 0, im = 0;
        for (long j = 0; j < L; ++j)
        {
            re += x[(size_t) (start + j)] * std::cos (w * (double) j);
            im -= x[(size_t) (start + j)] * std::sin (w * (double) j);
        }
        const double amp = 2.0 / (double) L * std::sqrt (re * re + im * im);
        p[(size_t) hm] = 0.5 * amp * amp;
    }
    return true;
}
} // namespace detail

inline Analysis analyze (const Stereo& in, const std::string& source = {})
{
    using namespace detail;
    Analysis a;
    a.source = source;
    a.seconds = in.seconds();
    const double fs = in.fs;
    const double coverage = std::min (1.0, a.seconds / 10.0);
    auto put = [&] (const char* id, double v, double conf) { a.m[id] = Metric { v, std::isfinite (v) ? std::clamp (conf, 0.0, 1.0) * coverage : 0.0 }; };
    if (in.size() < (size_t) (fs * 0.5))
        return a;

    // --- technical: 48 kHz ---------------------------------------------------------------------------------------
    int overs = 0;
    const double tp = std::max (truePeak (in.L, overs), truePeak (in.R, overs));
    put ("T1", 20.0 * std::log10 (std::max (tp, 1e-10)), 1.0);
    put ("T2", (double) (clipRuns (in.L) + clipRuns (in.R) + overs), 1.0);
    {
        double sl = 0, sr = 0;
        for (size_t i = 0; i < in.size(); ++i)
        {
            sl += in.L[i];
            sr += in.R[i];
        }
        put ("T3", 100.0 * std::max (std::fabs (sl), std::fabs (sr)) / (double) in.size(), 1.0);
    }

    // --- 6 kHz path ------------------------------------------------------------------------------------------------
    const auto dL = decimate (in.L, fs), dR = decimate (in.R, fs);
    const double fd = fs / kDecim;
    std::vector<double> dM (dL.size());
    for (size_t i = 0; i < dM.size(); ++i)
        dM[i] = 0.5 * (dL[i] + dR[i]);
    const Spectra S = welch (dL, dR, fd, 16384);
    auto P = [&] (double f1, double f2) { return S.midPower (f1, f2); };

    const double low = P (20, 250);
    const double bands[][2] = { { 20, 40 }, { 40, 60 }, { 60, 90 }, { 90, 120 }, { 120, 180 }, { 180, 250 } };
    const char* bandIds[] = { "B1_20_40", "B1_40_60", "B1_60_90", "B1_90_120", "B1_120_180", "B1_180_250" };
    const bool hasLow = low > 1e-14;
    for (int b = 0; b < 6; ++b)
        put (bandIds[b], hasLow ? db (P (bands[b][0], bands[b][1]) / low) : NAN, 1.0);
    put ("B3", hasLow ? db (P (20, 60) / P (60, 120)) : NAN, 1.0);
    put ("B4a", hasLow ? db (P (150, 350) / P (60, 120)) : NAN, 1.0);
    put ("B4b", hasLow ? db (P (150, 350) / P (500, 2000)) : NAN, 1.0);
    put ("T4", hasLow ? db (P (5, 25) / P (25, 120)) : NAN, 1.0);

    const double midRef = P (500, 2000);
    // 1/3-octave bands on the 1/6-octave grid: narrower bands follow the notes the line happens to play (the key), which is
    // not what EQ should match across songs
    for (double fc : curveFreqs())
        a.curve.push_back (db (P (fc * std::pow (2.0, -1.0 / 6), fc * std::pow (2.0, 1.0 / 6)) / std::max (midRef, 1e-30)));

    // tilt: regression of 1/6-octave PSD (dB) on octaves, 30-300 Hz
    {
        std::vector<double> xs, ys;
        for (int k = 0; k < 20; ++k)
        {
            const double fc = 30.0 * std::pow (2.0, k / 6.0), f1 = fc * std::pow (2.0, -1.0 / 12), f2 = fc * std::pow (2.0, 1.0 / 12);
            xs.push_back (std::log2 (fc));
            ys.push_back (db (P (f1, f2) / (f2 - f1)));
        }
        const double mx = std::accumulate (xs.begin(), xs.end(), 0.0) / (double) xs.size(), my = std::accumulate (ys.begin(), ys.end(), 0.0) / (double) ys.size();
        double sxy = 0, sxx = 0;
        for (size_t i = 0; i < xs.size(); ++i)
        {
            sxy += (xs[i] - mx) * (ys[i] - my);
            sxx += (xs[i] - mx) * (xs[i] - mx);
        }
        put ("B2", hasLow ? sxy / sxx : NAN, 1.0);
    }

    // resonances: 1/12-octave levels on a 1/48-octave grid, 25-250 Hz, against the median of the surrounding octave
    if (hasLow)
    {
        std::vector<double> g, v;
        for (int j = 0; j <= 160; ++j)
        {
            const double fc = 25.0 * std::pow (2.0, j / 48.0), f1 = fc * std::pow (2.0, -1.0 / 24), f2 = fc * std::pow (2.0, 1.0 / 24);
            g.push_back (fc);
            v.push_back (db (P (f1, f2) / (f2 - f1)));
        }
        std::vector<double> prom (g.size());
        for (size_t j = 0; j < g.size(); ++j)
        {
            const size_t lo = j >= 24 ? j - 24 : 0, hi = std::min (g.size() - 1, j + 24);
            prom[j] = v[j] - median (std::vector<double> (v.begin() + (long) lo, v.begin() + (long) hi + 1));
        }
        std::vector<size_t> peaks;
        for (size_t j = 1; j + 1 < g.size(); ++j)
            if (prom[j] > 0 && prom[j] >= prom[j - 1] && prom[j] > prom[j + 1])
                peaks.push_back (j);
        std::sort (peaks.begin(), peaks.end(), [&] (size_t x, size_t y) { return prom[x] > prom[y]; });
        std::vector<size_t> kept;
        for (size_t j : peaks)
        {
            bool apart = true;
            for (size_t k : kept)
                apart = apart && (j > k ? j - k : k - j) >= 8;
            if (apart)
                kept.push_back (j);
        }
        put ("B5", kept.empty() ? 0.0 : std::max (0.0, prom[kept[0]]), 1.0);
        for (size_t j : kept)
        {
            if (prom[j] < 3.0)
                break;
            size_t l = j, r = j;
            while (l > 0 && prom[l - 1] > prom[j] - 3.0)
                --l;
            while (r + 1 < g.size() && prom[r + 1] > prom[j] - 3.0)
                ++r;
            const double bw = std::max (1.0, (double) (r - l)) / 48.0, k = std::pow (2.0, bw);
            a.resonances.push_back ({ g[j], prom[j], std::clamp (std::sqrt (k) / (k - 1.0), 1.0, 12.0) });
        }
    }

    // --- stereo from the spectra -----------------------------------------------------------------------------------
    {
        double worst = 0;
        bool any = false;
        for (auto& b : { std::array<double, 2> { 20, 40 }, std::array<double, 2> { 40, 80 }, std::array<double, 2> { 80, 150 } })
        {
            double ll, rr, lr;
            S.band (b[0], b[1], ll, rr, lr);
            if (ll + rr > 1e-14)
            {
                worst = any ? std::min (worst, db ((ll + rr + 2 * lr) / (2 * (ll + rr)))) : db ((ll + rr + 2 * lr) / (2 * (ll + rr)));
                any = true;
            }
        }
        put ("S1", any ? std::max (-60.0, worst) : NAN, 1.0);
        double ll, rr, lr;
        S.band (20, 120, ll, rr, lr);
        put ("S3", ll + rr > 1e-14 ? std::clamp (db ((ll + rr - 2 * lr) / std::max (ll + rr + 2 * lr, 1e-30)), -60.0, 20.0) : NAN, 1.0);
        int highestWide = -1;
        const auto& cf = corrFreqs();
        for (size_t k = 0; k < cf.size(); ++k)
        {
            S.band (cf[k] * std::pow (2.0, -1.0 / 6), cf[k] * std::pow (2.0, 1.0 / 6), ll, rr, lr);
            const double c = ll > 1e-16 && rr > 1e-16 ? lr / std::sqrt (ll * rr) : 1.0;
            a.bandCorr.push_back (c);
            if (c < 0.9)
                highestWide = (int) k;
        }
        if (highestWide >= 0)
            a.monoBelowHz = std::clamp (5.0 * std::round (cf[(size_t) highestWide] * std::pow (2.0, 1.0 / 6) / 5.0), 40.0, 200.0);
    }

    // --- low band (20-120 Hz) in time ------------------------------------------------------------------------------
    Chain bp = butterworth (true, 4, 20.0, fd);
    for (auto& b : butterworth (false, 4, 120.0, fd))
        bp.push_back (b);
    const auto lowL = filtfilt (dL, bp, 1500), lowR = filtfilt (dR, bp, 1500);
    std::vector<double> lowM (lowL.size());
    for (size_t i = 0; i < lowM.size(); ++i)
        lowM[i] = 0.5 * (lowL[i] + lowR[i]);
    const Energy E (lowM);
    const long n6 = (long) lowM.size();

    // D1: crest of active 4 s windows (1 s hop), median
    {
        const long W = std::min (n6, (long) (4 * fd)), hop = (long) fd;
        std::vector<double> rms, crest;
        for (long s = 0; s + W <= n6; s += hop)
        {
            double pk = 0;
            for (long i = s; i < s + W; ++i)
                pk = std::max (pk, std::fabs (lowM[(size_t) i]));
            rms.push_back (std::sqrt (E.mean (s, s + W)));
            crest.push_back (pk);
        }
        const double top = rms.empty() ? 0 : *std::max_element (rms.begin(), rms.end());
        std::vector<double> c;
        for (size_t i = 0; i < rms.size(); ++i)
            if (top > 1e-7 && rms[i] >= 0.1 * top)
                c.push_back (20.0 * std::log10 (crest[i] / rms[i]));
        put ("D1", c.empty() ? NAN : median (c), (double) c.size() / 4.0);
    }
    // D2: 400 ms RMS levels (100 ms hop), gated 30 dB under the loudest; 95th - 10th percentile
    std::vector<double> lv400;
    std::vector<long> lv400At;
    {
        const long W = (long) (0.4 * fd), hop = (long) (0.1 * fd);
        std::vector<double> all;
        for (long s = 0; s + W <= n6; s += hop)
        {
            all.push_back (db (E.mean (s, s + W)));
            lv400At.push_back (s);
        }
        lv400 = all;
        const double top = all.empty() ? -300 : *std::max_element (all.begin(), all.end());
        std::vector<double> act;
        for (double v : all)
            if (v >= top - 30 && top > -140)
                act.push_back (v);
        put ("D2", act.size() >= 3 ? percentile (act, 0.95) - percentile (act, 0.10) : NAN, (double) act.size() / 50.0);
    }
    // D7: peak minus loudest 3 s RMS (unweighted: K-weighting would gut this band)
    {
        double pk = 0;
        for (double v : lowM)
            pk = std::max (pk, std::fabs (v));
        const long W = std::min (n6, (long) (3 * fd)), hop = (long) (0.5 * fd);
        double best = 0;
        for (long s = 0; s + W <= n6; s += hop)
            best = std::max (best, E.mean (s, s + W));
        put ("D7", pk > 1e-7 && best > 0 ? 20.0 * std::log10 (pk) - db (best) : NAN, 1.0);
    }
    // S2: correlation of active 400 ms windows: 5th percentile (scored) and median
    {
        const long W = (long) (0.4 * fd), hop = (long) (0.1 * fd);
        const double top = lv400.empty() ? -300 : *std::max_element (lv400.begin(), lv400.end());
        std::vector<double> c;
        size_t w = 0;
        for (long s = 0; s + W <= n6; s += hop, ++w)
        {
            if (w >= lv400.size() || lv400[w] < top - 30 || top < -140)
                continue;
            double lr = 0, ll = 0, rr = 0;
            for (long i = s; i < s + W; ++i)
            {
                lr += lowL[(size_t) i] * lowR[(size_t) i];
                ll += lowL[(size_t) i] * lowL[(size_t) i];
                rr += lowR[(size_t) i] * lowR[(size_t) i];
            }
            c.push_back (ll > 0 && rr > 0 ? lr / std::sqrt (ll * rr) : 1.0);
        }
        put ("S2", c.empty() ? NAN : percentile (c, 0.05), (double) c.size() / 20.0);
        a.s2Median = c.empty() ? NAN : median (c);
    }
    // onsets: 5 ms vs 60 ms power envelopes of the low band, +4.5 dB, re-armed once the rise falls under 3 dB (kick and
    // bass attacks overlap, so the envelopes rarely settle completely), 80 ms apart; D6 at each
    std::vector<long> onsets;
    {
        const double gate = std::pow (10.0, (percentile (lv400, 0.95) - 30.0) / 10.0);
        const double af = 1.0 - std::exp (-1.0 / (0.005 * fd)), as = 1.0 - std::exp (-1.0 / (0.060 * fd));
        double pf = 0, ps = 0;
        long last = -1000000;
        bool inOnset = false;
        for (long i = 0; i < n6; ++i)
        {
            const double x2 = lowM[(size_t) i] * lowM[(size_t) i];
            pf += af * (x2 - pf);
            ps += as * (x2 - ps);
            const double rise = db (pf / std::max (ps, 1e-30));
            if (! inOnset && rise > 4.5 && pf > gate && i - last > (long) (0.08 * fd))
            {
                last = i;
                onsets.push_back (std::max (0L, i - (long) (0.005 * fd)));
                inOnset = true;
            }
            else if (rise < 3.0)
                inOnset = false;
        }
        a.onsets = (int) onsets.size();
        std::vector<double> ts;
        const long w1 = (long) (0.02 * fd), w2 = (long) (0.1 * fd);
        for (long o : onsets)
            if (o + w1 + w2 <= n6)
                ts.push_back (db (E.mean (o, o + w1) / std::max (E.mean (o + w1, o + w1 + w2), 1e-30)));
        put ("D6", ts.empty() ? NAN : median (ts), (double) ts.size() / 8.0);
    }

    // --- notes -----------------------------------------------------------------------------------------------------
    {
        bass::PitchTracker pt;
        pt.prepare (fs);
        const int64_t W = pt.windowSamples(), hop = pt.hopSamples();
        struct Fr { double t, f0; };
        std::vector<Fr> frames;
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (i % bass::PitchTracker::kSub == 0)
                pt.control();
            pt.push (0.5 * ((double) in.L[i] + in.R[i]));
            const int64_t n = (int64_t) i + 1;
            if (n % hop == 0 && n > W + hop)
            {
                float f0 = 0, pur = 0;
                const int64_t t = n - W;
                frames.push_back ({ (double) t / fs, pt.pickCentred (t, f0, pur) ? (double) f0 : 0.0 });
            }
        }
        // C5: pitched share of the time the low band is active
        {
            const double top = lv400.empty() ? -300 : *std::max_element (lv400.begin(), lv400.end());
            int active = 0, voiced = 0;
            for (auto& f : frames)
            {
                const long c = (long) (f.t * fd);
                if (db (E.mean (c - 150, c + 150)) >= top - 25 && top > -140)
                {
                    ++active;
                    voiced += f.f0 > 0 ? 1 : 0;
                }
            }
            const double secs = active * (double) hop / fs;
            put ("C5", active > 0 ? (double) voiced / active : NAN, secs / 10.0);
        }
        // segment: consecutive voiced frames within 0.6 semitone of the note's running median; split at low-band onsets
        std::vector<Note> notes;
        std::vector<double> run;
        double runT0 = 0, runT1 = 0;
        size_t nextOnset = 0;
        auto close = [&] {
            if (run.size() >= 12)
            {
                Note nt;
                nt.t0 = runT0;
                nt.t1 = runT1;
                nt.f0 = median (run);
                notes.push_back (nt);
            }
            run.clear();
        };
        for (auto& f : frames)
        {
            bool split = false;
            while (nextOnset < onsets.size() && (double) onsets[nextOnset] / fd <= f.t)
            {
                split = split || (! run.empty() && (double) onsets[nextOnset] / fd - runT0 > 0.08);
                ++nextOnset;
            }
            if (f.f0 <= 0 || split || (! run.empty() && std::fabs (12.0 * std::log2 (f.f0 / median (run))) > 0.6))
                close();
            if (f.f0 > 0)
            {
                if (run.empty())
                    runT0 = f.t;
                runT1 = f.t;
                run.push_back (f.f0);
            }
        }
        close();
        double loudest = -300;
        for (auto& nt : notes)
        {
            const long s = (long) ((nt.t0 + 0.01) * fd), len = std::min ((long) ((nt.t1 - nt.t0 - 0.01) * fd), (long) (0.3 * fd));
            if (harmonics (dM, fd, nt.f0, s, len, nt.p))
            {
                nt.levelDb = db (nt.p[1] + nt.p[2] + nt.p[3] + nt.p[4]);
                nt.hrDb = db ((nt.p[2] + nt.p[3] + nt.p[4] + nt.p[5]) / std::max (nt.p[1], 1e-30));
                loudest = std::max (loudest, nt.levelDb);
            }
            else
                nt.levelDb = -300;
        }
        for (auto& nt : notes)
            if (nt.levelDb > loudest - 30)
                a.notes.push_back (nt);
    }
    const auto& notes = a.notes;
    const double noteConf = (double) notes.size() / 8.0;
    if (notes.size() >= 3)
    {
        std::vector<double> hr;
        double mx = 0, my = 0;
        for (auto& nt : notes)
        {
            hr.push_back (nt.hrDb);
            mx += std::log2 (nt.f0);
            my += nt.levelDb;
        }
        mx /= (double) notes.size();
        my /= (double) notes.size();
        double sxy = 0, sxx = 0;
        for (auto& nt : notes)
        {
            sxy += (std::log2 (nt.f0) - mx) * (nt.levelDb - my);
            sxx += (std::log2 (nt.f0) - mx) * (std::log2 (nt.f0) - mx);
        }
        const double slope = sxx > 1e-9 ? sxy / sxx : 0.0;
        double ss = 0;
        for (auto& nt : a.notes)
        {
            nt.residualDb = nt.levelDb - (my + slope * (std::log2 (nt.f0) - mx));
            ss += nt.residualDb * nt.residualDb;
        }
        put ("C4", median (hr), noteConf);
        // a trend needs a spread of pitches: full confidence from a standard deviation of ~1.7 semitones
        put ("D4", sxx > 1e-9 ? slope : NAN, noteConf * std::min (1.0, sxx / (double) notes.size() / 0.02));
        // D3 on the residuals of the pitch trend, so frequency-dependent level (D4) does not count twice
        put ("D3", notes.size() >= 4 ? std::sqrt (ss / (double) (notes.size() - 2)) : NAN, noteConf);
    }
    else
        for (const char* id : { "C4", "D3", "D4" })
            put (id, NAN, 0);

    // --- translation -----------------------------------------------------------------------------------------------
    {
        const auto& sys = playbackSystems();
        auto balance = [&] (const Chain& model, double phon) {
            double bassP = 0, midP = 0;
            const size_t k1 = (size_t) std::ceil (40.0 / S.df), k2 = (size_t) std::ceil (2000.0 / S.df);
            for (size_t k = k1; k < k2 && k < S.LL.size(); ++k)
            {
                const double f = (double) k * S.df;
                const double p = S.midPsd (k) * chainPower (model, f, kAnalysisRate) * std::pow (10.0, equalLoudnessWeightDb (f, phon) / 10.0);
                (f < 300.0 ? bassP : midP) += p;
            }
            return midP > 1e-30 ? db (bassP / midP) : NAN;
        };
        std::vector<double> x1;
        for (auto& s : sys)
        {
            const double b = hasLow ? balance (s.model, s.phon) : NAN;
            put (("X1_" + std::string (s.id)).c_str(), b, 1.0);
            if (std::isfinite (b))
                x1.push_back (b);
        }
        if (x1.size() >= 2)
        {
            const double mean = std::accumulate (x1.begin(), x1.end(), 0.0) / (double) x1.size();
            double ss = 0;
            for (double v : x1)
                ss += (v - mean) * (v - mean);
            put ("X4", std::sqrt (ss / (double) (x1.size() - 1)), 1.0);
        }
        else
            put ("X4", NAN, 0);
        put ("X3", hasLow ? balance ({}, 80) - balance ({}, 40) : NAN, 1.0);
        // X2: a note under a system's cutoff is supported if its harmonics, after that system, are within 12 dB of its
        // unfiltered fundamental
        int pairs = 0, ok = 0;
        for (auto& nt : notes)
            for (auto& s : sys)
                if (s.cutoffHz > 0 && nt.f0 < s.cutoffHz)
                {
                    double hp = 0;
                    for (int h = 2; h <= 8; ++h)
                        hp += nt.p[(size_t) h] * chainPower (s.model, h * nt.f0, kAnalysisRate);
                    ++pairs;
                    ok += hp >= std::pow (10.0, -1.2) * nt.p[1] ? 1 : 0;
                }
        put ("X2", pairs > 0 ? (double) ok / pairs : NAN, (double) pairs / 8.0);
    }
    return a;
}
} // namespace lee
