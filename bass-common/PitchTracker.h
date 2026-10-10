#pragma once

// Pitch and onset tracking for monophonic bass (no JUCE), shared by the bass plug-ins.
//
// The input (mono) is low-passed at 400 Hz and decimated to about 5 kHz. Every 4 ms a YIN frame (two periods of the lowest
// note, 30 Hz) yields a pitch, a confidence and a purity (the share of the window's energy that sits on the pitch itself;
// a "pitch" with no energy at its own frequency is a trick of the tracker, e.g. a chord's common period). A fast (8 ms)
// and a slow (60 ms) envelope of the low band feed an onset detector (+4.5 dB), which places an onset ~6.5 ms before the
// moment it fires. Everything is indexed by input sample number, so results do not depend on the host's block size.
//
// Callers: call push() for every input sample, control() once every kSub samples *before* the push of that sample, and
// ask pickCentred() (one window centred on a time) or pickPitch() (the pure one of a past and a future window) for a note.

#include "Dsp.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace bass
{
class PitchTracker
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
        hopD_ = std::max (4, (int) std::lround (0.004 * fsd_));
        hopIn_ = (int64_t) hopD_ * D_;
        W_ = (int64_t) N_ * D_;
        win_.assign ((size_t) N_, 0.0f);
        diff_.assign ((size_t) tauMax_ + 2, 0.0f);
        cmnd_.assign ((size_t) tauMax_ + 2, 0.0f);
        din_.assign (kRing, 0.0f);
        lp_.setLowpass (400.0, sr_);
        af_ = 1.0 - std::exp (-1.0 / (0.008 * sr_));
        as_ = 1.0 - std::exp (-1.0 / (0.060 * sr_));
        valN_ = std::clamp ((int) std::lround (0.04 * sr_ / kSub), 1, (int) kValMax);
        am_ = 1.0 - std::exp (-1.0 / (0.030 * sr_));
        al_ = 1.0 - std::exp (-1.0 / (0.120 * sr_));
        reset();
    }

    void reset()
    {
        std::fill (din_.begin(), din_.end(), 0.0f);
        lp_.reset();
        nIn_ = 0;
        m_ = 0;
        frames_.fill (Frame {});
        frameCount_ = 0;
        onsets_.fill (-1000000000LL);
        onsetN_ = onsetHead_ = 0;
        inOnset_ = false;
        lastOnsetT_ = -1000000;
        pf_ = ps_ = pm_ = pl_ = 0.0;
        val_.fill (0.0f);
        valHead_ = 0;
        peakSince_ = 0.0;
        dipped_ = true;
    }

    int64_t samples() const { return nIn_; }
    int64_t windowSamples() const { return W_; }   // length of one pitch window, input samples
    int64_t hopSamples() const { return hopIn_; }
    double fastPower() const { return pf_; }       // 8 ms envelope of the low band's power
    double slowPower() const { return ps_; }       // 60 ms envelope
    double mediumPower() const { return pm_; }     // 30 ms
    double longPower() const { return pl_; }       // 120 ms
    bool inOnset() const { return inOnset_; }
    int64_t lastOnset() const { return lastOnsetT_; }   // input sample at which the detector last fired
    int onsetCount() const { return onsetN_; }
    int64_t onsetTime (int i) const { return onsets_[(size_t) i]; } // estimated start of the attack (input samples)
    int64_t newestOnsetTime() const { return onsets_[(size_t) ((onsetHead_ - 1) & 31)]; }

    // call once every kSub samples, before push() of the sample at which nIn == multiple of kSub
    void control()
    {
        const double lvl = 10.0 * std::log10 (ps_ + 1e-12);
        const double wgt = std::clamp ((lvl + 72.0) / 12.0, 0.0, 1.0);
        const double d = 10.0 * std::log10 ((pf_ + 1e-12) / (ps_ + 1e-12)) * wgt;
        // Fast playing: right after a short note the 60 ms envelope is still full of it, and the next attack hardly rises above
        // it. Against the quietest moment of the last 40 ms (the gap between two staccato notes) it rises a lot: 10 dB above that
        // also counts, if the level itself is above -60 dBFS (a note, not noise in a gap).
        double valley = pf_;
        for (int i = 0; i < valN_; ++i)
            valley = std::min (valley, (double) val_[(size_t) i]);
        val_[(size_t) (valHead_++ % valN_)] = (float) pf_;
        // ...but only once the level has really dipped (6 dB under its peak since the last attack: a release, a gap), so a low
        // note whose 8 ms envelope ripples by a few dB never fires again by itself
        peakSince_ = std::max (peakSince_, pf_);
        if (pf_ < 0.25 * peakSince_)
            dipped_ = true;
        const double dV = dipped_ && pf_ > 1e-6 ? 10.0 * std::log10 ((pf_ + 1e-12) / (valley + 1e-12)) : 0.0;
        if (d > 4.5 || dV > 10.0)
        {
            // (a rise out of a real gap may follow 40 ms after the last attack; a rise against the 60 ms envelope only after 60 ms,
            // as the 8 ms envelope of a low note ripples by a few dB and could fire again within the same note)
            const int64_t since = nIn_ - lastOnsetT_;
            if (! inOnset_ && (dV > 10.0 ? since > (int64_t) (0.04 * sr_) : since > (int64_t) (0.06 * sr_)))
            {
                lastOnsetT_ = nIn_;
                onsets_[(size_t) (onsetHead_++ & 31)] = nIn_ - (int64_t) (0.0065 * sr_);
                onsetN_ = std::min (onsetN_ + 1, 32);
                peakSince_ = pf_;
                dipped_ = false;
            }
            inOnset_ = true;
        }
        else if (d < 2.0)
            inOnset_ = false;
    }

    void push (double a)
    {
        const double lo = lp_.process (a);
        pf_ += af_ * (lo * lo - pf_);
        ps_ += as_ * (lo * lo - ps_);
        pm_ += am_ * (lo * lo - pm_);
        pl_ += al_ * (lo * lo - pl_);
        if (nIn_ % D_ == 0)
        {
            din_[(size_t) (m_ & (kRing - 1))] = (float) lo;
            ++m_;
            if (m_ % hopD_ == 0 && m_ >= N_)
                frame();
        }
        ++nIn_;
    }

    // The note whose window is centred on input time t (processing fades out around a note change anyway).
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
        if (! (F.f0 > 0.0f && F.conf >= 0.8f && F.purity >= kMinPurity))
            return false;
        f0 = F.f0;
        purity = F.purity;
        return true;
    }

    // The note under time t. A window that ends at t sees only the past, one that starts at t only the future; near a
    // note change one of them is mixed, and an onset between them says which one is pure.
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
        auto voiced = [] (const Frame* f) { return f != nullptr && f->f0 > 0.0f && f->conf >= 0.75f && f->purity >= kMinPurity; };
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
                for (int i = 0; i < onsetN_; ++i)
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

    // The pitch of whatever plays between input times a and b (a short note, say), measured on that stretch alone: YIN over
    // the stretch with lags up to a third of its length, so it needs about three periods of the note. Returns false for no
    // clear pitch (same confidence and purity tests as the frames).
    bool pitchOver (int64_t a, int64_t b, float& f0) const
    {
        const int64_t ma = (a + D_ - 1) / D_, mb = std::min (b / D_, m_ - 1);
        const int L = (int) (mb - ma + 1);
        if (L < 3 * tauMin_ || mb - ma >= kRing - 1 || ma < 0)
            return false;
        const int tMax = std::min (tauMax_, L / 3);
        if (tMax <= tauMin_ + 1)
            return false;
        std::vector<float> w ((size_t) L);
        double mean = 0.0;
        for (int j = 0; j < L; ++j)
            mean += (w[(size_t) j] = din_[(size_t) ((ma + j) & (kRing - 1))]);
        mean /= L;
        for (auto& v : w)
            v -= (float) mean;
        std::vector<float> d ((size_t) tMax + 2, 0.0f), cm ((size_t) tMax + 2, 1.0f);
        for (int tau = 1; tau <= tMax + 1 && tau < L; ++tau)
        {
            double s2 = 0.0;
            for (int j = 0; j + tau < L; ++j)
            {
                const double e = (double) w[(size_t) j] - w[(size_t) (j + tau)];
                s2 += e * e;
            }
            d[(size_t) tau] = (float) (s2 / (double) (L - tau)); // per-sample, so lags of different overlap compare
        }
        double run = 0.0;
        for (int tau = 1; tau <= tMax + 1; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0.0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin_; tau <= tMax; ++tau)
            if (cm[(size_t) tau] < 0.2f)
            {
                while (tau + 1 <= tMax && cm[(size_t) tau + 1] < cm[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        if (best < 0)
            return false;
        float tau = (float) best;
        if (best + 1 <= tMax + 1)
        {
            const float p0 = cm[(size_t) best - 1], p1 = cm[(size_t) best], p2 = cm[(size_t) best + 1], den = p0 - 2.0f * p1 + p2;
            if (std::fabs (den) > 1e-9f)
                tau += 0.5f * (p0 - p2) / den;
        }
        f0 = (float) fsd_ / tau;
        if (1.0f - cm[(size_t) best] < 0.8f)
            return false;
        double pur = 0.0;
        return purityAt (mb, f0, L, pur) && pur >= kMinPurity;
    }

private:
    static constexpr int kRing = 4096, kFrames = 1024, kValMax = 1024;
    std::array<float, kValMax> val_ {};
    int valN_ = 120, valHead_ = 0;
    double peakSince_ = 0.0;
    bool dipped_ = true;
    static constexpr float kMinPurity = 0.35f;
    struct Frame
    {
        int64_t end = 0;
        float f0 = 0.0f, conf = 0.0f, purity = 0.0f;
    };

    // Share of the window's energy that sits on harmonics 1..4 of f0 (exactly K periods, so nothing leaks and the harmonics
    // are orthogonal). Counting the fundamental alone would throw out real bass notes whose fundamental is weak or missing
    // (an amp or a cab that cuts the lowest octave, a sub that was filtered away): their energy sits on the 2nd to 4th.
    // Noise scores 0.45 at most (p99 0.33), a note 0.65 or more.
    bool purityAt (int64_t last, float f0, int maxL, double& purity) const
    {
        const double period = fsd_ / f0;
        int K = std::max (2, (int) std::ceil (0.05 * f0));
        K = std::min (K, (int) std::floor (maxL / period));
        if (K < 1)
            return false;
        const int L = (int) std::lround (K * period);
        if (L < 4 || last - L + 1 < 0)
            return false;
        double e2 = 0.0, share = 0.0;
        for (int j = 0; j < L; ++j)
        {
            const double x = din_[(size_t) ((last - (L - 1) + j) & (kRing - 1))];
            e2 += x * x;
        }
        if (e2 / L < 1e-10)
            return false;
        for (int h = 1; h <= 4; ++h)
        {
            const double w = 2.0 * kPi * K * h / L, c = std::cos (w), s = std::sin (w);
            double pr = 1.0, pi = 0.0, re = 0.0, im = 0.0;
            for (int j = 0; j < L; ++j)
            {
                const double x = din_[(size_t) ((last - (L - 1) + j) & (kRing - 1))];
                re += x * pr;
                im -= x * pi;
                const double nr = pr * c - pi * s;
                pi = pr * s + pi * c;
                pr = nr;
            }
            const double amp = 2.0 / L * std::sqrt (re * re + im * im);
            share += 0.5 * amp * amp / (e2 / L);
        }
        purity = std::min (1.0, share);
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
                if (purityAt (last, fr.f0, N_, p))
                    fr.purity = (float) p;
            }
        }
        frames_[(size_t) (frameCount_ & (kFrames - 1))] = fr;
        ++frameCount_;
    }

    double sr_ = 48000.0, fsd_ = 4800.0;
    int D_ = 10, tauMax_ = 170, tauMin_ = 18, N_ = 340, hopD_ = 20;
    int64_t hopIn_ = 200, W_ = 3400;
    std::vector<float> din_, win_, diff_, cmnd_;
    Lr4 lp_;
    std::array<Frame, kFrames> frames_;
    int64_t frameCount_ = 0, m_ = 0, nIn_ = 0;
    std::array<int64_t, 32> onsets_ {};
    int onsetN_ = 0, onsetHead_ = 0;
    bool inOnset_ = false;
    int64_t lastOnsetT_ = -1000000;
    double pf_ = 0.0, ps_ = 0.0, pm_ = 0.0, pl_ = 0.0, af_ = 0.01, as_ = 0.001, am_ = 0.002, al_ = 0.0005;
};
} // namespace bass
