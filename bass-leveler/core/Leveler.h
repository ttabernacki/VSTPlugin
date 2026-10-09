#pragma once

// Bass Note Leveler - DSP core (header-only, C++17, no dependencies).
//
// Evens out per-note level on a monophonic bass line, automatically. Each note is measured while it
// is still inside the look-ahead delay, compared with the typical level of the recent notes, and
// corrected by a narrow bell filter on its own fundamental - before its first sample is heard.
//
//   input --> [analysis path: low-pass + decimate -> YIN pitch tracker -> onset detector ->
//              note segmentation -> per-note level -> running reference -> soft-knee correction]
//   input --> [look-ahead delay] --> [two crossfading bell voices at the tracked pitch] --> output
//
// The correction curve is smooth everywhere (small dead zone around "already even", ease-in, smooth
// ceilings) and the gain moves with attack / release times, so nothing ever hits a hard limit.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bnl
{
constexpr double kPi = 3.14159265358979323846;

struct Params
{
    float amount = 0.6f;       // 0..1   how much of each note's distance from the typical level is corrected
    float maxBoostDb = 6.0f;   // soft ceilings: the correction eases toward them, it never stops dead
    float maxCutDb = 9.0f;
    float speedMs = 30.0f;     // how fast the gain moves (release; attack is about 40% of this)
    float bellQ = 2.5f;
    float voicedThreshold = 0.70f;
};

struct NoteInfo
{
    double startSec;
    float midi, levelDb, targetDb, corrDb;
};

// compact record of a recent note, for displays
struct RecentNote
{
    float midi = 0.0f, deviationDb = 0.0f, correctionDb = 0.0f;
};

class Leveler
{
public:
    static constexpr int kSub = 16; // control block (samples)

    void prepare (double sampleRate)
    {
        sr_ = sampleRate;
        fmin_ = 28.0f;
        fmax_ = 420.0f;
        D_ = std::max (1, (int) std::floor (sr_ / 5000.0));
        fsd_ = sr_ / D_;
        tauMax_ = (int) std::ceil (fsd_ / fmin_) + 2;
        tauMin_ = std::max (4, (int) std::floor (fsd_ / fmax_));
        N_ = 2 * tauMax_;
        hopD_ = std::max (4, (int) std::lround (0.002 * fsd_));
        hopIn_ = (int64_t) hopD_ * D_;
        nIn_win_ = (int64_t) N_ * D_;
        // The note must be measured before its first sample leaves the delay line:
        // pitch window + measurement span + onset-detection slack.
        lat_ = (int) (nIn_win_ + std::llround (0.060 * sr_));
        dsize_ = 1;
        while (dsize_ < lat_ + 4096)
            dsize_ <<= 1;
        for (auto& d : delay_)
            d.assign ((size_t) dsize_, 0.0f);
        win_.assign ((size_t) N_, 0.0f);
        diff_.assign ((size_t) tauMax_ + 2, 0.0f);
        cmnd_.assign ((size_t) tauMax_ + 2, 0.0f);
        dbuf_.assign (4096, 0.0f);
        boxLen_ = std::max (8, (int) std::lround (0.025 * fsd_));
        designDecimator();
        reset();
    }

    void reset()
    {
        for (auto& d : delay_)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (dbuf_.begin(), dbuf_.end(), 0.0f);
        wr_ = 0;
        nIn_ = 0;
        m_ = 0;
        for (auto& s : lp_)
            s = {};
        frames_.fill (Frame {});
        frameCount_ = 0;
        notes_.fill (Note {});
        noteHead_ = -1;
        onsetN_ = 0;
        boxSum_ = 0.0;
        boxBuf_.fill (0.0f);
        envHist_.fill (-120.0f);
        envN_ = 0;
        armed_ = true;
        peakDb_ = valleyDb_ = -120.0f;
        lastOnsetM_ = -1000000;
        unvoicedRun_ = 0;
        candRun_ = 0;
        lastMidi_ = candMidi_ = 0.0f;
        histN_ = 0;
        hist_.fill (0.0f);
        for (auto& v : v_)
            v = Voice {};
        cur_ = 0;
        voiceKey_ = -2;
        recentN_ = 0;
        recent_.fill (RecentNote {});
        notesLog.clear();
    }

    int latencySamples() const { return lat_; }
    void setParams (const Params& p) { prm_ = p; }
    float currentGainDb() const { return v_[cur_].gain; }
    float currentPitchHz() const { return v_[cur_].freq; }
    float voiceGainDb (int voice) const { return v_[voice & 1].gain; } // each bell voice's own gain (for tests)

    // Correction (dB) for a note whose level is devDb BELOW (+) or ABOVE (-) the typical level.
    // Smooth everywhere: a small dead zone (a note within about 1 dB of typical is barely touched), a
    // gentle ease-in, then a soft ceiling. There is no corner where the correction suddenly stops.
    static float softCorrection (float devDb, float amount, float maxBoostDb, float maxCutDb)
    {
        const float ceiling = devDb >= 0.0f ? maxBoostDb : maxCutDb;
        if (ceiling < 0.05f || amount <= 0.0f)
            return 0.0f;
        constexpr float kDead = 1.0f;
        const float a = std::fabs (devDb);
        const float eased = devDb * a / (a + kDead);
        return ceiling * std::tanh (amount * eased / ceiling);
    }

    // The most recent notes, oldest first (for displays). Returns how many were copied.
    int copyRecent (RecentNote* out, int maxCount) const
    {
        const int n = std::min (std::min (recentN_, (int) recent_.size()), maxCount);
        for (int i = 0; i < n; ++i)
            out[i] = recent_[(size_t) ((recentN_ - n + i) % (int) recent_.size())];
        return n;
    }

    bool logNotes = false;
    std::vector<NoteInfo> notesLog;
    std::vector<double> onsetLog; // seconds, when logNotes is set

    // In-place processing of one or two channels. The result does not depend on how the host
    // splits the audio into blocks: control updates happen at fixed absolute sample positions.
    void process (float* const* ch, int nCh, int n)
    {
        nCh = std::min (nCh, 2);
        for (int i = 0; i < n; ++i)
        {
            float mono = nCh == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            if (! std::isfinite (mono))
                mono = 0.0f; // never let NaN / inf into the analysis state
            for (int c = 0; c < nCh; ++c)
                delay_[c][(size_t) wr_] = std::isfinite (ch[c][i]) ? ch[c][i] : 0.0f;
            analyse (mono);
            wr_ = (wr_ + 1) & (dsize_ - 1);
            if ((nIn_ & (kSub - 1)) == 0)
            {
                updateControl();
                rampPos_ = 0;
            }
            // the bell coefficients glide to their new values over the control block: stepping them every kSub samples is
            // a zipper (a click train at sr/16) on top of the bass
            {
                const double r = (double) std::min (rampPos_ + 1, kSub) / kSub;
                ++rampPos_;
                for (auto& v : v_)
                {
                    auto mix = [r] (double a, double b) { return a + (b - a) * r; };
                    v.s1.a1 = mix (v.s1p.a1, v.s1n.a1);
                    v.s1.a2 = mix (v.s1p.a2, v.s1n.a2);
                    v.s1.a3 = mix (v.s1p.a3, v.s1n.a3);
                    v.m1 = mix (v.m1p, v.m1n);
                }
            }
            for (int c = 0; c < nCh; ++c)
            {
                float x = delay_[c][(size_t) ((wr_ - 1 - lat_) & (dsize_ - 1))];
                double y = x;
                for (auto& v : v_)
                {
                    if (! v.active)
                    {
                        v.st[c] = {};
                        continue;
                    }
                    y = bellStep (v.s1, v.st[c], v.m1, y);
                    if (std::fabs (v.st[c].z1) < 1e-20)
                        v.st[c].z1 = 0;
                    if (std::fabs (v.st[c].z2) < 1e-20)
                        v.st[c].z2 = 0;
                }
                ch[c][i] = (float) y;
            }
        }
    }

private:
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
    };
    struct State
    {
        double z1 = 0, z2 = 0;
    };
    struct Frame
    {
        float f0 = 0.0f, conf = 0.0f;
        int64_t centre = 0;
    };
    struct Note
    {
        int64_t start = 0, lastVoiced = 0;
        bool done = false, known = false;
        float hz = 0.0f, midi = 0.0f, level = 0.0f, target = 0.0f, corr = 0.0f;
        int n = 0;
        std::array<float, 24> aMidi {}, aLvl {}, aConf {};
    };
    struct Measure
    {
        float lvl = 0.0f;
        int lenIn = 0; // measurement window length, input samples
        bool ok = false;
    };

    // ---------------- analysis path ------------------------------------------------------
    void designDecimator()
    {
        const double fc = std::min (0.42 * fsd_, 1500.0);
        const double q[2] = { 0.5411961, 1.3065630 }; // 4th-order Butterworth as two biquads
        for (int s = 0; s < 2; ++s)
        {
            const double w = 2.0 * kPi * fc / sr_, cw = std::cos (w), al = std::sin (w) / (2.0 * q[s]), a0 = 1.0 + al;
            lpc_[s] = { (1.0 - cw) / 2.0 / a0, (1.0 - cw) / a0, (1.0 - cw) / 2.0 / a0, -2.0 * cw / a0, (1.0 - al) / a0 };
        }
    }

    void analyse (float x)
    {
        double v = x;
        for (int s = 0; s < 2; ++s)
        {
            const Biquad& c = lpc_[s];
            State& z = lp_[s];
            const double y = c.b0 * v + z.z1;
            z.z1 = c.b1 * v - c.a1 * y + z.z2;
            z.z2 = c.b2 * v - c.a2 * y;
            if (std::fabs (z.z1) < 1e-20)
                z.z1 = 0;
            if (std::fabs (z.z2) < 1e-20)
                z.z2 = 0;
            v = y;
        }
        if (nIn_ % D_ == 0)
            decimated ((float) v);
        ++nIn_;
    }

    void decimated (float x)
    {
        dbuf_[(size_t) (m_ & 4095)] = x;
        // sliding-window RMS (about one period of the lowest notes: no 2f ripple) for onset detection
        const double sq = (double) x * x;
        const size_t bi = (size_t) (m_ % boxLen_);
        boxSum_ += sq - (double) boxBuf_[bi];
        boxBuf_[bi] = (float) sq;
        if (boxSum_ < 0)
            boxSum_ = 0;
        ++m_;
        if (m_ % hopD_ == 0)
        {
            onsetDetect();
            if (m_ >= N_)
                frame();
        }
    }

    // Energy onsets. An onset needs the envelope to rise well above the valley it has fallen to since
    // the last onset (or silence), so ripple and wobble inside a held note cannot retrigger it.
    void onsetDetect()
    {
        const float db = 10.0f * std::log10 ((float) (boxSum_ / boxLen_) + 1e-12f);
        const float before = envN_ >= 4 ? envHist_[(size_t) ((envN_ - 4) & 63)] : -120.0f;
        const int64_t since = m_ - lastOnsetM_;
        if (! armed_)
        {
            peakDb_ = std::max (peakDb_, db);
            if ((db < peakDb_ - 2.0f && since > (int64_t) (0.06 * fsd_)) || since > (int64_t) (0.2 * fsd_))
            {
                armed_ = true;
                valleyDb_ = db;
            }
        }
        else
            valleyDb_ = std::min (valleyDb_, db);
        if (armed_ && envN_ > 8 && db > -62.0f && since > (int64_t) (0.045 * fsd_) && (db - before > 4.0f || db > valleyDb_ + 6.0f))
        {
            lastOnsetM_ = m_;
            armed_ = false;
            peakDb_ = db;
            if (logNotes)
                onsetLog.push_back ((double) (m_ * D_) / sr_);
            if (onsetN_ < (int) onsets_.size())
                onsets_[(size_t) onsetN_++] = (int64_t) m_ * D_ - (int64_t) (0.012 * sr_); // the envelope lags the attack
        }
        envHist_[(size_t) (envN_ & 63)] = db;
        ++envN_;
    }

    // One analysis frame: YIN over the latest N decimated samples + level measurement.
    void frame()
    {
        const int64_t last = m_ - 1;
        double mean = 0.0;
        for (int j = 0; j < N_; ++j)
        {
            win_[(size_t) j] = dbuf_[(size_t) ((last - (N_ - 1) + j) & 4095)];
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
        Measure me;
        fr.centre = (int64_t) (last - N_ / 2) * D_;
        if (std::sqrt (e2 / N_) > 1e-4)
        {
            const int W = tauMax_;
            diff_[0] = 0;
            for (int tau = 1; tau <= tauMax_; ++tau)
            {
                double s = 0;
                for (int j = 0; j < W; ++j)
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
                me = measure (fr.f0);
            }
        }
        frames_[(size_t) (frameCount_ & (kFrames - 1))] = fr;
        if (frameCount_ == 0)
            firstCentre_ = fr.centre;
        ++frameCount_;
        track ((int64_t) last * D_, fr, me);
    }

    // Level of the fundamental over a window of exactly K periods (so nothing leaks into it).
    Measure measure (float f0) const
    {
        Measure r;
        const double period = fsd_ / f0;
        int K = std::max (2, (int) std::ceil (0.040 * f0));
        K = std::min (K, (int) std::floor (N_ / period));
        if (K < 1)
            return r;
        const int L = std::min (N_, (int) std::lround (K * period));
        const double w1 = 2.0 * kPi * K / L;
        const int64_t last = m_ - 1;
        const double c = std::cos (w1), sn = std::sin (w1);
        double pr = 1.0, pi = 0.0, re = 0.0, im = 0.0;
        for (int j = 0; j < L; ++j)
        {
            const double x = dbuf_[(size_t) ((last - (L - 1) + j) & 4095)];
            re += x * pr;
            im -= x * pi;
            const double nr = pr * c - pi * sn;
            pi = pr * sn + pi * c;
            pr = nr;
        }
        const double amp1 = 2.0 / L * std::sqrt (re * re + im * im);
        r.lvl = (float) (20.0 * std::log10 (amp1 + 1e-9));
        r.lenIn = L * D_;
        r.ok = true;
        return r;
    }

    // ---------------- note tracking ------------------------------------------------------
    Note* current() { return noteHead_ >= 0 ? &notes_[(size_t) (noteHead_ % kNotes)] : nullptr; }

    void startNote (int64_t t)
    {
        if (Note* p = current())
            finalise (*p);
        ++noteHead_;
        Note& n = notes_[(size_t) (noteHead_ % kNotes)];
        n = Note {};
        n.start = n.lastVoiced = t;
        candRun_ = 0;
    }

    void track (int64_t tEnd, const Frame& fr, const Measure& me)
    {
        for (int i = 0; i < onsetN_; ++i)
            startNote (onsets_[(size_t) i]);
        onsetN_ = 0;
        const bool voiced = me.ok && fr.conf >= prm_.voicedThreshold && fr.f0 >= fmin_ * 0.95f && fr.f0 <= fmax_ * 1.05f;
        Note* n = current();
        if (! voiced)
        {
            ++unvoicedRun_;
            if (n && ! n->done && tEnd - n->start > 2 * nIn_win_)
                finalise (*n);
            return;
        }
        const float midi = 69.0f + 12.0f * std::log2 (fr.f0 / 440.0f);
        const int gap = unvoicedRun_;
        unvoicedRun_ = 0;
        if (! n)
        {
            startNote (tEnd - nIn_win_);
            n = current();
        }
        else if (n->done && gap >= 4 && tEnd - n->start > (int64_t) (0.06 * sr_) + nIn_win_ / 2)
        {
            startNote (tEnd - nIn_win_); // voiced again after a gap: a new pluck the energy detector missed
            n = current();
        }
        n->lastVoiced = tEnd;

        // A legato change (hammer-on / pull-off) has no energy onset, but the pitch JUMPS and then holds.
        // A slide or bend drifts continuously and must stay one note, so only jumps start a new note.
        if (n->done)
        {
            if (std::fabs (midi - lastMidi_) > 0.5f && std::fabs (midi - n->midi) > 0.7f)
            {
                candMidi_ = midi;
                candRun_ = 1;
            }
            else if (candRun_ > 0 && std::fabs (midi - candMidi_) < 0.25f)
            {
                if (++candRun_ >= 4)
                {
                    startNote (tEnd - nIn_win_ / 2);
                    n = current();
                    candRun_ = 0;
                }
            }
            else
                candRun_ = 0;
        }
        const float prevMidi = lastMidi_;
        lastMidi_ = midi;

        // collect frames whose pitch window and measurement window both lie inside the note
        const int64_t from = n->start + std::max<int64_t> ((int64_t) (0.012 * sr_) + me.lenIn, nIn_win_ - (int64_t) (0.010 * sr_));
        const int64_t span = (int64_t) (0.030 * sr_);
        // Only frames that agree with the previous frame: while a window still straddles the previous
        // note the tracker can wobble (typically by an octave); finalise() also keeps just the frames
        // that are close to the best confidence seen in the note.
        if (! n->done && std::fabs (midi - prevMidi) < 0.3f && tEnd >= from && tEnd <= from + span && n->n < 24)
        {
            n->aConf[(size_t) n->n] = fr.conf;
            n->aMidi[(size_t) n->n] = midi;
            n->aLvl[(size_t) n->n] = me.lvl;
            ++n->n;
        }
        if (! n->done && tEnd > from + span)
            finalise (*n);
    }

    static float medianOf (float* a, int n)
    {
        std::sort (a, a + n);
        return (n & 1) ? a[n / 2] : 0.5f * (a[n / 2 - 1] + a[n / 2]);
    }

    void finalise (Note& n)
    {
        if (n.done)
            return;
        n.done = true;
        if (n.n < 3)
            return;
        // keep the frames whose confidence is near the best one in this note
        float cmax = 0.0f;
        for (int i = 0; i < n.n; ++i)
            cmax = std::max (cmax, n.aConf[(size_t) i]);
        const float cmin = std::max (prm_.voicedThreshold, cmax - 0.10f);
        int k = 0;
        for (int i = 0; i < n.n; ++i)
            if (n.aConf[(size_t) i] >= cmin)
            {
                n.aMidi[(size_t) k] = n.aMidi[(size_t) i];
                n.aLvl[(size_t) k] = n.aLvl[(size_t) i];
                ++k;
            }
        n.n = k;
        if (n.n < 3)
            return;
        float t[24];
        for (int i = 0; i < n.n; ++i)
            t[i] = n.aMidi[(size_t) i];
        const float midi = medianOf (t, n.n);
        int agree = 0;
        for (int i = 0; i < n.n; ++i)
            agree += std::fabs (n.aMidi[(size_t) i] - midi) < 0.4f;
        if (agree * 10 < n.n * 7)
            return; // pitch unstable inside the measurement window
        for (int i = 0; i < n.n; ++i)
            t[i] = n.aLvl[(size_t) i];
        n.level = medianOf (t, n.n);
        if (n.level < -68.0f)
            return; // noise / ring-out rather than a played note
        n.midi = midi;
        n.hz = 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
        n.known = true;

        // The reference is the typical level of the recent notes (their running median), taken BEFORE
        // this note joins them. It needs a few notes first, then is trusted more and more.
        float refVal = n.level;
        const int cnt = std::min (histN_, kHist);
        if (cnt > 0)
        {
            float h[kHist];
            for (int i = 0; i < cnt; ++i)
                h[i] = hist_[(size_t) i];
            refVal = medianOf (h, cnt);
        }
        n.target = refVal;
        const float trust = std::max (0.0f, std::min (1.0f, (float) (histN_ - 2) / 3.0f));
        n.corr = trust * softCorrection (refVal - n.level, prm_.amount, prm_.maxBoostDb, prm_.maxCutDb);
        hist_[(size_t) (histN_ % kHist)] = n.level;
        ++histN_;

        recent_[(size_t) (recentN_ % (int) recent_.size())] = { midi, n.level - refVal, n.corr };
        ++recentN_;
        if (logNotes)
            notesLog.push_back ({ (double) n.start / sr_, midi, n.level, refVal, n.corr });
    }

    // ---------------- control (what the bell should do at the output time) ----------------------------
    // Returns the key of the note that is sounding (or about to: the correction starts 12 ms ahead of the
    // attack, so it is already in place when the note arrives), or -1 when there is nothing to correct.
    int control (int64_t tOut, float& fHz, float& gDb)
    {
        gDb = 0.0f;
        const int64_t tLook = tOut + (int64_t) (0.012 * sr_);
        const Note* found = nullptr;
        int id = -1;
        for (int k = noteHead_; k >= 0 && k > noteHead_ - kNotes; --k)
            if (notes_[(size_t) (k % kNotes)].start <= tLook)
            {
                found = &notes_[(size_t) (k % kNotes)];
                id = k;
                break;
            }
        if (! found || ! found->known)
            return -1;
        const int64_t end = id < noteHead_ ? notes_[(size_t) ((id + 1) % kNotes)].start
                                           : found->lastVoiced + nIn_win_ / 4 + (int64_t) (0.02 * sr_);
        if (tOut >= end)
            return id; // the note has ended: let the gain relax to nothing
        float f = found->hz;
        if (frameCount_ > 0)
        {
            const int64_t k = std::llround ((double) (tOut - firstCentre_) / (double) hopIn_);
            if (k >= 0 && k < frameCount_ && k > frameCount_ - kFrames)
            {
                const Frame& fr = frames_[(size_t) (k & (kFrames - 1))];
                if (fr.conf >= prm_.voicedThreshold)
                {
                    if (std::fabs (12.0f * std::log2 (fr.f0 / f)) >= 3.0f)
                    {
                        fHz = f;
                        return id; // slid too far from where it was measured: ease the correction away
                    }
                    f = fr.f0; // follow slides and bends inside the note
                }
            }
        }
        fHz = f;
        gDb = found->corr;
        return id;
    }

    // ---------------- audio path --------------------------------------------------------
    struct Svf
    {
        double g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0;
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

    static double bellStep (const Svf& s, State& z, double mix, double x)
    {
        const double v3 = x - z.z2;
        const double v1 = s.a1 * z.z1 + s.a2 * v3;
        const double v2 = z.z2 + s.a2 * z.z1 + s.a3 * v3;
        z.z1 = 2.0 * v1 - z.z1;
        z.z2 = 2.0 * v2 - z.z2;
        return x + mix * v1;
    }

    // Every kSub samples: decide gain and bell frequency for the next kSub output samples.
    // Two bell voices take turns: a new note starts on the idle voice while the previous note's voice
    // releases, so a bell never has to jump from one pitch to another while it still has gain.
    void updateControl()
    {
        const int64_t tOut = nIn_ - lat_ + kSub / 2;
        float fTarget = v_[cur_].freq, gTarget = 0.0f;
        const int key = control (tOut, fTarget, gTarget);
        if (key != voiceKey_)
        {
            voiceKey_ = key;
            if (key >= 0)
            {
                cur_ ^= 1;
                v_[cur_].freq = fTarget;
            }
        }
        const float attack = std::max (0.003f, 0.4f * prm_.speedMs * 0.001f);
        const float release = std::max (0.004f, prm_.speedMs * 0.001f);
        const float af = 1.0f - std::exp (-(float) kSub / (0.006f * (float) sr_));
        for (int i = 0; i < 2; ++i)
        {
            Voice& v = v_[i];
            const float target = i == cur_ ? gTarget : 0.0f;
            const float tau = std::fabs (target) > std::fabs (v.gain) ? attack : release;
            v.gain += (1.0f - std::exp (-(float) kSub / (tau * (float) sr_))) * (target - v.gain);
            if (target == 0.0f && std::fabs (v.gain) < 1e-3f)
                v.gain = 0.0f;
            if (i == cur_ && v.gain != 0.0f)
                v.freq = std::exp (std::log (v.freq) + af * (std::log (std::max (20.0f, fTarget)) - std::log (v.freq)));
            v.active = std::fabs (v.gain) > 1e-3f || std::fabs (v.lastGain) > 1e-3f;
            v.lastGain = v.gain;
            const double A = std::pow (10.0, (double) v.gain / 40.0);
            const double q1 = prm_.bellQ;
            v.s1p = v.s1;
            v.m1p = v.m1;
            setSvf (v.s1n, v.freq, sr_, 1.0 / (q1 * A));
            v.m1n = (A - 1.0 / A) / q1;
        }
    }

    static constexpr int kFrames = 1024, kNotes = 64;
    Params prm_;
    double sr_ = 48000.0, fsd_ = 4800.0;
    float fmin_ = 28.0f, fmax_ = 420.0f;
    int D_ = 10, tauMax_ = 175, tauMin_ = 11, N_ = 350, hopD_ = 10, lat_ = 4800, dsize_ = 8192, wr_ = 0, boxLen_ = 120;
    int64_t hopIn_ = 100, nIn_win_ = 3500, nIn_ = 0, m_ = 0, firstCentre_ = 0, frameCount_ = 0;
    std::array<std::vector<float>, 2> delay_;
    std::vector<float> win_, diff_, cmnd_, dbuf_;
    Biquad lpc_[2] {};
    State lp_[2] {};
    std::array<Frame, kFrames> frames_ {};
    std::array<Note, kNotes> notes_ {};
    int noteHead_ = -1;
    std::array<int64_t, 32> onsets_ {};
    int onsetN_ = 0;
    double boxSum_ = 0;
    std::array<float, 4096> boxBuf_ {};
    std::array<float, 64> envHist_ {};
    int envN_ = 0;
    bool armed_ = true;
    float peakDb_ = -120.0f, valleyDb_ = -120.0f;
    int64_t lastOnsetM_ = -1000000;
    int unvoicedRun_ = 0, candRun_ = 0;
    float lastMidi_ = 0.0f, candMidi_ = 0.0f;
    static constexpr int kHist = 24;
    std::array<float, kHist> hist_ {};
    int histN_ = 0;
    struct Voice
    {
        float gain = 0.0f, lastGain = 0.0f, freq = 80.0f;
        bool active = false;
        Svf s1, s1p, s1n; // running coefficients, and at the start and end of the control block
        double m1 = 0, m1p = 0, m1n = 0;
        State st[2] {};
    };
    Voice v_[2];
    int rampPos_ = 0;
    int cur_ = 0, voiceKey_ = -2;
    std::array<RecentNote, 32> recent_ {};
    int recentN_ = 0;
};
} // namespace bnl
