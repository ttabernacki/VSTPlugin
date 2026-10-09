#pragma once

// Note Space: DSP core (no JUCE).
//
// The bass is split, sample by sample, into
//   * partials: harmonics 1..8 of the tracked note, each as a slowly varying complex envelope c_h(t) times e^{j h theta(t)},
//     where theta is the phase of the note (the integral of its pitch). c_h is found by heterodyning the input with
//     e^{-j h theta} and averaging over exactly two periods, twice (a triangle over four periods): an average over whole
//     periods cancels every other harmonic exactly, and anything between harmonics (mud) falls in the stopband, so each
//     partial comes out on its own even at 30 Hz.
//   * residual: input minus the partials. Room boom, mud between the harmonics, string and pick noise, rumble.
// With every control at neutral the output is partials + residual = the input (to rounding), so nothing can be lost.
//
// Controls act on that split:
//   Contrast    residual below Range down (+) or up (-), the note untouched; attacks are protected.
//   Tone lock   each harmonic's share of the note pulled toward its long-term average: every note gets the same timbre.
//   Fundamental the fundamental alone, up or down.
//   Repair      the fundamental replaced by its slow part (two 60 ms poles on its complex envelope): beating and wobble
//               rotate against the note and average out, so what is left is a steady sine locked to the note.
//   Translate   harmonics 2-4 lifted (or generated, phase-locked to the fundamental) to a floor below the fundamental,
//               so the note keeps its pitch on small speakers.
// Processing fades out where the split cannot be trusted: no pitch, a pitch change in the averaging window.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace nsp
{
constexpr double kPi = 3.14159265358979323846;

struct Params
{
    float contrast = 0.4f;      // -1..1  residual +6 dB .. -12 dB
    float toneLock = 0.0f;      // 0..1
    float fundamentalDb = 0.0f; // -6..+6
    float repair = 0.0f;        // 0..1
    float translate = 0.0f;     // 0..1
    float rangeHz = 300.0f;     // residual cleanup acts below this
};

struct Cx
{
    double re = 0.0, im = 0.0;
};
inline Cx operator+ (Cx a, Cx b) { return { a.re + b.re, a.im + b.im }; }
inline Cx operator- (Cx a, Cx b) { return { a.re - b.re, a.im - b.im }; }
inline Cx operator* (Cx a, Cx b) { return { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; }
inline Cx operator* (Cx a, double s) { return { a.re * s, a.im * s }; }
inline double mag (Cx a) { return std::sqrt (a.re * a.re + a.im * a.im); }

class NoteSpace
{
public:
    static constexpr int kH = 8, kSub = 16;

    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        // pitch tracker on a decimated, low-passed mono copy (as in Low-End Definition)
        D_ = std::max (1, (int) std::floor (sr_ / 5000.0));
        fsd_ = sr_ / D_;
        tauMax_ = (int) std::ceil (fsd_ / 30.0) + 2;
        tauMin_ = std::max (4, (int) std::floor (fsd_ / 260.0));
        N_ = 2 * tauMax_;
        hopD_ = std::max (4, (int) std::lround (0.004 * fsd_));
        hopIn_ = (int64_t) hopD_ * D_;
        W_ = (int64_t) N_ * D_;
        win_.assign ((size_t) N_, 0.0f);
        diff_.assign ((size_t) tauMax_ + 2, 0.0f);
        cmnd_.assign ((size_t) tauMax_ + 2, 0.0f);
        din_.assign (kRing, 0.0f);
        setLp (lpA_, 400.0);
        af_ = 1.0 - std::exp (-1.0 / (0.008 * sr_));
        as_ = 1.0 - std::exp (-1.0 / (0.060 * sr_));

        // the split runs 2 * Pmax behind the pitch, which runs Lp behind the input
        pMax_ = (int) std::ceil (sr_ / 30.0);
        Lp_ = (int) (W_ / 2 + hopIn_ + 64);
        aMax_ = (int) std::ceil (0.45 / 80.0 * sr_) + 2; // the residual low-pass's group delay at the lowest Range
        lag2_ = 2 * pMax_ + aMax_;
        lat_ = Lp_ + lag2_;
        lat_ = ((lat_ + kSub - 1) / kSub) * kSub;
        Lp_ = lat_ - lag2_;
        rsize_ = 1;
        while (rsize_ < 4 * pMax_ + 64)
            rsize_ <<= 1;
        dsize_ = 1;
        while (dsize_ < lat_ + 4096)
            dsize_ <<= 1;
        for (auto& d : dl_)
            d.assign ((size_t) dsize_, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            dr_[c].assign ((size_t) rsize_, 0.0);
            rr_[c].assign ((size_t) rsize_, 0.0);
        }
        tapPr_.assign ((size_t) rsize_, 0.0);
        tapRr_.assign ((size_t) rsize_, 0.0);
        for (int c = 0; c < 2; ++c)
            for (int h = 0; h < kH; ++h)
            {
                S1_[c][h].assign ((size_t) rsize_, Cx {});
                S2_[c][h].assign ((size_t) rsize_, Cx {});
            }
        zr_.assign ((size_t) rsize_, Cx { 1.0, 0.0 });
        f0r_.assign ((size_t) rsize_, 55.0f);
        vr_.assign ((size_t) rsize_, 0);
        rangeApplied_ = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& d : dl_)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (din_.begin(), din_.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
            for (int h = 0; h < kH; ++h)
            {
                std::fill (S1_[c][h].begin(), S1_[c][h].end(), Cx {});
                std::fill (S2_[c][h].begin(), S2_[c][h].end(), Cx {});
                accS1_[c][h] = accS2_[c][h] = Cx {};
            }
        for (int c = 0; c < 2; ++c)
        {
            std::fill (dr_[c].begin(), dr_[c].end(), 0.0);
            std::fill (rr_[c].begin(), rr_[c].end(), 0.0);
        }
        std::fill (tapPr_.begin(), tapPr_.end(), 0.0);
        std::fill (tapRr_.begin(), tapRr_.end(), 0.0);
        std::fill (zr_.begin(), zr_.end(), Cx { 1.0, 0.0 });
        std::fill (f0r_.begin(), f0r_.end(), 55.0f);
        std::fill (vr_.begin(), vr_.end(), 0);
        lpA_.z[0] = lpA_.z[1] = St {};
        for (auto& l : lpR_)
            l.z[0] = l.z[1] = St {};
        nIn_ = 0;
        m_ = 0;
        frames_.fill (Frame {});
        frameCount_ = 0;
        onsets_.fill (-1000000000LL);
        onsetN_ = onsetHead_ = 0;
        inOnset_ = false;
        lastOnsetT_ = -1000000;
        pf_ = ps_ = 0.0;
        theta_ = 0.0;
        f0s_ = 55.0;
        f0Tick_ = 0.0f;
        voicedTick_ = false;
        jumps_.fill (-1000000000LL);
        jumpHead_ = 0;
        v_ = 0.0;
        gr_ = 1.0;
        for (int h = 0; h < kH; ++h)
        {
            g_[h] = 1.0;
            ref_[h] = -100.0;
            relSm_[h] = -100.0;
            inDb_[h] = outDb_[h] = -100.0f;
        }
        refN_ = 0.0;
        c1s_ = c1t_ = Cx {};
        aRes_ = 0;
        fundG_ = 1.0;
        repW_ = 1.0;
        pitchOut_ = 0.0f;
        partEn_ = resEn_ = resOutEn_ = 0.0;
        rangeApplied_ = -1.0f;
    }

    int latencySamples() const { return lat_; }
    void setParams (const Params& p) { prm_ = p; }

    // for the UI and the tests
    float pitchHz() const { return pitchOut_; }
    float voicing() const { return (float) v_; }
    float harmonicInDb (int h) const { return inDb_[std::clamp (h, 0, kH - 1)]; }   // re the fundamental
    float harmonicOutDb (int h) const { return outDb_[std::clamp (h, 0, kH - 1)]; }
    float noteToResidualInDb() const { return (float) (10.0 * std::log10 ((partEn_ + 1e-20) / (resEn_ + 1e-20))); }
    float noteToResidualOutDb() const { return (float) (10.0 * std::log10 ((partEn_ + 1e-20) / (resOutEn_ + 1e-20))); }
    float residualGainDb() const { return (float) (20.0 * std::log10 (std::max (gr_, 1e-6))); }

    // Optional taps for testing: what the split produced for the sample that just left (mono average).
    double lastPartials() const { return tapP_; }
    double lastResidual() const { return tapR_; }

    void process (float* const* ch, int nCh, int n)
    {
        nCh = std::min (nCh, 2);
        const int mask = dsize_ - 1;
        for (int i = 0; i < n; ++i)
        {
            if (nIn_ % kSub == 0)
                controlInput();
            double x[2];
            for (int c = 0; c < 2; ++c)
            {
                x[c] = ch[std::min (c, nCh - 1)][i];
                if (! std::isfinite (x[c]))
                    x[c] = 0.0;
            }
            analyse (0.5 * (x[0] + x[1]));
            const int wr = (int) (nIn_ & mask);
            for (int c = 0; c < nCh; ++c)
                dl_[c][(size_t) wr] = (float) x[c];

            stageA (nCh);
            double out[2] = { 0.0, 0.0 };
            stageB (nCh, out);
            for (int c = 0; c < nCh; ++c)
                ch[c][i] = (float) out[c];
            ++nIn_;
        }
    }

private:
    static constexpr int kRing = 4096, kFrames = 1024;

    struct Svf
    {
        double g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0;
    };
    struct St
    {
        double ic1 = 0, ic2 = 0;
    };
    struct Lr4
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
    void setLp (Lr4& l, double fc)
    {
        fc = std::min (fc, 0.45 * sr_);
        l.s.g = std::tan (kPi * fc / sr_);
        l.s.k = 1.4142135623730951;
        l.s.a1 = 1.0 / (1.0 + l.s.g * (l.s.g + l.s.k));
        l.s.a2 = l.s.g * l.s.a1;
        l.s.a3 = l.s.g * l.s.a2;
    }
    struct Frame
    {
        int64_t end = 0;
        float f0 = 0.0f, conf = 0.0f, purity = 0.0f;
    };

    // ---------------- pitch tracking (input time) ------------------------------------------
    void analyse (double a)
    {
        const double lo = lpA_.process (a);
        pf_ += af_ * (lo * lo - pf_);
        ps_ += as_ * (lo * lo - ps_);
        if (nIn_ % D_ == 0)
        {
            din_[(size_t) (m_ & (kRing - 1))] = (float) lo;
            ++m_;
            if (m_ % hopD_ == 0 && m_ >= N_)
                frame();
        }
    }

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
                // four independent sums: the compiler can turn this into SIMD (a single chain of additions cannot be)
                const float* w = win_.data();
                const float* v = w + tau;
                float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
                int j = 0;
                for (; j + 4 <= Wd; j += 4)
                {
                    const float d0 = w[j] - v[j], d1 = w[j + 1] - v[j + 1], d2 = w[j + 2] - v[j + 2], d3 = w[j + 3] - v[j + 3];
                    s0 += d0 * d0;
                    s1 += d1 * d1;
                    s2 += d2 * d2;
                    s3 += d3 * d3;
                }
                for (; j < Wd; ++j)
                {
                    const float d0 = w[j] - v[j];
                    s0 += d0 * d0;
                }
                diff_[(size_t) tau] = (s0 + s1) + (s2 + s3);
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


    // onsets, for telling which pitch window is the pure one at a note change, and for protecting attacks
    void controlInput()
    {
        const double lvl = 10.0 * std::log10 (ps_ + 1e-12);
        const double wgt = std::clamp ((lvl + 72.0) / 12.0, 0.0, 1.0);
        const double d = 10.0 * std::log10 ((pf_ + 1e-12) / (ps_ + 1e-12)) * wgt;
        if (d > 4.5)
        {
            if (! inOnset_ && nIn_ - lastOnsetT_ > (int64_t) (0.04 * sr_))
            {
                lastOnsetT_ = nIn_;
                onsets_[(size_t) (onsetHead_++ & 31)] = nIn_ - (int64_t) (0.0065 * sr_);
                onsetN_ = std::min (onsetN_ + 1, 32);
            }
            inOnset_ = true;
        }
        else if (d < 2.0)
            inOnset_ = false;
    }

    // ---------------- stage A: pitch, phase, heterodyne (Lp_ behind the input) -------------
    void pitchTick (int64_t t1)
    {
        float f0 = 0.0f, pur = 0.0f;
        const bool voiced = pickCentred (t1, f0, pur);
        if (voiced)
        {
            f0 = std::clamp (f0, 31.0f, 260.0f);
            const bool jump = ! voicedTick_ || std::fabs (12.0 * std::log2 (f0 / std::max (1.0f, f0Tick_))) > 0.7;
            if (jump)
                jumps_[(size_t) (jumpHead_++ & 63)] = t1;
            f0Tick_ = f0;
        }
        else if (voicedTick_)
            jumps_[(size_t) (jumpHead_++ & 63)] = t1; // the note ends: the split is not trusted around here either
        voicedTick_ = voiced;
    }

    // the pitch frame whose window is centred on t (the split around a note change is faded out anyway)
    bool pickCentred (int64_t t, float& f0, float& purity) const
    {
        if (frameCount_ == 0)
            return false;
        const int64_t latest = frameCount_ - 1, oldest = std::max<int64_t> (0, frameCount_ - kFrames + 1);
        const Frame& L = frames_[(size_t) (latest & (kFrames - 1))];
        const int64_t want = t + W_ / 2;
        if (want > L.end)
            return false;
        const int64_t k = latest - (L.end - want + hopIn_ / 2) / hopIn_;
        if (k < oldest)
            return false;
        const Frame& F = frames_[(size_t) (k & (kFrames - 1))];
        if (! (F.f0 > 0.0f && F.conf >= 0.8f && F.purity >= 0.03f))
            return false;
        f0 = F.f0;
        purity = F.purity;
        return true;
    }

    Cx readS (const std::vector<Cx>& r, double tau) const
    {
        const double fl = std::floor (tau);
        const int64_t i = (int64_t) fl;
        const double f = tau - fl;
        const Cx a = r[(size_t) (i & (rsize_ - 1))], b = r[(size_t) ((i + 1) & (rsize_ - 1))];
        return { a.re + (b.re - a.re) * f, a.im + (b.im - a.im) * f };
    }

    void stageA (int nCh)
    {
        const int64_t t1 = nIn_ - Lp_;
        if (t1 < 0)
            return;
        if ((t1 & (kSub - 1)) == 0)
            pitchTick (t1);
        // the phase of the note: glides followed within a few ms, jumps taken at once
        if (voicedTick_)
        {
            if (std::fabs (12.0 * std::log2 (f0Tick_ / f0s_)) > 0.7)
                f0s_ = f0Tick_;
            else
                f0s_ = std::exp (std::log (f0s_) + (1.0 - std::exp (-1.0 / (0.003 * sr_))) * (std::log ((double) f0Tick_) - std::log (f0s_)));
        }
        theta_ += 2.0 * kPi * f0s_ / sr_;
        if (theta_ > 2.0 * kPi)
            theta_ -= 2.0 * kPi;
        const Cx z { std::cos (theta_), std::sin (theta_) };
        const size_t k = (size_t) (t1 & (rsize_ - 1));
        zr_[k] = z;
        f0r_[k] = (float) f0s_;
        vr_[k] = voicedTick_ ? 1 : 0;
        const int mask = dsize_ - 1;
        const Cx zc { z.re, -z.im };
        for (int c = 0; c < nCh; ++c)
        {
            const double x = dl_[c][(size_t) (t1 & mask)];
            Cx zh { 1.0, 0.0 };
            for (int h = 0; h < kH; ++h)
            {
                zh = zh * zc; // e^{-j (h+1) theta}
                accS1_[c][h] = accS1_[c][h] + zh * x;
                S1_[c][h][k] = accS1_[c][h];
            }
        }
        // first two-period average, centred pMax_ samples back (its window reaches at most pMax_ ahead)
        const int64_t u = t1 - pMax_;
        if (u < 0)
            return;
        const size_t ku = (size_t) (u & (rsize_ - 1));
        const double P = sr_ / (double) f0r_[ku];
        for (int c = 0; c < nCh; ++c)
            for (int h = 0; h < kH; ++h)
            {
                const Cx b1 = (readS (S1_[c][h], (double) u + P) - readS (S1_[c][h], (double) u - P)) * (0.5 / P);
                accS2_[c][h] = accS2_[c][h] + b1;
                S2_[c][h][ku] = accS2_[c][h];
            }
    }

    // ---------------- stage B: the split and the processing (at the output) ----------------
    void controlOutput (int64_t t2)
    {
        const size_t k2 = (size_t) (t2 & (rsize_ - 1));
        const double f0 = f0r_[k2], P = sr_ / f0;
        bool stable = vr_[k2] != 0;
        for (int i = 0; i < 64 && stable; ++i)
            if (std::llabs (jumps_[(size_t) i] - t2) < (int64_t) (2.0 * P + 0.125 * (double) W_) + kSub)
                stable = false;
        const double tick = (double) kSub / sr_;
        const double vt = stable ? 1.0 : 0.0;
        v_ += (1.0 - std::exp (-tick / 0.008)) * (vt - v_);
        if (vt == 0.0 && v_ < 1e-4)
            v_ = 0.0;
        if (vt == 1.0 && v_ > 1.0 - 1e-4)
            v_ = 1.0;
        pitchOut_ = stable ? (float) f0 : 0.0f;

        // attacks are mostly residual (pick, string noise): give them about 40 ms before the residual is touched
        int64_t lastOn = -1000000000LL;
        for (int i = 0; i < onsetN_; ++i)
            if (onsets_[(size_t) i] <= t2 && onsets_[(size_t) i] > lastOn)
                lastOn = onsets_[(size_t) i];
        attW_ = std::clamp (((double) (t2 - lastOn) - 0.003 * sr_) / (0.035 * sr_), 0.0, 1.0);
        // repair is a slow steadying of the fundamental: keep it off the attack, where it would only lag behind
        repW_ = std::clamp (((double) (t2 - lastOn) - 0.02 * sr_) / (0.12 * sr_), 0.0, 1.0);

        const double c = std::clamp ((double) prm_.contrast, -1.0, 1.0);
        const double grT = std::pow (10.0, (c > 0.0 ? -12.0 * c : -6.0 * c) / 20.0);
        gr_ += (1.0 - std::exp (-tick / 0.02)) * (grT - gr_);
        if (std::fabs (gr_ - grT) < 1e-6)
            gr_ = grT;

        if ((float) prm_.rangeHz != rangeApplied_)
        {
            rangeApplied_ = prm_.rangeHz;
            const double fc = std::clamp ((double) prm_.rangeHz, 80.0, 600.0);
            for (auto& l : lpR_)
                setLp (l, fc);
            aRes_ = std::min (aMax_, (int) std::lround (0.45 / fc * sr_));
        }

        // tone lock: each harmonic's share of the note, against its long-term average
        double tot = 0.0, L[kH];
        for (int h = 0; h < kH; ++h)
        {
            const double m = mag (cMono_[h]);
            tot += m * m;
            L[h] = 20.0 * std::log10 (m + 1e-9);
        }
        const double totDb = 10.0 * std::log10 (tot + 1e-18);
        const bool loud = totDb > -70.0;
        for (int h = 0; h < kH; ++h)
        {
            const double rel = L[h] - totDb;
            if (stable && loud)
            {
                relSm_[h] = relSm_[h] < -99.0 ? rel : relSm_[h] + (1.0 - std::exp (-tick / 0.08)) * (rel - relSm_[h]);
                if (relSm_[h] > -50.0)
                    ref_[h] = ref_[h] < -99.0 ? relSm_[h] : ref_[h] + (1.0 - std::exp (-tick / 3.0)) * (relSm_[h] - ref_[h]);
            }
            double gDb = 0.0;
            if (stable && loud && relSm_[h] > -45.0 && ref_[h] > -99.0)
                gDb = std::clamp ((double) prm_.toneLock, 0.0, 1.0) * std::clamp (ref_[h] - relSm_[h], -9.0, 9.0);
            if (h == 0)
                gDb += std::clamp ((double) prm_.fundamentalDb, -12.0, 12.0);
            const double gT = std::pow (10.0, gDb / 20.0);
            g_[h] += (1.0 - std::exp (-tick / 0.03)) * (gT - g_[h]);
            if (std::fabs (g_[h] - gT) < 1e-6)
                g_[h] = gT;
            if (loud && stable)
            {
                inDb_[h] = (float) (L[h] - L[0]);
                outDb_[h] = (float) (20.0 * std::log10 (mag (cOutMono_[h]) + 1e-9) - 20.0 * std::log10 (mag (cOutMono_[0]) + 1e-9));
            }
        }
    }

    // The split is computed aMax_ samples ahead of the output, so the residual can be low-passed (below Range) with its
    // group delay compensated: the cut lands in phase with the residual, and the note is untouched.
    void stageB (int nCh, double* out)
    {
        const int64_t t2 = nIn_ - lat_, t3 = t2 + aMax_;
        const int mask = dsize_ - 1;
        if (t3 >= 0)
            split (nCh, t3);
        if (t2 < 0)
        {
            for (int c = 0; c < nCh; ++c)
                out[c] = 0.0;
            return;
        }
        const size_t k2 = (size_t) (t2 & (rsize_ - 1));
        const double gRes = v_ * attW_ * (gr_ - 1.0);
        double rOutM = 0.0;
        for (int c = 0; c < nCh; ++c)
        {
            const double x = dl_[c][(size_t) (t2 & mask)];
            const double rIn = rr_[c][(size_t) ((t2 + aRes_) & (rsize_ - 1))];
            const double rLow = lpR_[c].process (rIn); // lines up with the residual at t2
            out[c] = x + dr_[c][k2] + gRes * rLow;
            rOutM += (rr_[c][k2] + gRes * rLow) / nCh;
        }
        tapP_ = tapPr_[k2];
        tapR_ = tapRr_[k2];
        const double ae = 1.0 - std::exp (-1.0 / (0.3 * sr_));
        partEn_ += ae * (tapP_ * tapP_ - partEn_);
        resEn_ += ae * (tapR_ * tapR_ - resEn_);
        resOutEn_ += ae * (rOutM * rOutM - resOutEn_);
    }

    void split (int nCh, int64_t t3)
    {
        const int mask = dsize_ - 1;
        if ((t3 & (kSub - 1)) == 0)
            controlOutput (t3);
        const size_t k3 = (size_t) (t3 & (rsize_ - 1));
        const double P = sr_ / (double) f0r_[k3];
        const Cx z = zr_[k3];
        Cx zp[kH];
        zp[0] = z;
        for (int h = 1; h < kH; ++h)
            zp[h] = zp[h - 1] * z;
        const double ar = 1.0 - std::exp (-1.0 / (0.06 * sr_));
        const double rep = std::clamp ((double) prm_.repair, 0.0, 1.0) * repW_, tr = std::clamp ((double) prm_.translate, 0.0, 1.0);
        const bool active = v_ > 0.0;
        double pM = 0.0, rM = 0.0;
        for (int h = 0; h < kH; ++h)
            cMono_[h] = cOutMono_[h] = Cx {};
        for (int c = 0; c < nCh; ++c)
        {
            const double x = dl_[c][(size_t) (t3 & mask)];
            Cx cs[kH], co[kH];
            double sumP = 0.0, sumPo = 0.0;
            for (int h = 0; h < kH; ++h)
            {
                // second two-period average: the partial's complex envelope (x2 for a real signal)
                cs[h] = (readS (S2_[c][h], (double) t3 + P) - readS (S2_[c][h], (double) t3 - P)) * (1.0 / P);
                sumP += (cs[h] * zp[h]).re;
                cMono_[h] = cMono_[h] + cs[h] * (1.0 / nCh);
            }
            if (c == 0)
            {
                // the fundamental's slow part (two smoothing poles, 60 ms each): whatever beats against it rotates and averages out
                c1s_ = c1s_ + (cs[0] - c1s_) * ar;
                c1t_ = c1t_ + (c1s_ - c1t_) * ar;
            }
            if (active)
            {
                for (int h = 0; h < kH; ++h)
                    co[h] = cs[h] * g_[h];
                if (rep > 0.0)
                    co[0] = (cs[0] * (1.0 - rep) + c1t_ * rep) * g_[0];
                if (tr > 0.0)
                {
                    // harmonics 2-4 kept at least 6, 9, 12 dB below the fundamental, generated phase-locked to it if absent
                    const double m1 = mag (co[0]);
                    const Cx u1 = m1 > 1e-12 ? co[0] * (1.0 / m1) : Cx { 1.0, 0.0 };
                    Cx uh = u1;
                    for (int h = 1; h <= 3; ++h)
                    {
                        uh = uh * u1; // e^{j (h+1) arg c1}
                        const double T = m1 * std::pow (10.0, -(6.0 + 3.0 * (h - 1)) / 20.0);
                        const double m = mag (co[h]);
                        if (m < T)
                        {
                            const double target = m + tr * (T - m);
                            const Cx dir = co[h] + uh * (0.1 * T);
                            const double md = mag (dir);
                            if (md > 1e-15)
                                co[h] = dir * (target / md);
                        }
                    }
                }
                for (int h = 0; h < kH; ++h)
                {
                    sumPo += (co[h] * zp[h]).re;
                    cOutMono_[h] = cOutMono_[h] + co[h] * (1.0 / nCh);
                }
            }
            else
            {
                sumPo = sumP;
                for (int h = 0; h < kH; ++h)
                    cOutMono_[h] = cMono_[h];
            }
            const double r = x - sumP;
            rr_[c][k3] = r;
            dr_[c][k3] = v_ * (sumPo - sumP);
            pM += sumP / nCh;
            rM += r / nCh;
        }
        tapPr_[k3] = pM;
        tapRr_[k3] = rM;
    }

    Params prm_;
    double sr_ = 48000.0, fsd_ = 4800.0;
    int D_ = 10, tauMax_ = 170, tauMin_ = 18, N_ = 340, hopD_ = 20;
    int64_t hopIn_ = 200, W_ = 3400;
    std::vector<float> din_, win_, diff_, cmnd_;
    Lr4 lpA_, lpR_[2];
    std::array<Frame, kFrames> frames_;
    int64_t frameCount_ = 0, m_ = 0, nIn_ = 0;
    std::array<int64_t, 32> onsets_ {};
    int onsetN_ = 0, onsetHead_ = 0;
    bool inOnset_ = false;
    int64_t lastOnsetT_ = -1000000;
    double pf_ = 0.0, ps_ = 0.0, af_ = 0.01, as_ = 0.001;

    int pMax_ = 1600, Lp_ = 4000, lag2_ = 3204, lat_ = 7200, rsize_ = 8192, dsize_ = 16384;
    std::vector<float> dl_[2];
    std::vector<Cx> S1_[2][kH], S2_[2][kH], zr_;
    Cx accS1_[2][kH], accS2_[2][kH];
    std::vector<float> f0r_;
    std::vector<unsigned char> vr_;
    double theta_ = 0.0, f0s_ = 55.0;
    float f0Tick_ = 0.0f;
    bool voicedTick_ = false;
    std::array<int64_t, 64> jumps_ {};
    int jumpHead_ = 0;

    double v_ = 0.0, gr_ = 1.0, g_[kH] {}, ref_[kH] {}, relSm_[kH] {}, refN_ = 0.0;
    Cx c1s_, c1t_, cMono_[kH], cOutMono_[kH];
    int aMax_ = 300, aRes_ = 0;
    double repW_ = 1.0;
    std::vector<double> dr_[2], rr_[2], tapPr_, tapRr_;
    double fundG_ = 1.0, vTarget_ = 0.0, attW_ = 1.0;
    float rangeApplied_ = -1.0f;
    float pitchOut_ = 0.0f, inDb_[kH] {}, outDb_[kH] {};
    double partEn_ = 0.0, resEn_ = 0.0, resOutEn_ = 0.0, tapP_ = 0.0, tapR_ = 0.0;
};
} // namespace nsp
