#pragma once

// Steelwave: picking patterns, tempo sync and velocity shaping (no JUCE).
//
// A picking pattern plays the strings of whatever is held, one step at a time, at a rate locked to
// the host's tempo: thumb-and-finger rolls for Americana, slow arpeggios and a drifting random pick
// for ambient. Steps fall on the host's grid while it plays, so a held chord rolls in time.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sw
{
enum Pattern { PatOff, PatForward, PatBackward, PatPinch, PatTravis, PatArpUp, PatArpUpDown, PatDrift, kPatterns };

inline const char* patternName (int p)
{
    static const char* n[] = { "Off", "Forward roll", "Backward roll", "Pinch & roll", "Alternating thumb", "Arpeggio up", "Arpeggio up-down", "Drift (ambient)" };
    return n[std::clamp (p, 0, kPatterns - 1)];
}

// step lengths, in beats
constexpr int kPatternRates = 5;
inline double patternBeats (int rate)
{
    static const double b[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
    return b[std::clamp (rate, 0, kPatternRates - 1)];
}
inline const char* patternRateName (int rate)
{
    static const char* n[] = { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet" };
    return n[std::clamp (rate, 0, kPatternRates - 1)];
}

// Which of the n strings held (0 = lowest) a step picks, as a bit mask. rnd: any random number.
inline uint32_t patternStep (int pattern, int step, int n, uint32_t rnd)
{
    if (n <= 0 || pattern <= PatOff)
        return 0;
    const int lo = 0, hi = n - 1, mid = n >= 3 ? n / 2 : hi, lo2 = n >= 3 ? 1 : lo;
    auto bit = [] (int i) { return 1u << i; };
    switch (pattern)
    {
        case PatForward: { const int s[] = { lo, mid, hi }; return bit (s[step % 3]); }            // thumb, index, middle
        case PatBackward: { const int s[] = { hi, mid, lo }; return bit (s[step % 3]); }           // middle, index, thumb
        case PatPinch: { const uint32_t s[] = { bit (lo) | bit (hi), bit (mid), bit (hi), bit (mid) }; return s[step % 4]; }
        case PatTravis: { const uint32_t s[] = { bit (lo) | bit (hi), bit (mid), bit (lo2), bit (hi) }; return s[step % 4]; } // bass alternates
        case PatArpUp: return bit (step % n);
        case PatArpUpDown:
        {
            if (n == 1)
                return bit (0);
            const int period = 2 * n - 2, i = step % period;
            return bit (i < n ? i : period - i);
        }
        case PatDrift:
            // a random string, some steps left out (never the one on the beat)
            if (step % 4 != 0 && (rnd >> 8) % 100 < 30)
                return 0;
            return bit ((int) (rnd % (uint32_t) n));
        default: return 0;
    }
}

// how hard each step is picked, relative to the key's velocity: the beat a little stronger
inline double patternAccent (int pattern, int step)
{
    if (pattern == PatDrift)
        return step % 4 == 0 ? 0.85 : 0.65;
    const int len = pattern == PatForward || pattern == PatBackward ? 3 : 4;
    return step % len == 0 ? 1.0 : 0.8;
}

// --- tempo sync ---------------------------------------------------------------------------
// The nearest musical length (1/32 note to 4 bars) to `sec`, at `bpm`.
inline double syncSeconds (double sec, double bpm)
{
    if (bpm <= 0.0 || sec <= 0.0)
        return sec;
    static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 16.0 };
    const double beat = 60.0 / bpm;
    double best = sec, bestD = 1e9;
    for (double b : beats)
    {
        const double d = std::fabs (std::log (b * beat / sec));
        if (d < bestD)
        {
            bestD = d;
            best = b * beat;
        }
    }
    return best;
}
// a rate (Hz) whose cycle is the nearest musical length
inline double syncRate (double hz, double bpm) { return hz <= 0.0 ? hz : 1.0 / syncSeconds (1.0 / hz, bpm); }

// --- velocity -------------------------------------------------------------------------------
// Keyboards and pads differ: the range a player really reaches (from controller setup) is spread
// over 0..1, then a curve: -1 light (a soft touch gets loud), 0 linear, +1 heavy (it takes a hard hit).
inline double shapeVelocity (double v, double curve, double lo = 0.0, double hi = 1.0)
{
    if (hi - lo < 0.05)
    {
        lo = 0.0;
        hi = 1.0;
    }
    v = std::clamp ((v - lo) / (hi - lo), 0.0, 1.0);
    return std::max (0.02, std::pow (v, std::exp2 (std::clamp (curve, -1.0, 1.0) * 1.25)));
}
} // namespace sw
