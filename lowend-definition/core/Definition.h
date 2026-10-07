#pragma once

// Low-End Definition: DSP core (no JUCE).
//
//  * Everything below the Range frequency is split off (complementary: high = input - low, so with every
//    control at neutral the output is the delayed input).
//  * Pitch-aware contrast: a YIN tracker finds the note under the look-ahead window. A bell on the
//    fundamental and an opposite-signed bell between the 1st and 2nd harmonic raise (or, negative, soften)
//    the note's contrast against the rest of the low band. Boosting fades out by itself as the note
//    gets pure (the "definition" estimate), so it cannot over-cook a clean tone.
//  * Punch / Sustain: a fast-vs-slow envelope comparison on the low band gives a smooth gain that
//    is applied a few ms ahead of the detector (look-ahead), so attacks can be emphasised or tamed
//    without the detector's own lag.
//  * Definition meter: the share of low-band energy that sits on the note's fundamental, measured on the
//    input and on the processed output.
//
//  * Kick awareness (optional sidechain): a spectral duck of the bass against the kick, per frequency bin of a short-time
//    spectrum of the low band, with the note's own harmonics protected (the pitch tracker says where they are). It is
//    added to the output as a difference signal, inside the look-ahead, so it costs no extra delay. A running
//    Masking mode asks instead which bass components mask the kick (roex auditory filters, tonality offset, equal-loudness
//    weighting) and ducks just enough to uncover it. A running kick/bass correlation reports whether the two partly cancel, and can flip the bass polarity.
//
// All state advances sample by sample and the control logic runs every kSub samples, so the output does not
// depend on the host's block size.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace led
{
constexpr double kPi = 3.14159265358979323846;

struct Params
{
    float contrast = 0.5f; // -1 .. 1   soften .. tighten around the note
    float punch = 0.3f;    // -1 .. 1   tame .. emphasise the attack
    float sustain = 0.0f;  // -1 .. 1   shorten .. lengthen the body
    float rangeHz = 200.0f; // crossover: everything below is processed
    bool match = true;      // keep the low band's loudness: contrast moves energy around, it should not just add level
    float kick = 0.6f;      // 0..1  how far the bass is ducked (up to 12 dB) where the kick masks it; needs a sidechain
    int align = 0;          // 0 = only report the kick/bass polarity, 1 = flip the bass polarity when that helps
    int kickMode = 0;       // 0 = Simple (kick's share of each bin), 1 = Masking (duck only what actually masks the kick)
};

// 512-point complex FFT (radix 2), in doubles
struct Fft512
{
    static constexpr int N = 512;
    double cs[N / 2], sn[N / 2];
    int rev[N];
    Fft512()
    {
        for (int i = 0; i < N / 2; ++i)
        {
            cs[i] = std::cos (2.0 * kPi * i / N);
            sn[i] = -std::sin (2.0 * kPi * i / N);
        }
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < 9; ++b)
                if (i & (1 << b))
                    r |= 1 << (8 - b);
            rev[i] = r;
        }
    }
    void run (double* re, double* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i)
            if (rev[i] > i)
            {
                std::swap (re[i], re[rev[i]]);
                std::swap (im[i], im[rev[i]]);
            }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const double wr = cs[j * step], wi = inverse ? -sn[j * step] : sn[j * step];
                    const int a = i + j, b = a + half;
                    const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
        if (inverse)
            for (int i = 0; i < N; ++i)
            {
                re[i] /= N;
                im[i] /= N;
            }
    }
};

class Definition
{
public:
    static constexpr int kSub = 16;

    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        D_ = std::max (1, (int) std::floor (sr_ / 5000.0));
        fsd_ = sr_ / D_;
        tauMax_ = (int) std::ceil (fsd_ / 30.0) + 2;
        tauMin_ = std::max (4, (int) std::floor (fsd_ / 260.0));
        N_ = 2 * tauMax_;
        hopD_ = std::max (4, (int) std::lround (0.002 * fsd_));
        hopIn_ = (int64_t) hopD_ * D_;
        W_ = (int64_t) N_ * D_;
        // spectral stage: a ~72 ms window with 87.5 % overlap on the decimated low band; its output is final one window
        // (+ a hop, the group delay of the analysis filter and the interpolator) after the audio it describes
        Ns_ = std::min (Fft512::N, (int) std::lround (0.072 * fsd_));
        Ns_ = (Ns_ / 8) * 8;
        hopS_ = Ns_ / 8;
        lat_ = (int) (W_ + hopIn_ + 64);
        lat_ = std::max (lat_, (int) ((int64_t) (Ns_ + hopS_ + 4) * D_ + (int64_t) std::lround (0.0075 * sr_) + 64));
        lat_ = ((lat_ + kSub - 1) / kSub) * kSub;
        sw_.assign ((size_t) Ns_, 0.0);
        for (int j = 0; j < Ns_; ++j)
            sw_[(size_t) j] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * j / Ns_));
        olaNorm_ = 1.0;
        {
            double sum = 0.0; // Hann window summed over the hop grid
            for (int k = 0; k < 8; ++k)
            {
                const double w = sw_[(size_t) (k * hopS_)];
                sum += w * w;
            }
            olaNorm_ = 1.0 / sum;
        }
        binHz_ = fsd_ / Fft512::N;
        kMax_ = std::min (Fft512::N / 2 - 2, (int) std::ceil (380.0 / binHz_));
        dk_.assign (kRing, 0.0f);
        dd_.assign (kRing, 0.0f);
        // auditory-filter spreading between analysis bins (roex, Glasberg-Moore ERB), lower side shallower
        const int nb = kMax_ + 2;
        wm_.assign ((size_t) nb * (size_t) nb, 0.0f);
        eq_.assign ((size_t) nb, 1.0f);
        for (int j = 0; j < nb; ++j)
        {
            const double fj = std::max (30.0, j * binHz_), erb = 24.7 * (4.37 * fj / 1000.0 + 1.0), p = 4.0 * fj / erb;
            for (int i = 0; i < nb; ++i)
            {
                const double fi = i * binHz_, g = std::fabs (fi - fj) / fj, pp = fi < fj ? 0.75 * p : p;
                wm_[(size_t) j * (size_t) nb + (size_t) i] = (float) ((1.0 + pp * g) * std::exp (-pp * g));
            }
            const double f4 = std::pow (80.0 / fj, 4.0);
            eq_[(size_t) j] = (float) (0.4 + 0.6 / std::sqrt (1.0 + f4)); // the sub matters less to the ear (and to small speakers)
        }
        antBlk_ = std::max (1, (int) std::lround (0.004 * sr_ / kSub));
        dsize_ = 1;
        while (dsize_ < lat_ + 4096)
            dsize_ <<= 1;
        for (auto& d : dl_)
            d.assign ((size_t) dsize_, 0.0f);
        win_.assign ((size_t) N_, 0.0f);
        diff_.assign ((size_t) tauMax_ + 2, 0.0f);
        cmnd_.assign ((size_t) tauMax_ + 2, 0.0f);
        din_.assign (kRing, 0.0f);
        dout_.assign (kRing, 0.0f);
        gHist_.assign (kGHist, 0.0f);
        af_ = 1.0 - std::exp (-1.0 / (0.008 * sr_));
        as_ = 1.0 - std::exp (-1.0 / (0.060 * sr_));
        am_ = 1.0 - std::exp (-1.0 / (0.030 * sr_));
        al_ = 1.0 - std::exp (-1.0 / (0.120 * sr_));
        rangeApplied_ = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& d : dl_)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (din_.begin(), din_.end(), 0.0f);
        std::fill (dout_.begin(), dout_.end(), 0.0f);
        std::fill (gHist_.begin(), gHist_.end(), 0.0f);
        lpA_ = {};
        lpK_ = {};
        std::fill (dk_.begin(), dk_.end(), 0.0f);
        std::fill (dd_.begin(), dd_.end(), 0.0f);
        gKick_.fill (0.0);
        lastFrameEnd_ = -1;
        deltaGaps_ = 0;
        kickQuiet_ = 0;
        selfSc_ = false;
        pk_ = 0.0;
        kickDuckDb_ = 0.0f;
        cc_.fill (0.0);
        ebb_ = ekk_ = activeSec_ = 0.0;
        alignDb_ = 0.0f;
        alignLagMs_ = 0.0f;
        alignRho_ = 0.0f;
        polTarget_ = 1.0;
        polSm_ = 1.0;
        krFrames_ = 0;
        for (auto& l : lpD_)
            l = {};
        for (auto& c : bz_)
            for (auto& z : c)
                z = {};
        s1_ = s2_ = Svf {};
        m1_ = m2_ = 0.0;
        nIn_ = 0;
        m_ = 0;
        pf_ = ps_ = pm_ = pl_ = 0.0;
        gs_ = gOut_ = 0.0;
        gAmp_ = gStep_ = 0.0;
        gAmp_ = 1.0;
        frameCount_ = 0;
        frames_.fill (Frame {});
        onsetN_ = 0;
        onsets_.fill (-1000000000LL);
        onsetAmp_.fill (0.0f);
        onsetHead_ = 0;
        dPeak_ = 0.0;
        pFresh_ = true;
        inOnset_ = false;
        lastOnsetT_ = -1000000;
        freq_ = 0.0;
        scale_ = 0.0;
        pS_ = 0.0;
        gain1Db_ = matchDb_ = cSm_ = 0.0;
        rangeSm_ = 0.0;
        pitchOut_ = 0.0f;
        pInSm_ = pOutSm_ = 0.0f;
        meterValid_ = false;
        transDb_ = 0.0f;
        rangeApplied_ = -1.0f;
    }

    int latencySamples() const { return lat_; }
    void setParams (const Params& p) { prm_ = p; }

    // for the UI and the tests
    float transientGainDb() const { return transDb_; }
    float contrastGainDb() const { return (float) gain1Db_; }
    float matchGainDb() const { return (float) matchDb_; }
    float kickDuckDb() const { return kickDuckDb_; }          // deepest duck of the latest spectral frame, dB (>= 0)
    float alignDb() const { return alignDb_; }                 // kick+bass summed vs kick-bass: > 0 good, < 0 they partly cancel
    float alignLagMs() const { return alignLagMs_; }           // delaying the bass by this much would line it up best with the kick
    float alignCorrelation() const { return alignRho_; }       // correlation at that lag
    bool polarityFlipped() const { return polTarget_ < 0.0; }
    bool alignKnown() const { return activeSec_ > 1.5; }
    bool sidechainIsBass() const { return selfSc_; }           // the sidechain carries (nearly) the same signal as the input: a routing mistake
    int64_t spectralGaps() const { return deltaGaps_; } // samples where the kick duck was not ready in time (should stay 0)
    float pitchHz() const { return pitchOut_; }
    bool meterValid() const { return meterValid_; }
    float definitionIn() const { return pInSm_; }   // 0..1
    float definitionOut() const { return pOutSm_; } // 0..1

    // sc: optional sidechain (the kick), scCh channels, same length n
    void process (float* const* ch, int nCh, int n, const float* const* sc = nullptr, int scCh = 0)
    {
        nCh = std::min (nCh, 2);
        const int mask = dsize_ - 1;
        for (int i = 0; i < n; ++i)
        {
            if (nIn_ % kSub == 0)
                updateControl();
            double x[2];
            for (int c = 0; c < 2; ++c)
            {
                x[c] = ch[std::min (c, nCh - 1)][i];
                if (! std::isfinite (x[c]))
                    x[c] = 0.0;
            }
            double k = 0.0;
            if (sc != nullptr && scCh > 0)
            {
                k = sc[0][i];
                if (scCh > 1)
                    k = 0.5 * (k + sc[1][i]);
                if (! std::isfinite (k))
                    k = 0.0;
            }
            analyse (0.5 * (x[0] + x[1]), k);
            const double delta = kickDelta();

            const int wr = (int) (nIn_ & mask);
            double lowMono = 0.0;
            gAmp_ += gStep_;
            for (int c = 0; c < nCh; ++c)
            {
                dl_[c][(size_t) wr] = (float) x[c];
                const double d = dl_[c][(size_t) ((wr - lat_) & mask)];
                // the low band is read advanced by the filter's group delay, so it lines up with d in time
                const double xa = (1.0 - advFrac_) * dl_[c][(size_t) ((wr - lat_ + advI_) & mask)] + advFrac_ * dl_[c][(size_t) ((wr - lat_ + advI_ + 1) & mask)];
                const double low = lpD_[c].process (xa);
                double y = bell (s1_, bz_[c][0], m1_, low);
                y = bell (s2_, bz_[c][1], m2_, y) * gAmp_;
                y += delta;
                lowMono += y;
                ch[c][i] = (float) (polSm_ * ((d - low) + y));
            }
            if (nIn_ % D_ == 0)
                dout_[(size_t) ((m_ - 1) & (kRing - 1))] = (float) (lowMono / nCh);
            ++nIn_;
        }
    }

private:
    static constexpr int kRing = 4096, kGHist = 4096, kFrames = 1024;
    static constexpr double kPunchDb = 10.0, kS = 8.0, kCeil = 12.0;

    struct Svf
    {
        double g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0;
    };
    struct St
    {
        double ic1 = 0, ic2 = 0;
    };
    static void setSvf (Svf& s, double f, double sr, double k)
    {
        f = std::min (f, 0.45 * sr);
        s.g = std::tan (kPi * f / sr);
        s.k = k;
        s.a1 = 1.0 / (1.0 + s.g * (s.g + k));
        s.a2 = s.g * s.a1;
        s.a3 = s.g * s.a2;
    }
    static inline void tick (const Svf& s, St& z, double x, double& v1, double& v2)
    {
        const double v3 = x - z.ic2;
        v1 = s.a1 * z.ic1 + s.a2 * v3;
        v2 = z.ic2 + s.a2 * z.ic1 + s.a3 * v3;
        z.ic1 = 2.0 * v1 - z.ic1;
        z.ic2 = 2.0 * v2 - z.ic2;
        if (std::fabs (z.ic1) < 1e-20)
            z.ic1 = 0;
        if (std::fabs (z.ic2) < 1e-20)
            z.ic2 = 0;
    }
    static inline double bell (const Svf& s, St& z, double mix, double x)
    {
        double v1, v2;
        tick (s, z, x, v1, v2);
        return x + mix * v1;
    }
    struct Lr4 // Linkwitz-Riley low-pass: two Butterworth 2nd-order stages
    {
        Svf s;
        St z[2];
        double process (double x)
        {
            double v1, v2;
            tick (s, z[0], x, v1, v2);
            tick (s, z[1], v2, v1, v2);
            return v2;
        }
    };
    struct Frame
    {
        int64_t end = 0;
        float f0 = 0.0f, conf = 0.0f, purity = 0.0f;
    };

    void designRange (double fc)
    {
        rangeApplied_ = (float) fc;
        Svf s;
        setSvf (s, fc, sr_, 1.4142135623730951);
        lpA_.s = lpK_.s = lpD_[0].s = lpD_[1].s = s;
        // group delay of two cascaded 2nd-order Butterworth stages, as a fractional read position (it glides with Range)
        const double adv = std::min ((double) (lat_ - kSub - 2), 0.45 / fc * sr_);
        advI_ = (int) std::floor (adv);
        advFrac_ = adv - advI_;
        adv_ = (int) std::lround (adv);
    }

    // ---------------- analysis path ------------------------------------------------------
    void analyse (double a, double kickIn)
    {
        const double lo = lpA_.process (a);
        const double kl = lpK_.process (kickIn);
        pk_ += af_ * (kl * kl - pk_);
        const double p = lo * lo;
        pf_ += af_ * (p - pf_);
        ps_ += as_ * (p - ps_);
        pm_ += am_ * (p - pm_);
        pl_ += al_ * (p - pl_);
        if (nIn_ % D_ == 0)
        {
            din_[(size_t) (m_ & (kRing - 1))] = (float) lo;
            dk_[(size_t) (m_ & (kRing - 1))] = (float) kl;
            ++m_;
            if (m_ % hopD_ == 0 && m_ >= N_)
                frame();
            correlate();
            if (m_ % hopS_ == 0 && m_ >= Ns_ + 1)
                spectralFrame (m_ - 1);
        }
    }

    // Share of the window's energy that sits on f0 (exactly K periods, so nothing leaks).
    bool purityAt (const std::vector<float>& ring, int64_t last, float f0, int maxL, double& purity) const
    {
        const double period = fsd_ / f0;
        int K = std::max (2, (int) std::ceil (0.05 * f0));
        K = std::min (K, (int) std::floor (maxL / period));
        if (K < 1)
            return false;
        const int L = (int) std::lround (K * period);
        if (L < 4 || last - L + 1 < 0)
            return false;
        const double w = 2.0 * kPi * K / L, c = std::cos (w), s = std::sin (w);
        double pr = 1.0, pi = 0.0, re = 0.0, im = 0.0, e2 = 0.0;
        for (int j = 0; j < L; ++j)
        {
            const double x = ring[(size_t) ((last - (L - 1) + j) & (kRing - 1))];
            re += x * pr;
            im -= x * pi;
            e2 += x * x;
            const double nr = pr * c - pi * s;
            pi = pr * s + pi * c;
            pr = nr;
        }
        if (e2 / L < 1e-10)
            return false;
        const double amp = 2.0 / L * std::sqrt (re * re + im * im);
        purity = std::min (1.0, 0.5 * amp * amp / (e2 / L));
        return true;
    }

    void frame()
    {
        const int64_t last = m_ - 1;
        double mean = 0.0;
        for (int j = 0; j < N_; ++j)
        {
            win_[(size_t) j] = din_[(size_t) ((last - (N_ - 1) + j) & (kRing - 1))];
            mean += win_[(size_t) j];
        }
        mean /= N_;
        double e2 = 0.0;
        for (int j = 0; j < N_; ++j)
        {
            win_[(size_t) j] -= (float) mean;
            e2 += (double) win_[(size_t) j] * win_[(size_t) j];
        }
        Frame fr;
        fr.end = last * D_;
        if (std::sqrt (e2 / N_) > 1e-4)
        {
            const int Wd = tauMax_;
            diff_[0] = 0;
            for (int tau = 1; tau <= tauMax_; ++tau)
            {
                double s = 0;
                for (int j = 0; j < Wd; ++j)
                {
                    const float d = win_[(size_t) j] - win_[(size_t) (j + tau)];
                    s += (double) d * d;
                }
                diff_[(size_t) tau] = (float) s;
            }
            double run = 0;
            cmnd_[0] = 1.0f;
            for (int tau = 1; tau <= tauMax_; ++tau)
            {
                run += diff_[(size_t) tau];
                cmnd_[(size_t) tau] = run > 0 ? (float) (diff_[(size_t) tau] * tau / run) : 1.0f;
            }
            int best = -1;
            for (int tau = tauMin_; tau < tauMax_; ++tau)
                if (cmnd_[(size_t) tau] < 0.20f)
                {
                    while (tau + 1 < tauMax_ && cmnd_[(size_t) tau + 1] < cmnd_[(size_t) tau])
                        ++tau;
                    best = tau;
                    break;
                }
            if (best < 0)
            {
                float lo = 1e9f;
                for (int tau = tauMin_; tau < tauMax_; ++tau)
                    if (cmnd_[(size_t) tau] < lo)
                    {
                        lo = cmnd_[(size_t) tau];
                        best = tau;
                    }
                if (lo > 0.45f)
                    best = -1;
            }
            if (best > 0)
            {
                float tau = (float) best;
                const float a = cmnd_[(size_t) best - 1], b = cmnd_[(size_t) best], c = cmnd_[(size_t) best + 1];
                const float den = a - 2.0f * b + c;
                if (std::fabs (den) > 1e-9f)
                    tau += 0.5f * (a - c) / den;
                fr.f0 = (float) fsd_ / tau;
                fr.conf = std::max (0.0f, 1.0f - cmnd_[(size_t) best]);
                double p = 0.0;
                if (purityAt (din_, last, fr.f0, N_, p))
                    fr.purity = (float) p;
            }
        }
        frames_[(size_t) (frameCount_ & (kFrames - 1))] = fr;
        ++frameCount_;
    }

    // The note under output time t. A window that ends at t sees only the past, one that starts at t only
    // the future; near a note change one of them is mixed, and an onset between them says which one is pure.
    bool pickPitch (int64_t t, float& f0, float& purity) const
    {
        if (frameCount_ == 0)
            return false;
        const int64_t latest = frameCount_ - 1, oldest = std::max<int64_t> (0, frameCount_ - kFrames + 1);
        const Frame& L = frames_[(size_t) (latest & (kFrames - 1))];
        const Frame *B = nullptr, *F = nullptr;
        if (t <= L.end)
        {
            const int64_t k = latest - (L.end - t + hopIn_ - 1) / hopIn_;
            if (k >= oldest)
                B = &frames_[(size_t) (k & (kFrames - 1))];
        }
        if (t + W_ <= L.end)
        {
            const int64_t k = latest - (L.end - (t + W_)) / hopIn_;
            if (k >= oldest)
                F = &frames_[(size_t) (k & (kFrames - 1))];
        }
        // a pitch that has no energy at its own frequency is a trick of the tracker (a chord's common period), not a note
        auto voiced = [] (const Frame* f) { return f != nullptr && f->f0 > 0.0f && f->conf >= 0.75f && f->purity >= 0.03f; };
        const Frame* use = nullptr;
        if (voiced (B) && voiced (F))
        {
            const double st = std::fabs (12.0 * std::log2 ((double) F->f0 / B->f0));
            if (st < 0.7)
                use = F->conf >= B->conf ? F : B;
            else
            {
                int64_t best = 0;
                bool found = false;
                for (int i = 0; i < onsetN_ && i < (int) onsets_.size(); ++i)
                {
                    const int64_t o = onsets_[(size_t) i];
                    if (o >= t - W_ && o <= t + W_ && (! found || std::llabs (o - t) < std::llabs (best - t)))
                    {
                        best = o;
                        found = true;
                    }
                }
                if (found)
                    use = best <= t ? F : B;
                else if (std::fabs (F->conf - B->conf) > 0.08f)
                    use = F->conf > B->conf ? F : B;
            }
        }
        else if (voiced (B) && B->conf >= 0.88f) // a lone window has to be very sure: noise is rarely periodic twice
            use = B;
        else if (voiced (F) && F->conf >= 0.88f)
            use = F;
        if (! use)
            return false;
        f0 = use->f0;
        purity = use->purity;
        return true;
    }

    // ---------------- kick: spectral duck, alignment ---------------------------------------
    // Cross-correlation of bass and kick over +-2.5 ms, gathered only while both are playing.
    void correlate()
    {
        const int L = kLag;
        const int64_t m0 = m_ - 1 - L;
        if (m0 < L + 1)
            return;
        // no kick for a long time (sidechain removed, or a quiet section): drop what was learnt, polarity included
        if (10.0 * std::log10 (pk_ + 1e-12) > -60.0)
            kickQuiet_ = 0;
        else if (++kickQuiet_ > (int64_t) (20.0 * fsd_))
        {
            cc_.fill (0.0);
            ebb_ = ekk_ = activeSec_ = 0.0;
            alignDb_ = alignLagMs_ = alignRho_ = 0.0f;
            selfSc_ = false;
        }
        const bool active = 10.0 * std::log10 (pf_ + 1e-12) > -45.0 && 10.0 * std::log10 (pk_ + 1e-12) > -40.0;
        if (! active)
            return;
        const double forget = 1.0 - 1.0 / (6.0 * fsd_);
        const double kv = dk_[(size_t) (m0 & (kRing - 1))];
        const double bv = din_[(size_t) (m0 & (kRing - 1))];
        for (int l = -L; l <= L; ++l)
            cc_[(size_t) (l + L)] = cc_[(size_t) (l + L)] * forget + (double) din_[(size_t) ((m0 - l) & (kRing - 1))] * kv;
        ebb_ = ebb_ * forget + bv * bv;
        ekk_ = ekk_ * forget + kv * kv;
        activeSec_ = std::min (60.0, activeSec_ + 1.0 / fsd_);
    }

    void spectralFrame (int64_t last)
    {
        const int N = Fft512::N, Ns = Ns_;
        static thread_local double br[Fft512::N], bi[Fft512::N], kr[Fft512::N], ki[Fft512::N];
        for (int j = 0; j < N; ++j)
        {
            if (j < Ns)
            {
                const size_t idx = (size_t) ((last - (Ns - 1) + j) & (kRing - 1));
                br[j] = din_[idx] * sw_[(size_t) j];
                kr[j] = dk_[idx] * sw_[(size_t) j];
            }
            else
                br[j] = kr[j] = 0.0;
            bi[j] = ki[j] = 0.0;
        }
        fft_.run (br, bi, false);
        fft_.run (kr, ki, false);

        // the note in this same window: the newest pitch frame covers it
        float f0 = 0.0f;
        if (frameCount_ > 0)
        {
            const Frame& F = frames_[(size_t) ((frameCount_ - 1) & (kFrames - 1))];
            if (F.f0 > 0.0f && F.conf >= 0.75f)
                f0 = F.f0;
        }

        double raw[Fft512::N / 2 + 2], tgt[Fft512::N / 2 + 2];
        const int kMax = kMax_;
        for (int k = 0; k <= kMax + 1; ++k)
        {
            const double pb = br[k] * br[k] + bi[k] * bi[k], pk = kr[k] * kr[k] + ki[k] * ki[k];
            raw[k] = pk / (pb + pk + 1e-12);
        }
        // a kick that is barely there (bleed, noise in the sidechain) is no reason to move anything
        const double kickLevelDb = 10.0 * std::log10 (pk_ + 1e-12);
        const double gate = std::clamp ((kickLevelDb + 72.0) / 10.0, 0.0, 1.0);
        const double depth = selfSc_ ? 0.0 : 12.0 * std::clamp ((double) prm_.kick, 0.0, 1.0) * gate;
        const double resHz = fsd_ / Ns_; // one resolution cell of the window: a gain feature narrower than this smears onto its neighbours
        double deepest = 0.0;
        // Masking mode: which bass components cover up the kick? Excitation of each analysis bin through the auditory filter.
        double need[Fft512::N / 2 + 2];
        for (int k = 0; k <= kMax + 1; ++k)
            need[k] = 0.0;
        if (prm_.kickMode == 1)
        {
            const int nb = kMax + 2;
            double pbv[Fft512::N / 2 + 2], pkv[Fft512::N / 2 + 2], kpeak = 0.0, sumB = 0.0, logB = 0.0;
            for (int k = 0; k < nb; ++k)
            {
                pbv[k] = br[k] * br[k] + bi[k] * bi[k];
                pkv[k] = kr[k] * kr[k] + ki[k] * ki[k];
                kpeak = std::max (kpeak, pkv[k]);
                if (k >= 1 && k <= kMax)
                {
                    sumB += pbv[k];
                    logB += std::log (pbv[k] + 1e-9);
                }
            }
            // a tonal masker covers up less than a noisy one: tone-masking-noise offset 14 dB, noise-masking-noise 5 dB
            const double sfm = std::exp (logB / kMax) / (sumB / kMax + 1e-9);
            const double offsetDb = 5.0 + 9.0 * (1.0 - std::clamp (sfm, 0.0, 1.0));
            const double a = std::pow (10.0, -offsetDb / 10.0);
            for (int j = 1; j <= kMax; ++j)
            {
                double eb = 0.0, ek = 0.0;
                const float* w = &wm_[(size_t) j * (size_t) nb];
                for (int i = 1; i <= kMax; ++i)
                {
                    eb += pbv[i] * w[i];
                    ek += pkv[i] * w[i];
                }
                // only where the kick is really there, and only as much as is needed to uncover it (plus 3 dB)
                const double sig = std::clamp ((10.0 * std::log10 (ek / (kpeak + 1e-12) + 1e-12) + 22.0) / 12.0, 0.0, 1.0);
                const double red = 10.0 * std::log10 ((eb * a + 1e-12) / (ek + 1e-12)) + 3.0;
                need[j] = std::clamp (red, 0.0, depth) * sig * sig * (3.0 - 2.0 * sig) * eq_[(size_t) j];
            }
        }
        for (int k = 1; k <= kMax; ++k)
        {
            double duckDb;
            if (prm_.kickMode == 1)
                duckDb = need[k]; // already smooth: it comes through the auditory filters
            else
            {
                const double m = 0.25 * raw[k - 1] + 0.5 * raw[k] + 0.25 * raw[k + 1]; // a little smoothing across frequency
                const double t = std::clamp ((m - 0.30) / 0.55, 0.0, 1.0);
                duckDb = depth * t * t * (3.0 - 2.0 * t);
            }
            double prot = 0.0;
            if (f0 > 0.0f)
                for (int h = 1; h <= 5; ++h)
                {
                    // flat inside about 0.6 of a resolution cell (the core of the note's own main lobe), then falling away
                    const double over = std::max (0.0, std::fabs (k * binHz_ - h * f0) - 0.6 * resHz) / (0.5 * resHz);
                    prot = std::max (prot, std::exp (-over * over));
                }
            tgt[k] = -duckDb * (1.0 - 0.95 * prot);
        }
        // in time: duck quickly, let go over ~45 ms
        const double rel = 1.0 - std::exp (-(double) hopS_ / (0.045 * fsd_));
        for (int k = 1; k <= kMax; ++k)
            gKick_[(size_t) k] += (tgt[k] < gKick_[(size_t) k] ? 0.7 : rel) * (tgt[k] - gKick_[(size_t) k]);
        static thread_local double ds_r[Fft512::N], ds_i[Fft512::N];
        for (int j = 0; j < N; ++j)
            ds_r[j] = ds_i[j] = 0.0;
        for (int k = 1; k <= kMax; ++k)
        {
            const double gdb = 0.25 * gKick_[(size_t) std::max (1, k - 1)] + 0.5 * gKick_[(size_t) k] + 0.25 * gKick_[(size_t) std::min (kMax, k + 1)];
            deepest = std::max (deepest, -gdb);
            const double g = std::pow (10.0, gdb / 20.0) - 1.0;
            ds_r[k] = g * br[k];
            ds_i[k] = g * bi[k];
            ds_r[N - k] = ds_r[k];
            ds_i[N - k] = -ds_i[k];
        }
        kickDuckDb_ += 0.5f * ((float) deepest - kickDuckDb_);
        fft_.run (ds_r, ds_i, true);
        // overlap-add; the newest hop of samples starts from silence
        for (int j = Ns - hopS_; j < Ns; ++j)
            dd_[(size_t) ((last - (Ns - 1) + j) & (kRing - 1))] = 0.0f;
        for (int j = 0; j < Ns; ++j)
            dd_[(size_t) ((last - (Ns - 1) + j) & (kRing - 1))] += (float) (ds_r[j] * sw_[(size_t) j] * olaNorm_);
        lastFrameEnd_ = last;
    }

    // The difference signal for the output sample leaving the delay now (cubic interpolation of the decimated one).
    double kickDelta()
    {
        if (lastFrameEnd_ < 0)
            return 0.0;
        const int64_t tOut = nIn_ - lat_ + adv_; // the analysis low band lags the audio by the filter's group delay
        if (tOut < 0)
            return 0.0;
        const int64_t j = tOut / D_;
        const double f = (double) (tOut - j * D_) / D_;
        // sample j is final once no later frame covers it: the next frame starts hopS_ after the newest one
        if (j - 1 < 0 || j + 2 > lastFrameEnd_ - Ns_ + hopS_)
        {
            if (nIn_ > (int64_t) lat_ + 3 * (int64_t) Ns_ * D_)
                ++deltaGaps_; // would be a bug: the look-ahead is too short for the spectral stage
            return 0.0;
        }
        const double p0 = dd_[(size_t) ((j - 1) & (kRing - 1))], p1 = dd_[(size_t) (j & (kRing - 1))], p2 = dd_[(size_t) ((j + 1) & (kRing - 1))],
                     p3 = dd_[(size_t) ((j + 2) & (kRing - 1))];
        return p1 + 0.5 * f * (p2 - p0 + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
    }

    void updateAlign()
    {
        if (ebb_ > 1e-12 && ekk_ > 1e-12)
        {
            const double cross = cc_[(size_t) kLag], sum = ebb_ + ekk_;
            alignDb_ = (float) (10.0 * std::log10 ((sum + 2.0 * cross + 1e-12) / (sum - 2.0 * cross + 1e-12)));
            int best = kLag;
            double bestV = 0.0;
            for (int l = -kLag; l <= kLag; ++l)
                if (std::fabs (cc_[(size_t) (l + kLag)]) > bestV)
                {
                    bestV = std::fabs (cc_[(size_t) (l + kLag)]);
                    best = l;
                }
            alignLagMs_ = (float) (1000.0 * (best) / fsd_);
            alignRho_ = (float) (cc_[(size_t) (best + kLag)] / std::sqrt (ebb_ * ekk_));
            // the same signal on both inputs (e.g. the bass track picked as its own sidechain): there is no kick to make room for
            const double ratioDb = 10.0 * std::log10 (ebb_ / ekk_);
            selfSc_ = activeSec_ > 0.3 && alignRho_ > 0.98f && std::fabs (ratioDb) < 3.0 && std::abs (best) <= 1;
        }
        else
            selfSc_ = false;
        if (prm_.align == 1 && alignKnown())
        {
            if (alignDb_ < -1.0f)
                polTarget_ = -1.0;
            else if (alignDb_ > -0.3f)
                polTarget_ = 1.0;
        }
        else
            polTarget_ = 1.0;
        polSm_ += (1.0 - std::exp (-(double) kSub / (0.020 * sr_))) * (polTarget_ - polSm_);
        if (std::fabs (polSm_ - polTarget_) < 1e-4)
            polSm_ = polTarget_;
    }

    // ---------------- control ------------------------------------------------------------
    void updateControl()
    {
        const int64_t blk = nIn_ / kSub;
        // Range moves the crossover and the read position of the low band: glide to a new value, never jump
        {
            const double target = std::clamp ((double) prm_.rangeHz, 60.0, 300.0);
            if (rangeSm_ <= 0.0)
                rangeSm_ = target;
            rangeSm_ += (1.0 - std::exp (-(double) kSub / (0.040 * sr_))) * (target - rangeSm_);
            if (std::fabs (rangeSm_ - target) < 0.02)
                rangeSm_ = target;
            if ((float) rangeSm_ != rangeApplied_)
                designRange (rangeSm_);
        }
        updateAlign();

        // fast-vs-slow envelope of the low band: positive while the level is rising, negative while it falls
        const double lvl = 10.0 * std::log10 (ps_ + 1e-12);
        const double wgt = std::clamp ((lvl + 72.0) / 12.0, 0.0, 1.0);
        const double d = 10.0 * std::log10 ((pf_ + 1e-12) / (ps_ + 1e-12)) * wgt;
        // Attacks: the detector fires a few ms after the real onset (its envelope has to rise), and the delay line gives us
        // time to place a smooth gain pulse where the attack really is. Strength follows how hard the level jumped.
        if (d > 4.5)
        {
            if (! inOnset_ && nIn_ - lastOnsetT_ > (int64_t) (0.04 * sr_))
            {
                lastOnsetT_ = nIn_;
                onsets_[(size_t) (onsetHead_ & 31)] = nIn_ - (int64_t) (0.0065 * sr_);
                onsetAmp_[(size_t) (onsetHead_ & 31)] = 0.0f;
                ++onsetHead_;
                onsetN_ = std::min (onsetN_ + 1, 32);
                dPeak_ = 0.0;
                // the last note's body gain fades out into this attack instead of reaching into it
                const int64_t bs = std::max<int64_t> (0, (onsets_[(size_t) ((onsetHead_ - 1) & 31)] - (int64_t) (0.003 * sr_)) / kSub);
                if (bs < blk)
                {
                    const float gv = gHist_[(size_t) (bs & (kGHist - 1))];
                    for (int64_t b = bs; b < blk; ++b)
                        gHist_[(size_t) (b & (kGHist - 1))] = gv * (1.0f - (float) (b - bs) / (float) (blk - bs));
                }
                gs_ = 0.0;
            }
            inOnset_ = true;
        }
        if (inOnset_)
        {
            dPeak_ = std::max (dPeak_, d);
            onsetAmp_[(size_t) ((onsetHead_ - 1) & 31)] = (float) std::clamp (dPeak_ / 6.0, 0.4, 1.0);
            if (d < 2.0)
                inOnset_ = false;
        }
        // Body: a slower pair (ripple of the low band's rectified level stays below 0.2 dB) reads how fast the note is dying
        const double wl = std::clamp ((10.0 * std::log10 (pl_ + 1e-12) + 72.0) / 12.0, 0.0, 1.0);
                const double decRaw = std::max (-10.0 * std::log10 ((pm_ + 1e-12) / (pl_ + 1e-12)) * wl, 0.0);
        // a natural ring-out falls at about decRaw 0.3-1.5; anything much faster is a mute or a tremolo/wobble, not a decay, and gets less
        const double dec = decRaw / (1.0 + (decRaw / 1.5) * (decRaw / 1.5));
        const double wf = std::clamp ((10.0 * std::log10 (pf_ + 1e-12) + 62.0) / 8.0, 0.0, 1.0); // note over: let go
        const double sinceOnset = (double) (nIn_ - lastOnsetT_) / sr_;
        const double wb = std::clamp ((sinceOnset - 0.03) / 0.03, 0.0, 1.0); // the body starts after the attack
        const double rawSus = inOnset_ ? 0.0 : (double) prm_.sustain * kS * dec * wf * wb;
        gs_ += (1.0 - std::exp (-(double) kSub / (0.006 * sr_))) * (rawSus - gs_);
        gHist_[(size_t) (blk & (kGHist - 1))] = (float) gs_;

        // the transient gain that belongs to the audio now leaving the delay (detected antBlk_ blocks later)
        double gT = 0.0;
        const int64_t latBlk = lat_ / kSub, idx = blk - latBlk + antBlk_;
        if (nIn_ >= lat_ && idx >= 0)
        {
            const int64_t tOut = nIn_ - lat_ + kSub / 2;
            double pulse = 0.0;
            if (prm_.punch != 0.0f)
                for (int i = 0; i < onsetN_; ++i)
                {
                    const double u = (double) (tOut - onsets_[(size_t) i]) / sr_;
                    if (u > 0.0 && u < 0.2)
                        pulse += (double) onsetAmp_[(size_t) i] * (std::exp (-u / 0.028) - std::exp (-u / 0.003)) / 0.683;
                }
            gT = kCeil * std::tanh ((kPunchDb * prm_.punch * pulse + gHist_[(size_t) (idx & (kGHist - 1))]) / kCeil);
        }
        gOut_ += (1.0 - std::exp (-(double) kSub / (0.0015 * sr_))) * (gT - gOut_);
        gT = gOut_;
        transDb_ = (float) gT;
        const double gNew = std::pow (10.0, (gT + matchDb_) / 20.0);
        gStep_ = (gNew - gAmp_) / kSub;

        // pitch-aware contrast
        float f0 = 0.0f, pur = 0.0f;
        const bool voiced = nIn_ >= lat_ && pickPitch (nIn_ - lat_, f0, pur);
        double scaleT = 0.0;
        if (voiced)
        {
            if (pFresh_)
                pS_ = pur, pFresh_ = false; // start from this note's own value, not from the last one
            pS_ += (1.0 - std::exp (-(double) kSub / (0.03 * sr_))) * ((double) pur - pS_);
            const double st = freq_ > 0.0 ? std::fabs (12.0 * std::log2 ((double) f0 / freq_)) : 99.0;
            if (freq_ <= 0.0 || (st > 0.7 && scale_ < 0.02))
                freq_ = f0, scaleT = 1.0;
            else if (st <= 0.7)
            {
                freq_ = std::exp (std::log (freq_) + (1.0 - std::exp (-(double) kSub / (0.010 * sr_))) * (std::log ((double) f0) - std::log (freq_)));
                scaleT = 1.0;
            }
            pitchOut_ = f0;
        }
        else
        {
            pitchOut_ = 0.0f;
            pFresh_ = true;
        }
        const double sa = scaleT > scale_ ? 0.008 : 0.004;
        scale_ += (1.0 - std::exp (-(double) kSub / (sa * sr_))) * (scaleT - scale_);
        if (scale_ < 1e-4 && scaleT == 0.0)
            scale_ = 0.0;
        // the knob can move in steps (a host's automation, a click): ease it, so the bells never jump
        cSm_ += (1.0 - std::exp (-(double) kSub / (0.025 * sr_))) * ((double) prm_.contrast - cSm_);
        if (std::fabs (cSm_ - (double) prm_.contrast) < 1e-5)
            cSm_ = prm_.contrast;
        const double c = cSm_;
        double ce = c;
        if (c > 0.0)
        {
            const double t = std::clamp ((pS_ - 0.80) / 0.18, 0.0, 1.0);
            ce = c * (1.0 - t * t * (3.0 - 2.0 * t)); // already-pure notes get less of the boost
        }
        gain1Db_ = ce * 6.0 * scale_;
        const double gain2Db = -ce * 4.0 * scale_;
        // loudness match: the fundamental's share of the band (pS_) gets bell 1, the rest partly gets bell 2
        double comp = 0.0;
        if (prm_.match && freq_ > 0.0 && (std::fabs (gain1Db_) > 1e-3 || std::fabs (gain2Db) > 1e-3))
        {
            const double r = pS_ * std::pow (10.0, gain1Db_ / 10.0) + (1.0 - pS_) * (0.5 * std::pow (10.0, gain2Db / 10.0) + 0.5);
            comp = -0.75 * 10.0 * std::log10 (std::max (r, 0.05)); // most of the way: the tone keeps some of its energy change
        }
        matchDb_ += (1.0 - std::exp (-(double) kSub / (0.025 * sr_))) * (comp - matchDb_);
        if (std::fabs (matchDb_ - comp) < 1e-4)
            matchDb_ = comp;
        if (freq_ > 0.0)
        {
            const double A1 = std::pow (10.0, gain1Db_ / 40.0), A2 = std::pow (10.0, gain2Db / 40.0), q1 = 3.0, q2 = 4.0;
            setSvf (s1_, freq_, sr_, 1.0 / (q1 * A1));
            setSvf (s2_, 1.5 * freq_, sr_, 1.0 / (q2 * A2));
            m1_ = (A1 - 1.0 / A1) / q1;
            m2_ = (A2 - 1.0 / A2) / q2;
        }
        else
            m1_ = m2_ = 0.0;

        if (blk % 64 == 0 && voiced)
            updateMeter (f0);
    }

    void updateMeter (float f0)
    {
        const int64_t mOut = m_ - 1, mIn = mOut - lat_ / D_;
        double pin = 0.0, pout = 0.0;
        if (mIn > 0 && purityAt (din_, mIn, f0, 1200, pin) && purityAt (dout_, mOut, f0, 1200, pout))
        {
            if (! meterValid_)
            {
                pInSm_ = (float) pin;
                pOutSm_ = (float) pout;
                meterValid_ = true;
            }
            pInSm_ += 0.2f * ((float) pin - pInSm_);
            pOutSm_ += 0.2f * ((float) pout - pOutSm_);
        }
    }

    Params prm_;
    double sr_ = 48000.0, fsd_ = 4800.0;
    int adv_ = 0, advI_ = 0, D_ = 10, tauMax_ = 170, tauMin_ = 18, N_ = 340, hopD_ = 10, lat_ = 4800, dsize_ = 8192, antBlk_ = 12;
    int64_t hopIn_ = 100, W_ = 3400;
    std::vector<float> dl_[2], din_, dout_, gHist_, win_, diff_, cmnd_;
    Lr4 lpA_, lpK_, lpD_[2];
    static constexpr int kLag = 14;
    Fft512 fft_;
    int Ns_ = 384, hopS_ = 48, kMax_ = 26;
    double olaNorm_ = 0.25, binHz_ = 10.4, pk_ = 0.0, ebb_ = 0.0, ekk_ = 0.0, activeSec_ = 0.0, polTarget_ = 1.0, polSm_ = 1.0;
    int64_t lastFrameEnd_ = -1, deltaGaps_ = 0, kickQuiet_ = 0;
    bool selfSc_ = false;
    int krFrames_ = 0;
    std::vector<double> sw_;
    std::vector<float> dk_, dd_, wm_, eq_;
    std::array<double, Fft512::N / 2 + 2> gKick_ {};
    std::array<double, 2 * kLag + 1> cc_ {};
    float kickDuckDb_ = 0.0f, alignDb_ = 0.0f, alignLagMs_ = 0.0f, alignRho_ = 0.0f;
    St bz_[2][2];
    Svf s1_, s2_;
    double m1_ = 0.0, m2_ = 0.0;
    int64_t nIn_ = 0, m_ = 0;
    double pf_ = 0.0, ps_ = 0.0, pm_ = 0.0, pl_ = 0.0, af_ = 0.01, as_ = 0.001, am_ = 0.002, al_ = 0.0005, gs_ = 0.0, gOut_ = 0.0, gAmp_ = 1.0, gStep_ = 0.0;
    float rangeApplied_ = -1.0f;
    std::array<Frame, kFrames> frames_;
    int64_t frameCount_ = 0;
    std::array<int64_t, 32> onsets_;
    std::array<float, 32> onsetAmp_ {};
    double dPeak_ = 0.0;
    int onsetN_ = 0, onsetHead_ = 0;
    bool inOnset_ = false, pFresh_ = true;
    int64_t lastOnsetT_ = -1000000;
    double advFrac_ = 0.0, rangeSm_ = 0.0, freq_ = 0.0, scale_ = 0.0, pS_ = 0.0, gain1Db_ = 0.0, matchDb_ = 0.0, cSm_ = 0.0;
    float pitchOut_ = 0.0f, transDb_ = 0.0f, pInSm_ = 0.0f, pOutSm_ = 0.0f;
    bool meterValid_ = false;
};
} // namespace led
