#pragma once

// Synthetic stereo "mixes" with known low-end defects: a plucked bass line (the Bass Note Leveler's synth), a pitch-swept
// kick on every beat, and decorrelated band noise standing in for the mids. Each defect knob changes one thing.

#include "../../bass-leveler/tests/synth.h"
#include "../core/Spectral.h"

#include <random>

namespace mix
{
struct Params
{
    unsigned seed = 1;
    double seconds = 16.0;
    double bassDb = 0, kickDb = 0, midDb = 0;
    double velSpreadDb = 1.5; // random per-note level
    double mudDb = 0;         // bell at 220 Hz on the whole mix
    double sideLowDb = -200;  // anti-phase copy of the bass in the side channel, re the bass
    double rumbleDb = -200;   // 12 Hz sine, dBFS
    double driveDb = 0;       // gain into a hard clip at full scale
};

inline lee::Stereo render (const Params& p)
{
    const double fs = lee::kAnalysisRate;
    std::mt19937 g (p.seed);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    // bass: 8th notes at 120 BPM, E1..A2 in a minor-pentatonic line
    static const int scale[] = { 28, 31, 33, 35, 38, 40, 43, 45 };
    std::vector<synth::Ev> ev;
    for (double t = 0.05; t < p.seconds - 0.3; t += 0.25)
        ev.push_back ({ t, 0.22, scale[g() % 8], u (g) * p.velSpreadDb });
    std::array<double, 128> res {};
    const auto bass = synth::render (fs, ev, p.seconds, res, p.seed);
    const size_t n = bass.size();
    lee::Stereo s;
    s.fs = fs;
    s.L.assign (n, 0.0f);
    s.R.assign (n, 0.0f);
    const double gb = std::pow (10.0, p.bassDb / 20), gk = 0.35 * std::pow (10.0, p.kickDb / 20), gm = 0.25 * std::pow (10.0, p.midDb / 20);
    const double gs = std::pow (10.0, p.sideLowDb / 20);
    // mids: two independent noises band-limited to 300 Hz - 3 kHz, loud enough to own 500 Hz - 2 kHz (as in a real mix)
    lee::Chain band = lee::butterworth (true, 4, 300, fs);
    for (auto& b : lee::butterworth (false, 4, 3000, fs))
        band.push_back (b);
    std::vector<float> nL (n), nR (n);
    for (size_t i = 0; i < n; ++i)
    {
        nL[i] = (float) u (g);
        nR[i] = (float) u (g);
    }
    nL = lee::filtered (nL, band);
    nR = lee::filtered (nR, band);
    for (size_t i = 0; i < n; ++i)
    {
        const double b = gb * bass[i];
        s.L[i] = (float) (b + gs * b + gm * nL[i]);
        s.R[i] = (float) (b - gs * b + gm * nR[i]);
    }
    // kick on every beat: 150 -> 48 Hz sweep, 250 ms decay
    for (double t = 0.0; t < p.seconds - 0.5; t += 0.5)
    {
        double ph = 0;
        const size_t i0 = (size_t) (t * fs);
        for (size_t i = 0; i < (size_t) (0.5 * fs) && i0 + i < n; ++i)
        {
            const double tt = (double) i / fs, f = 48 + 102 * std::exp (-tt / 0.03);
            ph += 2 * lee::kPi * f / fs;
            const float k = (float) (gk * std::sin (ph) * std::exp (-tt / 0.25) * std::min (1.0, tt / 0.001));
            s.L[i0 + i] += k;
            s.R[i0 + i] += k;
        }
    }
    if (p.mudDb != 0)
    {
        lee::Chain c { lee::Biquad::peak (220, 1.2, p.mudDb, fs) };
        s.L = lee::filtered (s.L, c);
        s.R = lee::filtered (s.R, c);
    }
    if (p.rumbleDb > -150)
    {
        const double a = std::pow (10.0, p.rumbleDb / 20);
        for (size_t i = 0; i < n; ++i)
        {
            const float r = (float) (a * std::sin (2 * lee::kPi * 12.0 * (double) i / fs));
            s.L[i] += r;
            s.R[i] += r;
        }
    }
    {
        // -6 dB of headroom (peaks near -9 dBTP), then optional drive into a hard clip
        const float gd = 0.5f * (float) std::pow (10.0, p.driveDb / 20);
        for (size_t i = 0; i < n; ++i)
        {
            s.L[i] = std::clamp (s.L[i] * gd, -1.0f, 1.0f);
            s.R[i] = std::clamp (s.R[i] * gd, -1.0f, 1.0f);
        }
    }
    return s;
}
} // namespace mix
