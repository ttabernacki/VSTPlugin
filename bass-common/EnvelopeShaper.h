#pragma once

// Punch and sustain: a gain that follows the note's envelope (no JUCE), shared by the bass plug-ins.
//
// Input side (analyse(), once per kSub samples): attacks come from the tracker's onset detector, and the body's decay
// rate from three envelopes of the low band (fast/medium/long). The sustain gain is written into a history.
// Output side (gainDb(), once per kSub samples of *output* time): the history is read for the audio that is leaving the
// delay line now, a few ms ahead of the detector's own lag, and a smooth pulse (3 ms rise, 28 ms fall) is placed on every
// attack the tracker found, so the gain is in the right place although the detector fired late.
//   Punch    -1..1  tame .. emphasise the attack (up to +-10 dB)
//   Sustain  -1..1  shorten .. lengthen the body (up to +-12 dB, only while the note is really decaying)

#include "PitchTracker.h"

namespace bass
{
class EnvelopeShaper
{
public:
    static constexpr int kSub = PitchTracker::kSub;

    void prepare (double sampleRate, int latencySamples)
    {
        sr_ = sampleRate;
        latBlk_ = latencySamples / kSub;
        antBlk_ = std::max (1, (int) std::lround (0.004 * sr_ / kSub));
        hist_.assign (kHist, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (hist_.begin(), hist_.end(), 0.0f);
        gs_ = gOut_ = dPeak_ = 0.0;
        seenOnset_ = -1000000;
        nOn_ = head_ = 0;
        times_.fill (-1000000000LL);
        amps_.fill (0.0f);
        inOnset_ = false;
    }

    // input time nIn (a multiple of kSub), after tracker.control(), before the tracker has seen the sample nIn
    void analyse (int64_t nIn, const PitchTracker& t, double sustain)
    {
        const int64_t blk = nIn / kSub;
        const double lvl = 10.0 * std::log10 (t.slowPower() + 1e-12);
        const double wgt = std::clamp ((lvl + 72.0) / 12.0, 0.0, 1.0);
        const double d = 10.0 * std::log10 ((t.fastPower() + 1e-12) / (t.slowPower() + 1e-12)) * wgt;
        if (t.lastOnset() != seenOnset_ && t.onsetCount() > 0)
        {
            seenOnset_ = t.lastOnset();
            times_[(size_t) (head_ & 31)] = t.newestOnsetTime();
            amps_[(size_t) (head_ & 31)] = 0.0f;
            ++head_;
            nOn_ = std::min (nOn_ + 1, 32);
            dPeak_ = 0.0;
            // the last note's body gain fades out into this attack instead of reaching into it
            const int64_t bs = std::max<int64_t> (0, (t.newestOnsetTime() - (int64_t) (0.003 * sr_)) / kSub);
            if (bs < blk)
            {
                const float gv = hist_[(size_t) (bs & (kHist - 1))];
                for (int64_t b = bs; b < blk; ++b)
                    hist_[(size_t) (b & (kHist - 1))] = gv * (1.0f - (float) (b - bs) / (float) (blk - bs));
            }
            gs_ = 0.0;
            inOnset_ = true;
        }
        if (t.inOnset())
        {
            dPeak_ = std::max (dPeak_, d);
            amps_[(size_t) ((head_ - 1) & 31)] = (float) std::clamp (dPeak_ / 6.0, 0.4, 1.0);
        }
        inOnset_ = t.inOnset();

        // body: how fast is the note dying (a natural ring-out falls at about 0.3-1.5; faster is a mute or a wobble)
        const double wl = std::clamp ((10.0 * std::log10 (t.longPower() + 1e-12) + 72.0) / 12.0, 0.0, 1.0);
        const double decRaw = std::max (-10.0 * std::log10 ((t.mediumPower() + 1e-12) / (t.longPower() + 1e-12)) * wl, 0.0);
        const double dec = decRaw / (1.0 + (decRaw / 1.5) * (decRaw / 1.5));
        const double wf = std::clamp ((10.0 * std::log10 (t.fastPower() + 1e-12) + 62.0) / 8.0, 0.0, 1.0); // note over: let go
        const double since = (double) (nIn - t.lastOnset()) / sr_;
        const double wb = std::clamp ((since - 0.03) / 0.03, 0.0, 1.0); // the body starts after the attack
        const double raw = inOnset_ ? 0.0 : sustain * kS * dec * wf * wb;
        gs_ += (1.0 - std::exp (-(double) kSub / (0.006 * sr_))) * (raw - gs_);
        hist_[(size_t) (blk & (kHist - 1))] = (float) gs_;
        blkNow_ = blk;
    }

    // dB of gain for the audio at output time tOut (middle of the tick); call once per kSub samples after analyse()
    double gainDb (double punch, int64_t nIn, int64_t tOut)
    {
        double gT = 0.0;
        const int64_t idx = nIn / kSub - latBlk_ + antBlk_;
        if (idx >= 0 && tOut >= 0)
        {
            double pulse = 0.0;
            if (punch != 0.0)
                for (int i = 0; i < nOn_; ++i)
                {
                    const double u = (double) (tOut - times_[(size_t) i]) / sr_;
                    if (u > 0.0 && u < 0.2)
                        pulse += (double) amps_[(size_t) i] * (std::exp (-u / 0.028) - std::exp (-u / 0.003)) / 0.683;
                }
            gT = kCeil * std::tanh ((kPunchDb * punch * pulse + hist_[(size_t) (idx & (kHist - 1))]) / kCeil);
        }
        gOut_ += (1.0 - std::exp (-(double) kSub / (0.0015 * sr_))) * (gT - gOut_);
        return gOut_;
    }

private:
    static constexpr int kHist = 4096;
    static constexpr double kPunchDb = 10.0, kS = 8.0, kCeil = 12.0;
    double sr_ = 48000.0, gs_ = 0.0, gOut_ = 0.0, dPeak_ = 0.0;
    int latBlk_ = 0, antBlk_ = 12, nOn_ = 0, head_ = 0;
    int64_t seenOnset_ = -1000000, blkNow_ = 0;
    bool inOnset_ = false;
    std::vector<float> hist_;
    std::array<int64_t, 32> times_ {};
    std::array<float, 32> amps_ {};
};
} // namespace bass
