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
// All state advances sample by sample and the control logic runs every kSub samples, so the output does not
// depend on the host's block size.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
        lat_ = (int) (W_ + hopIn_ + 64);
        lat_ = ((lat_ + kSub - 1) / kSub) * kSub;
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
        gain1Db_ = matchDb_ = 0.0;
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
    float pitchHz() const { return pitchOut_; }
    bool meterValid() const { return meterValid_; }
    float definitionIn() const { return pInSm_; }   // 0..1
    float definitionOut() const { return pOutSm_; } // 0..1

    void process (float* const* ch, int nCh, int n)
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
            analyse (0.5 * (x[0] + x[1]));

            const int wr = (int) (nIn_ & mask);
            double lowMono = 0.0;
            gAmp_ += gStep_;
            for (int c = 0; c < nCh; ++c)
            {
                dl_[c][(size_t) wr] = (float) x[c];
                const double d = dl_[c][(size_t) ((wr - lat_) & mask)];
                // the low band is read advanced by the filter's group delay, so it lines up with d in time
                const double low = lpD_[c].process (dl_[c][(size_t) ((wr - lat_ + adv_) & mask)]);
                double y = bell (s1_, bz_[c][0], m1_, low);
                y = bell (s2_, bz_[c][1], m2_, y) * gAmp_;
                lowMono += y;
                ch[c][i] = (float) ((d - low) + y);
            }
            if (nIn_ % D_ == 0)
                dout_[(size_t) ((m_ - 1) & (kRing - 1))] = (float) (lowMono / nCh);
            ++nIn_;
        }
    }

private:
    static constexpr int kRing = 4096, kGHist = 4096, kFrames = 1024;
    static constexpr double kPunchDb = 10.0, kS = 5.0, kCeil = 12.0;

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

    void designRange()
    {
        rangeApplied_ = prm_.rangeHz;
        const double fc = std::clamp ((double) prm_.rangeHz, 60.0, 300.0);
        Svf s;
        setSvf (s, fc, sr_, 1.4142135623730951);
        lpA_.s = lpD_[0].s = lpD_[1].s = s;
        adv_ = std::min (lat_ - kSub, (int) std::lround (0.45 / fc * sr_)); // group delay of two cascaded 2nd-order Butterworth stages
    }

    // ---------------- analysis path ------------------------------------------------------
    void analyse (double a)
    {
        const double lo = lpA_.process (a);
        const double p = lo * lo;
        pf_ += af_ * (p - pf_);
        ps_ += as_ * (p - ps_);
        pm_ += am_ * (p - pm_);
        pl_ += al_ * (p - pl_);
        if (nIn_ % D_ == 0)
        {
            din_[(size_t) (m_ & (kRing - 1))] = (float) lo;
            ++m_;
            if (m_ % hopD_ == 0 && m_ >= N_)
                frame();
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
        auto voiced = [] (const Frame* f) { return f != nullptr && f->f0 > 0.0f && f->conf >= 0.75f; };
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

    // ---------------- control ------------------------------------------------------------
    void updateControl()
    {
        const int64_t blk = nIn_ / kSub;
        if (prm_.rangeHz != rangeApplied_)
            designRange();

        // fast-vs-slow envelope of the low band: positive while the level is rising, negative while it falls
        const double lvl = 10.0 * std::log10 (ps_ + 1e-12);
        const double wgt = std::clamp ((lvl + 72.0) / 12.0, 0.0, 1.0);
        const double d = 10.0 * std::log10 ((pf_ + 1e-12) / (ps_ + 1e-12)) * wgt;
        // Attacks: the detector fires a few ms after the real onset (its envelope has to rise), and the delay line gives us
        // time to place a smooth gain pulse where the attack really is. Strength follows how hard the level jumped.
        if (d > 3.0)
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
            if (d < 1.5)
                inOnset_ = false;
        }
        // Body: a slower pair (ripple of the low band's rectified level stays below 0.2 dB) reads how fast the note is dying
        const double wl = std::clamp ((10.0 * std::log10 (pl_ + 1e-12) + 72.0) / 12.0, 0.0, 1.0);
        // a muted note-off dies far faster than any ring-out: soft-limit it so "sustain" does not pump the end of notes
        const double decRaw = std::max (-10.0 * std::log10 ((pm_ + 1e-12) / (pl_ + 1e-12)) * wl, 0.0);
        const double dec = 2.5 * std::tanh (decRaw / 2.5);
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
        const double c = prm_.contrast;
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
    int adv_ = 0, D_ = 10, tauMax_ = 170, tauMin_ = 18, N_ = 340, hopD_ = 10, lat_ = 4800, dsize_ = 8192, antBlk_ = 12;
    int64_t hopIn_ = 100, W_ = 3400;
    std::vector<float> dl_[2], din_, dout_, gHist_, win_, diff_, cmnd_;
    Lr4 lpA_, lpD_[2];
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
    double freq_ = 0.0, scale_ = 0.0, pS_ = 0.0, gain1Db_ = 0.0, matchDb_ = 0.0;
    float pitchOut_ = 0.0f, transDb_ = 0.0f, pInSm_ = 0.0f, pOutSm_ = 0.0f;
    bool meterValid_ = false;
};
} // namespace led
