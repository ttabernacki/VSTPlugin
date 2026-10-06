#pragma once

// Synthetic plucked bass with per-pitch "room resonances" injected on the fundamental, so the
// leveler can be measured against a known ground truth.

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace synth
{
struct Ev
{
    double t, dur;
    int midi;
    double velDb;
    double glideTo = 0.0;  // 0 = none; else MIDI note (fractional) reached at the end of the note
    double attack = 0.004; // seconds; long + no pick noise = legato (hammer-on / pull-off)
    bool pick = true;
};

inline double hz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }
inline double hz2 (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

inline std::vector<float> render (double fs, const std::vector<Ev>& ev, double total, const std::array<double, 128>& resonanceDb,
                                  unsigned seed = 1)
{
    std::vector<float> out ((size_t) (total * fs) + 1, 0.0f);
    std::mt19937 g (seed);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    static const double harm[6] = { 1.0, 0.55, 0.32, 0.18, 0.10, 0.06 };
    for (const auto& e : ev)
    {
        const double f = hz (e.midi), vel = std::pow (10.0, e.velDb / 20.0) * 0.25;
        const double res = std::pow (10.0, resonanceDb[(size_t) e.midi] / 20.0);
        const size_t n0 = (size_t) (e.t * fs), n = (size_t) (e.dur * fs);
        double phase = u (g) * 3.14159;
        for (size_t i = 0; i < n && n0 + i < out.size(); ++i)
        {
            const double t = (double) i / fs;
            const double fi = e.glideTo > 0.0 ? hz2 (e.midi + (e.glideTo - e.midi) * t / e.dur) : f;
            phase += 2.0 * 3.14159265358979 * fi / fs;
            double env = std::min (1.0, t / e.attack) * std::exp (-t / 0.45);
            const double rel = e.dur - t;
            if (rel < 0.025)
                env *= std::max (0.0, rel / 0.025);
            double s = 0.0;
            for (int h = 1; h <= 6; ++h)
                s += harm[h - 1] * (h == 1 ? res : 1.0) * std::sin (phase * h);
            if (e.pick && t < 0.008)
                s += 0.15 * u (g) * (1.0 - t / 0.008); // pick noise
            out[n0 + i] += (float) (vel * env * s);
        }
    }
    return out;
}

// per-note measurement from a signal, for ground-truth checks: exactly K periods, integer-period DFT
struct NoteMeasure
{
    double balDb, lvlDb;
};

inline NoteMeasure measure (const std::vector<float>& x, double fs, int midi, double startSec)
{
    const double f = hz (midi), period = fs / f;
    const int K = std::max (2, (int) std::ceil (0.050 * f));
    const int L = (int) std::lround (K * period);
    const size_t i0 = (size_t) ((startSec + 0.040) * fs);
    const double w1 = 2.0 * 3.14159265358979 * K / L;
    double amp[5] = {};
    for (int h = 1; h <= 4; ++h)
    {
        double re = 0, im = 0;
        for (int j = 0; j < L && i0 + (size_t) j < x.size(); ++j)
        {
            re += x[i0 + (size_t) j] * std::cos (w1 * h * j);
            im -= x[i0 + (size_t) j] * std::sin (w1 * h * j);
        }
        amp[h] = 2.0 / L * std::sqrt (re * re + im * im);
    }
    const double hs = amp[2] * amp[2] + amp[3] * amp[3] + amp[4] * amp[4];
    NoteMeasure m;
    m.lvlDb = 20.0 * std::log10 (amp[1] + 1e-12);
    m.balDb = m.lvlDb - 10.0 * std::log10 (hs + 1e-18);
    return m;
}
} // namespace synth
