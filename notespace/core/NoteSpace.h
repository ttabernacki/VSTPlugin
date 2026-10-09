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
//   Punch, Sustain   the low band's envelope: attacks and the body's ring-out (bass-common/EnvelopeShaper.h).
//   Kick        optional sidechain: the residual (mud, boom) ducks while the kick plays. The note is the one thing that is
//               never ducked, so the kick makes room without the bass losing its pitch.
//   Translate   harmonics 2-4 lifted (or generated, phase-locked to the fundamental) to a floor below the fundamental,
//               so the note keeps its pitch on small speakers.
// Processing fades out where the split cannot be trusted: no pitch, a pitch change in the averaging window.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "../../bass-common/EnvelopeShaper.h"

namespace nsp
{
using bass::Cx;
using bass::mag;
constexpr double kPi = bass::kPi;

struct Params
{
    float contrast = 0.4f;      // -1..1  residual +18 dB .. -40 dB (deliberately extreme: find the sweet spot by ear)
    float toneLock = 0.0f;      // 0..1
    float fundamentalDb = 0.0f; // -18..+18
    float repair = 0.0f;        // 0..1   blend to 100 % at 0.5, then the smoothing stretches from 60 to 200 ms
    float translate = 0.0f;     // 0..2   1 = harmonics 2-4 at -6/-9/-12 dB re the fundamental, 2 = +3/0/-3 dB
    float rangeHz = 300.0f;     // residual cleanup acts below this (50..1000 Hz)
    float punch = 0.0f;         // -1..1  tame .. emphasise attacks (the low band: note and residual below Range)
    float sustain = 0.0f;       // -1..1  shorten .. lengthen the body
    float kick = 0.0f;          // 0..1   duck the residual (never the note) up to 24 dB while the sidechain's kick plays
};

class NoteSpace
{
public:
    static constexpr int kH = 8, kSub = bass::PitchTracker::kSub;

    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        trk_.prepare (sr_);
        lpK_.setLowpass (150.0, sr_);
        aDyn_ = 1.0 - std::exp (-1.0 / (0.0007 * sr_));
        aDuckAtt_ = 1.0 - std::exp (-1.0 / (0.003 * sr_));
        aDuckRel_ = 1.0 - std::exp (-1.0 / (0.08 * sr_));
        ak_ = 1.0 - std::exp (-1.0 / (0.008 * sr_));
        W_ = trk_.windowSamples();
        hopIn_ = trk_.hopSamples();
        // the split runs 2 * Pmax behind the pitch, which runs Lp behind the input
        pMax_ = (int) std::ceil (sr_ / 30.0);
        Lp_ = (int) (W_ / 2 + hopIn_ + 64);
        aMax_ = (int) std::ceil (0.45 / 50.0 * sr_) + 2; // the residual low-pass's group delay at the lowest Range
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
        kpow_.assign ((size_t) dsize_, 0.0f);
        shaper_.prepare (sr_, lat_);
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
        rangeApplied_ = -1.0;
        aPar_ = 1.0 - std::exp (-(double) kSub / (0.03 * sr_));
        reset();
    }

    void reset()
    {
        for (auto& d : dl_)
            std::fill (d.begin(), d.end(), 0.0f);
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
        for (auto& l : lpR_)
            l.reset();
        for (auto& l : lpX_)
            l.reset();
        trk_.reset();
        shaper_.reset();
        lpK_.reset();
        std::fill (kpow_.begin(), kpow_.end(), 0.0f);
        pk_ = scSame_ = duckDb_ = dynDbT_ = 0.0;
        gDyn_ = gDynP_ = gDs_ = 1.0;
        rampB_ = 0;
        nIn_ = 0;
        theta_ = 0.0;
        f0s_ = 55.0;
        f0Tick_ = 0.0f;
        voicedTick_ = false;
        jumps_.fill (-1000000000LL);
        jumpHead_ = 0;
        v_ = vP_ = vC_ = 0.0;
        gr_ = grP_ = grC_ = 1.0;
        attP_ = attC_ = repP_ = 1.0;
        rampPos_ = 0;
        for (int h = 0; h < kH; ++h)
        {
            g_[h] = gP_[h] = 1.0;
            ref_[h] = -100.0;
            relSm_[h] = -100.0;
            inDb_[h] = outDb_[h] = -100.0f;
        }
        c1s_ = c1t_ = Cx {};
        aResF_ = 0.0;
        fresh_ = true;
        repW_ = 1.0;
        pitchOut_ = 0.0f;
        partEn_ = resEn_ = resOutEn_ = 0.0;
        rangeApplied_ = -1.0;
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

    float kickDuckDb() const { return (float) duckDb_; }
    float dynamicsDb() const { return (float) (20.0 * std::log10 (std::max (gDyn_, 1e-6))); }

    // sc: optional sidechain (the kick), scCh channels, same length n
    void process (float* const* ch, int nCh, int n, const float* const* sc = nullptr, int scCh = 0)
    {
        nCh = std::min (nCh, 2);
        const int mask = dsize_ - 1;
        for (int i = 0; i < n; ++i)
        {
            if (nIn_ % kSub == 0)
                {
                trk_.control();
                shaper_.analyse (nIn_, trk_, std::clamp ((double) prm_.sustain, -1.0, 1.0));
            }
            double x[2];
            for (int c = 0; c < 2; ++c)
            {
                x[c] = ch[std::min (c, nCh - 1)][i];
                if (! std::isfinite (x[c]))
                    x[c] = 0.0;
            }
            const double xm = 0.5 * (x[0] + x[1]);
            trk_.push (xm);
            double kin = 0.0;
            if (sc != nullptr && scCh > 0)
            {
                for (int c = 0; c < scCh; ++c)
                    kin += (double) sc[c][i] / scCh;
                if (! std::isfinite (kin))
                    kin = 0.0;
                if (kin != 0.0 || xm != 0.0) // two silences say nothing about whether the tracks are the same
                    scSame_ += ((kin == xm ? 1.0 : 0.0) - scSame_) * (1.0 / 4096.0);
            }
            else
                scSame_ -= scSame_ * (1.0 / 4096.0);
            const double kl = lpK_.process (kin);
            pk_ += ak_ * (kl * kl - pk_);
            kpow_[(size_t) (nIn_ & (dsize_ - 1))] = (float) pk_;
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
    // ---------------- stage A: pitch, phase, heterodyne (Lp_ behind the input) -------------
    void pitchTick (int64_t t1)
    {
        float f0 = 0.0f, pur = 0.0f;
        const bool voiced = trk_.pickCentred (t1, f0, pur);
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
        // every control value glides from where it was to where this tick puts it, over the kSub samples that follow: stepping
        // them once per tick is a zipper (a click train at sr/16) on the low band
        vP_ = v_;
        grP_ = gr_;
        attP_ = attW_;
        repP_ = repW_;
        repAP_ = repA_;
        trP_ = tr_;
        arP_ = ar_;
        for (int h = 0; h < kH; ++h)
            gP_[h] = g_[h];
        rampPos_ = 0;
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
        for (int i = 0; i < trk_.onsetCount(); ++i)
            if (trk_.onsetTime (i) <= t2 && trk_.onsetTime (i) > lastOn)
                lastOn = trk_.onsetTime (i);
        attW_ = std::clamp (((double) (t2 - lastOn) - 0.003 * sr_) / (0.035 * sr_), 0.0, 1.0);
        // repair is a slow steadying of the fundamental: keep it off the attack, where it would only lag behind
        repW_ = std::clamp (((double) (t2 - lastOn) - 0.02 * sr_) / (0.12 * sr_), 0.0, 1.0);

        const double c = std::clamp ((double) prm_.contrast, -1.0, 1.0);
        const double grT = std::pow (10.0, (c > 0.0 ? -40.0 * c : -18.0 * c) / 20.0);
        gr_ += (1.0 - std::exp (-tick / 0.02)) * (grT - gr_);
        if (std::fabs (gr_ - grT) < 1e-6)
            gr_ = grT;

        // Repair, Translate and Range ease toward a knob that moved instead of jumping (a jump is a click on the note)
        const double repT = std::clamp ((double) prm_.repair, 0.0, 1.0), trT = std::clamp ((double) prm_.translate, 0.0, 2.0);
        const double fcT = std::clamp ((double) prm_.rangeHz, 50.0, 1000.0);
        if (fresh_)
        {
            repA_ = repAP_ = repT;
            tr_ = trP_ = trT;
            rangeSm_ = fcT;
        }
        repA_ += aPar_ * (repT - repA_);
        if (std::fabs (repA_ - repT) < 1e-6)
            repA_ = repT;
        tr_ += aPar_ * (trT - tr_);
        if (std::fabs (tr_ - trT) < 1e-6)
            tr_ = trT;
        ar_ = 1.0 - std::exp (-1.0 / ((0.06 + 0.28 * std::max (0.0, repA_ - 0.5)) * sr_)); // 60 ms, stretching to 200 ms above 50 %
        if (fresh_)
            arP_ = ar_;
        rangeSm_ = std::exp (std::log (rangeSm_) + aPar_ * (std::log (fcT) - std::log (rangeSm_)));
        if (std::fabs (rangeSm_ / fcT - 1.0) < 1e-5)
            rangeSm_ = fcT;
        if (rangeSm_ != rangeApplied_)
        {
            rangeApplied_ = rangeSm_;
            for (auto& l : lpR_)
                l.setLowpass (rangeSm_, sr_);
            for (auto& l : lpX_)
                l.setLowpass (rangeSm_, sr_);
            aResF_ = std::min ((double) aMax_ - 1.0, 0.45 / rangeSm_ * sr_); // the residual low-pass's group delay, read ahead
        }
        fresh_ = false;

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
                gDb = std::clamp ((double) prm_.toneLock, 0.0, 1.0) * std::clamp (ref_[h] - relSm_[h], -24.0, 24.0);
            if (h == 0)
                gDb += std::clamp ((double) prm_.fundamentalDb, -18.0, 18.0);
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

        // Kick duck: the kick is read a few ms ahead of the audio leaving the delay, so the duck is already there when the kick
        // hits. Where the split is trusted only the residual below Range is ducked and the note is left alone; where it is not
        // (the first ~100 ms of a note, which is exactly where a kick usually lands, or a note that is not tracked) everything
        // below Range is ducked, as a plain sidechain duck would. A sidechain that carries the bass itself has no kick to make
        // room for.
        const double kdepth = scSame_ > 0.5 ? 0.0 : 24.0 * std::clamp ((double) prm_.kick, 0.0, 1.0);
        double dT = 0.0;
        if (kdepth > 0.0)
        {
            const double pw = kpow_[(size_t) ((t2 + (int64_t) (0.005 * sr_)) & mask)];
            dT = kdepth * std::clamp ((10.0 * std::log10 (pw + 1e-12) + 70.0) / 20.0, 0.0, 1.0); // -70 dBFS: nothing, -50: all
        }
        duckDb_ += (dT > duckDb_ ? aDuckAtt_ : aDuckRel_) * (dT - duckDb_);
        if (duckDb_ < 1e-4 && dT == 0.0)
            duckDb_ = 0.0;
        const double duck = duckDb_ > 0.0 ? std::pow (10.0, -duckDb_ / 20.0) : 1.0;

        // Punch and sustain: one gain for the low band, gliding across each tick
        if ((t2 & (kSub - 1)) == 0)
        {
            gDynP_ = gDyn_;
            dynDbT_ = shaper_.gainDb (std::clamp ((double) prm_.punch, -1.0, 1.0), nIn_, t2 + kSub / 2);
            gDyn_ = std::pow (10.0, dynDbT_ / 20.0);
            rampB_ = 0;
        }
        ++rampB_;
        // the ramps meet at a kink every tick (a line at sr/16 and its multiples): a short smoother takes it off
        gDs_ += aDyn_ * (gDynP_ + (gDyn_ - gDynP_) * (double) std::min (rampB_, kSub) / kSub - gDs_);
        const double gD = gDs_;

        const double gCon = vC_ * attC_ * (grC_ - 1.0) * duck, dk = duck - 1.0; // contrast on the trusted residual; the duck
        const int64_t aResI = (int64_t) std::floor (aResF_);
        const double aResFr = aResF_ - (double) aResI;
        double rOutM = 0.0;
        for (int c = 0; c < nCh; ++c)
        {
            const double x = dl_[c][(size_t) (t2 & mask)];
            const double ra = rr_[c][(size_t) ((t2 + aResI) & (rsize_ - 1))], rb = rr_[c][(size_t) ((t2 + aResI + 1) & (rsize_ - 1))];
            const double rIn = ra + (rb - ra) * aResFr;
            const double rLow = lpR_[c].process (rIn); // lines up with the residual at t2
            const double xa = dl_[c][(size_t) ((t2 + aResI) & mask)], xb = dl_[c][(size_t) ((t2 + aResI + 1) & mask)];
            const double xLow = lpX_[c].process (xa + (xb - xa) * aResFr); // everything below Range, lined up the same way
            const double lowChange = gCon * rLow + dk * (vC_ * rLow + (1.0 - vC_) * xLow);
            const double lowOut = (x - rr_[c][k2]) + dr_[c][k2] + rLow + lowChange; // note + residual below Range, after the changes
            out[c] = x + dr_[c][k2] + lowChange + (gD - 1.0) * lowOut;
            rOutM += (rr_[c][k2] + gCon * rLow + dk * vC_ * rLow) / nCh;
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
        const double rr = (double) std::min (rampPos_ + 1, kSub) / kSub;
        ++rampPos_;
        auto glide = [rr] (double a, double b) { return a + (b - a) * rr; };
        vC_ = glide (vP_, v_);
        grC_ = glide (grP_, gr_);
        attC_ = glide (attP_, attW_);
        double gC[kH];
        for (int h = 0; h < kH; ++h)
            gC[h] = glide (gP_[h], g_[h]);
        const double P = sr_ / (double) f0r_[k3];
        const Cx z = zr_[k3];
        Cx zp[kH];
        zp[0] = z;
        for (int h = 1; h < kH; ++h)
            zp[h] = zp[h - 1] * z;
        const double repAmt = glide (repAP_, repA_), ar = glide (arP_, ar_);
        const double rep = std::min (1.0, 2.0 * repAmt) * glide (repP_, repW_), tr = glide (trP_, tr_);
        const bool active = vC_ > 0.0;
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
                    co[h] = cs[h] * gC[h];
                if (rep > 0.0)
                    co[0] = (cs[0] * (1.0 - rep) + c1t_ * rep) * gC[0];
                if (tr > 0.0)
                {
                    // harmonics 2-4 kept at least 6, 9, 12 dB below the fundamental (raised by up to 9 dB above 100 %),
                    // generated phase-locked to it if absent
                    const double m1 = mag (co[0]);
                    const Cx u1 = m1 > 1e-12 ? co[0] * (1.0 / m1) : Cx { 1.0, 0.0 };
                    Cx uh = u1;
                    for (int h = 1; h <= 3; ++h)
                    {
                        uh = uh * u1; // e^{j (h+1) arg c1}
                        const double T = m1 * std::pow (10.0, (-(6.0 + 3.0 * (h - 1)) + 9.0 * std::max (0.0, tr - 1.0)) / 20.0);
                        const double m = mag (co[h]);
                        if (m < T)
                        {
                            const double target = m + std::min (1.0, tr) * (T - m);
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
            dr_[c][k3] = vC_ * (sumPo - sumP);
            pM += sumP / nCh;
            rM += r / nCh;
        }
        tapPr_[k3] = pM;
        tapRr_[k3] = rM;
    }

    Params prm_;
    double sr_ = 48000.0;
    int64_t hopIn_ = 200, W_ = 3400, nIn_ = 0;
    bass::PitchTracker trk_;
    bass::EnvelopeShaper shaper_;
    bass::Lr4 lpR_[2], lpX_[2], lpK_;
    std::vector<float> kpow_;
    double pk_ = 0.0, ak_ = 0.01, scSame_ = 0.0, duckDb_ = 0.0, gDyn_ = 1.0, gDynP_ = 1.0, gDs_ = 1.0, aDyn_ = 0.05, aDuckAtt_ = 0.01, aDuckRel_ = 0.001, dynDbT_ = 0.0;
    int rampB_ = 0;

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

    double v_ = 0.0, gr_ = 1.0, g_[kH] {}, ref_[kH] {}, relSm_[kH] {};
    Cx c1s_, c1t_, cMono_[kH], cOutMono_[kH];
    int aMax_ = 300;
    double aResF_ = 0.0, rangeSm_ = 300.0, repA_ = 0.0, repAP_ = 0.0, tr_ = 0.0, trP_ = 0.0, ar_ = 0.001, arP_ = 0.001, aPar_ = 0.01;
    bool fresh_ = true;
    double repW_ = 1.0, vP_ = 0.0, grP_ = 1.0, attP_ = 1.0, repP_ = 1.0, gP_[kH] {}, vC_ = 0.0, grC_ = 1.0, attC_ = 1.0;
    int rampPos_ = 0;
    std::vector<double> dr_[2], rr_[2], tapPr_, tapRr_;
    double attW_ = 1.0;
    double rangeApplied_ = -1.0;
    float pitchOut_ = 0.0f, inDb_[kH] {}, outDb_[kH] {};
    double partEn_ = 0.0, resEn_ = 0.0, resOutEn_ = 0.0, tapP_ = 0.0, tapR_ = 0.0;
};
} // namespace nsp
