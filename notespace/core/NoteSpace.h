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
        aCorr_ = 1.0 - std::exp (-1.0 / (1.0 * sr_));
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
                Z_[c][h].assign ((size_t) rsize_, Cx {});
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
                std::fill (Z_[c][h].begin(), Z_[c][h].end(), Cx {});
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
        pk_ = scSame_ = scXY_ = scXX_ = scYY_ = duckDb_ = dynDbT_ = 0.0;
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
        zone_ = Zone {};
        zAdv_ = 0;
        nextOn_ = -1;
        for (int i = 0; i < 32; ++i)
            onRaw_[i] = onRef_[i] = -1000000000LL;
        onHead_ = 0;
        endOkFor_ = -1;
        endOkVal_ = false;
        for (int c = 0; c < 2; ++c)
            for (int h = 0; h < kH; ++h)
                csLast_[c][h] = csFrom_[c][h] = Cx {};
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
                // Is the sidechain the bass itself (picked as its own sidechain, often after its fader, so at another level)?
                // Correlation over about a second: a scaled copy gives exactly 1, a kick against a bass line never 0.99.
                scXY_ += aCorr_ * (kin * xm - scXY_);
                scXX_ += aCorr_ * (kin * kin - scXX_);
                scYY_ += aCorr_ * (xm * xm - scYY_);
                const bool same = scXX_ > 1e-12 && scYY_ > 1e-12 && scXY_ > 0.99 * std::sqrt (scXX_ * scYY_);
                scSame_ += ((same ? 1.0 : 0.0) - scSame_) * (1.0 / 256.0);
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
        bool voiced = trk_.pickCentred (t1, f0, pur);
        // A window centred on t1 already hears the next note when its attack is less than half a window ahead: keep the note
        // that is playing until the attack (the split before the attack is measured with this pitch, see split()).
        if (voicedTick_ && stillSounding (t1))
            for (int i = 0; i < trk_.onsetCount(); ++i)
            {
                const int64_t o = trk_.onsetTime (i);
                if (o > t1 && o <= t1 + W_ / 2)
                {
                    voiced = true;
                    f0 = f0Tick_;
                    break;
                }
            }
        if (voiced)
        {
            f0 = std::clamp (f0, 31.0f, 260.0f);
            const bool jump = ! voicedTick_ || std::fabs (12.0 * std::log2 (f0 / std::max (1.0f, f0Tick_))) > 0.7;
            if (jump)
            {
                jumps_[(size_t) (jumpHead_++ & 63)] = t1;
                // a pitch change with no attack near it is still a new note: Sustain must not carry the last one's body into it
                bool attackNear = false;
                for (int i = 0; i < trk_.onsetCount() && ! attackNear; ++i)
                    attackNear = std::llabs (trk_.onsetTime (i) - t1) < W_;
                if (voicedTick_ && ! attackNear)
                    shaper_.noteChange (t1 - W_ / 2, nIn_); // the centred window flips about half a window after the change
            }
            f0Tick_ = f0;
        }
        else if (voicedTick_)
            jumps_[(size_t) (jumpHead_++ & 63)] = t1; // the note ends: the split is not trusted around here either
        voicedTick_ = voiced;
    }

    // An attack at o starts a measurement of the new note with its own pitch, read from the first pitch window that lies
    // entirely after the attack (the look-ahead has it already). The usual measurement takes over again once its four-period
    // window is clear of the attack and of the pitch tracker's own switch.
    bool attackPitch (int64_t o, float& f0) const
    {
        float pur = 0.0f;
        if (! trk_.pickCentred (o + W_ / 2 + hopIn_, f0, pur))
            return false;
        f0 = std::clamp (f0, 31.0f, 260.0f);
        return true;
    }

    // The onset detector fires a few ms into an attack and places the onset a fixed 6.5 ms earlier, which can be late (a slow
    // attack) or early. Here it is moved to where the attack really starts: walking back from the detector's estimate (at most
    // 15 ms) while the level stays within 10 dB of the attack's own first 4 ms. A faint tail of the note before is far below
    // that, so the new note is never started early (which would put it, and anything done to it, before the attack).
    int64_t refinedOnset (int64_t o)
    {
        for (int i = 0; i < 32; ++i)
            if (onRaw_[i] == o)
                return onRef_[i];
        const int mask = dsize_ - 1, seg = std::max (1, (int) (0.001 * sr_)), back = (int) (0.015 * sr_);
        if (o + 14 * seg > nIn_)
            return o; // the audio after the attack has not arrived yet: refine (and remember) it later
        auto pw = [&] (int64_t a, int64_t len) {
            double e = 0.0;
            for (int64_t t = a; t < a + len; ++t)
            {
                const double v = 0.5 * ((double) dl_[0][(size_t) (t & mask)] + (double) dl_[1][(size_t) (t & mask)]);
                e += v * v;
            }
            return e / (double) len;
        };
        int64_t r = o;
        if (o - back - seg >= 0)
        {
            // the attack's level: the loudest 1 ms in the 12 ms after the estimate
            double peak = 0.0;
            for (int64_t a = o; a < o + 12 * seg; a += seg)
                peak = std::max (peak, pw (a, seg));
            // forward from the estimate first (it may be early), then back while the level is still part of the attack
            while (r < o + 12 * seg && pw (r, seg) < 0.1 * peak)
                r += seg;
            if (r == o)
                while (r - seg >= o - back && pw (r - seg, seg) >= 0.1 * peak)
                    r -= seg;
        }
        onRaw_[onHead_ & 31] = o;
        onRef_[onHead_ & 31] = r;
        ++onHead_;
        return r;
    }

    // A jump of the tracked pitch at time j that an attack explains, seen from time t: after the attack the attack-aligned
    // measurement takes care of it; before it, only if the note before is measured over its last two periods (P: its period).
    bool explainedJump (int64_t j, int64_t t, double P)
    {
        for (int i = 0; i < trk_.onsetCount(); ++i)
        {
            const int64_t raw = trk_.onsetTime (i);
            if (j >= raw - 2 * kSub - (int64_t) (0.015 * sr_) && j <= raw + W_ / 2 + 3 * hopIn_)
            {
                const int64_t o = refinedOnset (raw);
                float f = 0.0f;
                if (! attackPitch (o, f))
                    return false;
                return t >= o || endFromS1Ok (o, P);
            }
        }
        return false;
    }

    // the last two periods before the attack at e can be measured with the usual phase (no switch of the tracked pitch in them)
    bool endFromS1Ok (int64_t e, double P)
    {
        if (e == endOkFor_)
            return endOkVal_;
        const int64_t a = e - (int64_t) std::ceil (2.0 * P) - kSub;
        bool ok = vr_[(size_t) (a & (rsize_ - 1))] != 0 && stillSounding (e) && steadyBefore (e, P);
        for (int i = 0; i < 64 && ok; ++i)
            if (jumps_[(size_t) i] >= a && jumps_[(size_t) i] < e)
                ok = false;
        endOkFor_ = e;
        endOkVal_ = ok;
        return ok;
    }

    // Is the note steady over its last two periods before e (the second within 3 dB of the first)? Only then do those two periods
    // stand for the note up to the attack; a note being let go is left alone there, as anywhere else the split is unsure.
    bool steadyBefore (int64_t e, double P) const
    {
        const int mask = dsize_ - 1;
        const int64_t n = std::max<int64_t> (1, (int64_t) P);
        if (e - 2 * n < 0)
            return false;
        auto pw = [&] (int64_t a0, int64_t len) {
            double s2 = 0.0;
            for (int64_t i = a0; i < a0 + len; ++i)
            {
                const double v = 0.5 * ((double) dl_[0][(size_t) (i & mask)] + (double) dl_[1][(size_t) (i & mask)]);
                s2 += v * v;
            }
            return s2 / (double) len;
        };
        const double p1 = pw (e - 2 * n, n), p2 = pw (e - n, n);
        return p1 > 1e-12 && p2 > 0.5 * p1 && p2 < 2.0 * p1;
    }

    // Is the note still sounding at t (its last 8 ms not more than 9 dB under the 32 ms before, and above -70 dBFS)? A note that
    // was let go before the next attack has ended: neither held nor measured up to the attack.
    bool stillSounding (int64_t t) const
    {
        const int mask = dsize_ - 1;
        const int64_t n1 = std::max<int64_t> (1, (int64_t) (0.008 * sr_)), n2 = std::max<int64_t> (1, (int64_t) (0.032 * sr_));
        if (t - n1 - n2 < 0)
            return false;
        auto pw = [&] (int64_t a, int64_t len) {
            double e = 0.0;
            for (int64_t i = a; i < a + len; ++i)
            {
                const double v = 0.5 * ((double) dl_[0][(size_t) (i & mask)] + (double) dl_[1][(size_t) (i & mask)]);
                e += v * v;
            }
            return e / (double) len;
        };
        const double recent = pw (t - n1, n1), before = pw (t - n1 - n2, n2);
        return recent > 1e-7 && recent > 0.125 * before;
    }

    void startZone (int nCh, int64_t o)
    {
        float f = 0.0f;
        if (! attackPitch (o, f))
        {
            zone_.on = false;
            return;
        }
        zone_.on = true;
        zone_.o = o;
        zone_.f0 = f;
        zone_.P = sr_ / f;
        zone_.w = 2.0 * kPi * f / sr_;
        const Cx z0 = zr_[(size_t) (o & (rsize_ - 1))];
        zone_.th0 = std::atan2 (z0.im, z0.re);
        // the usual measurement is clean again two periods after the tracker has switched to the new pitch (or after the attack)
        int64_t j = o;
        for (int i = 0; i < 64; ++i)
            if (jumps_[(size_t) i] >= o - 2 * kSub && jumps_[(size_t) i] <= o + W_ / 2 + 3 * hopIn_)
                j = std::max (j, jumps_[(size_t) i]);
        zone_.end = j + (int64_t) (2.0 * zone_.P + 0.125 * (double) W_) + kSub;
        zAdv_ = o;
        zPh_ = Cx { std::cos (zone_.th0), std::sin (zone_.th0) };
        zStep_ = Cx { std::cos (zone_.w), std::sin (zone_.w) };
        for (int c = 0; c < 2; ++c)
            for (int h = 0; h < kH; ++h)
                zAcc_[c][h] = Cx {};
        (void) nCh;
    }

    // the zone's heterodyne running sums, up to and including sample `upto`
    void advanceZone (int nCh, int64_t upto)
    {
        const int mask = dsize_ - 1;
        for (; zAdv_ <= upto; ++zAdv_)
        {
            const size_t k = (size_t) (zAdv_ & (rsize_ - 1));
            const Cx zc { zPh_.re, -zPh_.im };
            for (int c = 0; c < nCh; ++c)
            {
                const double x = dl_[c][(size_t) (zAdv_ & mask)];
                Cx zh { 1.0, 0.0 };
                for (int h = 0; h < kH; ++h)
                {
                    zh = zh * zc;
                    zAcc_[c][h] = zAcc_[c][h] + zh * x;
                    Z_[c][h][k] = zAcc_[c][h];
                }
            }
            zPh_ = zPh_ * zStep_;
            if (((zAdv_ - zone_.o) & 1023) == 0)
                zPh_ = zPh_ * (1.0 / mag (zPh_));
        }
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
        kAllP_ = kAll_;
        repAP_ = repA_;
        trP_ = tr_;
        arP_ = ar_;
        for (int h = 0; h < kH; ++h)
            gP_[h] = g_[h];
        rampPos_ = 0;
        const size_t k2 = (size_t) (t2 & (rsize_ - 1));
        const double f0 = f0r_[k2], P = sr_ / f0;
        // the next attack, if one is close: the note before it is measured over its last two periods (see split())
        nextOn_ = -1;
        for (int i = 0; i < trk_.onsetCount(); ++i)
        {
            const int64_t o = refinedOnset (trk_.onsetTime (i));
            if (o >= t2 && o <= t2 + 4 * pMax_ + kSub && (nextOn_ < 0 || o < nextOn_))
                nextOn_ = o;
        }
        bool stable = vr_[k2] != 0;
        // a pitch change at an attack is handled by the attack-aligned measurement; any other one makes the split untrustworthy
        // for about two periods either side
        for (int i = 0; i < 64 && stable; ++i)
            if (std::llabs (jumps_[(size_t) i] - t2) < (int64_t) (2.0 * P + 0.125 * (double) W_) + kSub && ! explainedJump (jumps_[(size_t) i], t2, P))
                stable = false;
        if (zone_.on && t2 + kSub > zone_.o && t2 < zone_.end + (int64_t) zone_.P)
            stable = true; // a note just after its attack, measured from the attack on
        if (zone_.on && nextOn_ >= 0 && nextOn_ - zone_.o < (int64_t) (2.0 * zone_.P) + kSub)
            stable = false; // a note shorter than two periods cannot be measured at all
        if (! zone_.on && nextOn_ >= 0 && vr_[k2] != 0 && t2 >= nextOn_ - (int64_t) (3.0 * P))
            stable = stable || endFromS1Ok (nextOn_, P); // the end of a note, measured over its last two periods
        const double tick = (double) kSub / sr_;
        const double vt = stable ? 1.0 : 0.0;
        v_ += (1.0 - std::exp (-tick / 0.008)) * (vt - v_);
        if (vt == 0.0 && v_ < 1e-4)
            v_ = 0.0;
        if (vt == 1.0 && v_ > 1.0 - 1e-4)
            v_ = 1.0;
        pitchOut_ = stable ? (float) (zone_.on && t2 + kSub > zone_.o && t2 < zone_.end ? zone_.f0 : f0) : 0.0f;

        // attacks are mostly residual (pick, string noise): give them 40 ms before the residual is touched
        int64_t lastOn = -1000000000LL;
        for (int i = 0; i < trk_.onsetCount(); ++i)
        {
            const int64_t o = refinedOnset (trk_.onsetTime (i));
            if (o <= t2 && o > lastOn)
                lastOn = o;
        }
        // (40 ms untouched, then in over the next 40: right at an attack the split is also least exact, and a boost would bring
        // that out)
        attW_ = std::clamp (((double) (t2 - lastOn) - 0.04 * sr_) / (0.04 * sr_), 0.0, 1.0);
        // the kick usually lands on the note's attack, where it needs room most: there the duck takes everything below Range
        // (80 ms, fading out over the next 30 ms); later in the note only the residual
        kAll_ = 1.0 - std::clamp (((double) (t2 - lastOn) - 0.08 * sr_) / (0.03 * sr_), 0.0, 1.0);
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

        // tone lock: each harmonic's share of the note, against its long-term average. Right after an attack the new note is
        // measured from the attack on, so its share is followed quickly (else the last note's correction would sit on it)
        const double sinceOn = (double) (t2 - lastOn) / sr_;
        const double tauRel = sinceOn < 0.04 ? 0.01 : 0.08, tauG = sinceOn < 0.06 ? 0.01 : 0.03;
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
                relSm_[h] = relSm_[h] < -99.0 ? rel : relSm_[h] + (1.0 - std::exp (-tick / tauRel)) * (rel - relSm_[h]);
                if (relSm_[h] > -50.0)
                    ref_[h] = ref_[h] < -99.0 ? relSm_[h] : ref_[h] + (1.0 - std::exp (-tick / 3.0)) * (relSm_[h] - ref_[h]);
            }
            double gDb = 0.0;
            if (stable && loud && relSm_[h] > -45.0 && ref_[h] > -99.0)
                gDb = std::clamp ((double) prm_.toneLock, 0.0, 1.0) * std::clamp (ref_[h] - relSm_[h], -24.0, 24.0);
            if (h == 0)
                gDb += std::clamp ((double) prm_.fundamentalDb, -18.0, 18.0);
            const double gT = std::pow (10.0, gDb / 20.0);
            g_[h] += (1.0 - std::exp (-tick / tauG)) * (gT - g_[h]);
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
        const double kAll = kAllC_, vRes = vC_ * (1.0 - kAll); // the duck on the residual only, or on everything below Range
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
            const double lowChange = gCon * rLow + dk * (vRes * rLow + (1.0 - vRes) * xLow);
            const double lowOut = (x - rr_[c][k2]) + dr_[c][k2] + rLow + lowChange; // note + residual below Range, after the changes
            out[c] = x + dr_[c][k2] + lowChange + (gD - 1.0) * lowOut;
            rOutM += (rr_[c][k2] + gCon * rLow + dk * vRes * rLow) / nCh;
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
        kAllC_ = glide (kAllP_, kAll_);
        double gC[kH];
        for (int h = 0; h < kH; ++h)
            gC[h] = glide (gP_[h], g_[h]);
        if (nextOn_ >= 0 && t3 == nextOn_)
        {
            startZone (nCh, t3); // an attack: measure the new note from here on, with its own pitch
            for (int c = 0; c < 2; ++c)
                for (int h = 0; h < kH; ++h)
                    csFrom_[c][h] = csLast_[c][h];
        }
        if (zone_.on && t3 >= zone_.end + (int64_t) zone_.P)
            zone_.on = false;
        const double P = sr_ / (double) f0r_[k3];
        const Cx z = zr_[k3];
        // Weights of the two attack-aware measurements against the usual four-period one (which straddles an attack):
        //   wz  the note just after its attack (one full weight until the usual measurement is clean again, then a one-period fade)
        //   we  the note just before the next attack: its last two periods (faded in over the period before they start)
        double wz = 0.0, we = 0.0;
        bool zoneEnd = false;
        int64_t za = 0;
        if (zone_.on && t3 >= zone_.o)
        {
            const int64_t lim = nextOn_ > t3 ? nextOn_ : INT64_MAX;
            if (lim != INT64_MAX && lim - zone_.o < (int64_t) (2.0 * zone_.P) + 1)
                wz = 0.0; // too short to measure
            else
            {
                wz = t3 < zone_.end ? 1.0 : std::max (0.0, 1.0 - (double) (t3 - zone_.end) / zone_.P);
                // five two-period boxes, centred on t3 and half a period apart (close to the usual four-period triangle), each
                // kept inside the note: from the attack on, and ending by the next attack
                const double P2 = 2.0 * zone_.P;
                for (int b = 0; b < 5; ++b)
                {
                    double a = (double) t3 + (b - 2) * 0.5 * zone_.P - zone_.P;
                    if (lim != INT64_MAX)
                        a = std::min (a, (double) lim - P2 - 1.0);
                    zBox_[b] = std::max (a, (double) zone_.o);
                }
                za = (int64_t) std::ceil (zBox_[4] + P2) + 2;
                advanceZone (nCh, za);
                if (lim != INT64_MAX && t3 >= lim - (int64_t) (2.0 * zone_.P))
                    wz = 1.0; // up to the next attack the box is the only clean measurement left
            }
        }
        else if (! zone_.on && nextOn_ > t3 && nextOn_ - t3 <= (int64_t) (3.0 * P) && endFromS1Ok (nextOn_, P))
        {
            zoneEnd = true;
            we = std::clamp (1.0 - ((double) (nextOn_ - t3) - 2.0 * P) / P, 0.0, 1.0);
        }
        const double xfade = zone_.on && t3 >= zone_.o ? std::min (1.0, (double) (t3 - zone_.o) / (0.005 * sr_)) : 1.0;
        Cx q { 1.0, 0.0 }; // the zone's phase reference against the usual one
        if (wz > 0.0)
        {
            const double th = zone_.th0 + zone_.w * (double) (t3 - zone_.o);
            q = Cx { std::cos (th), std::sin (th) } * Cx { z.re, -z.im };
        }
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
            Cx qh { 1.0, 0.0 };
            for (int h = 0; h < kH; ++h)
            {
                // second two-period average: the partial's complex envelope (x2 for a real signal)
                cs[h] = (readS (S2_[c][h], (double) t3 + P) - readS (S2_[c][h], (double) t3 - P)) * (1.0 / P);
                if (wz > 0.0)
                {
                    // after an attack: the note itself over two-period boxes, demodulated with its own pitch, in the usual reference
                    qh = qh * q;
                    Cx cz {};
                    for (int b = 0; b < 5; ++b)
                        cz = cz + (readS (Z_[c][h], zBox_[b] + 2.0 * zone_.P) - readS (Z_[c][h], zBox_[b]));
                    cz = cz * (0.2 / zone_.P) * qh;
                    cs[h] = cs[h] * (1.0 - wz) + cz * wz;
                    // over the attack itself, cross from the note that was playing to the new one (switching would click)
                    if (xfade < 1.0)
                        cs[h] = csFrom_[c][h] * (1.0 - xfade) + cs[h] * xfade;
                }
                else if (zoneEnd && we > 0.0)
                {
                    // before an attack: the note's last two periods (the usual average would already hear the next note)
                    const double e = (double) nextOn_;
                    const Cx ce = (readS (S1_[c][h], e) - readS (S1_[c][h], e - 2.0 * P)) * (1.0 / P);
                    cs[h] = cs[h] * (1.0 - we) + ce * we;
                }
                sumP += (cs[h] * zp[h]).re;
                cMono_[h] = cMono_[h] + cs[h] * (1.0 / nCh);
                csLast_[c][h] = cs[h];
            }
            if (c == 0)
            {
                // the fundamental's slow part (two smoothing poles, 60 ms each): whatever beats against it rotates and averages out.
                // Around an attack the measurement switches reference, so there the smoother just follows it
                // (it follows the measurement more and more tightly as the attack-aware measurements take over: no jump either way)
                const double arE = std::max (ar, std::max (wz * xfade, we));
                c1s_ = c1s_ + (cs[0] - c1s_) * arE;
                c1t_ = c1t_ + (c1s_ - c1t_) * arE;
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
                            // (the pull toward the phase-locked direction fades out as the harmonic reaches the floor, so crossing
                            // the floor never turns its phase in a single sample)
                            const Cx dir = co[h] + uh * (0.1 * (T - m));
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
    double pk_ = 0.0, ak_ = 0.01, scSame_ = 0.0, scXY_ = 0.0, scXX_ = 0.0, scYY_ = 0.0, aCorr_ = 2e-5, duckDb_ = 0.0, gDyn_ = 1.0, gDynP_ = 1.0, gDs_ = 1.0, aDyn_ = 0.05, aDuckAtt_ = 0.01, aDuckRel_ = 0.001, dynDbT_ = 0.0;
    int rampB_ = 0;

    int pMax_ = 1600, Lp_ = 4000, lag2_ = 3204, lat_ = 7200, rsize_ = 8192, dsize_ = 16384;
    std::vector<float> dl_[2];
    std::vector<Cx> S1_[2][kH], S2_[2][kH], Z_[2][kH], zr_;
    struct Zone
    {
        bool on = false;
        int64_t o = 0, end = 0;
        double f0 = 55.0, P = 870.0, w = 0.0, th0 = 0.0;
    } zone_;
    Cx zAcc_[2][kH], zPh_ { 1.0, 0.0 }, zStep_ { 1.0, 0.0 };
    int64_t zAdv_ = 0, nextOn_ = -1;
    double zBox_[5] {};
    Cx csLast_[2][kH], csFrom_[2][kH];
    int64_t onRaw_[32] {}, onRef_[32] {};
    int onHead_ = 0;
    int64_t endOkFor_ = -1;
    bool endOkVal_ = false;
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
    double attW_ = 1.0, kAll_ = 0.0, kAllP_ = 0.0, kAllC_ = 0.0;
    double rangeApplied_ = -1.0;
    float pitchOut_ = 0.0f, inDb_[kH] {}, outDb_[kH] {};
    double partEn_ = 0.0, resEn_ = 0.0, resOutEn_ = 0.0, tapP_ = 0.0, tapR_ = 0.0;
};
} // namespace nsp
