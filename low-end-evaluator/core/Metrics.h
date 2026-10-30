#pragma once

// The metric catalog (what is measured, how it is scored by default) and the playback-system models used by the
// translation metrics. Profiles store targets per metric id; everything else about a metric lives here.

#include "Spectral.h"

#include <string>
#include <vector>

namespace lee
{
enum Category { Clarity = 0, Dynamics = 1, Translation = 2, Gate = -1 };
inline const char* categoryName (int c) { return c == Clarity ? "Clarity" : c == Dynamics ? "Dynamics" : c == Translation ? "Translation" : "Technical"; }

struct MetricInfo
{
    const char* id;
    const char* name;
    const char* unit;
    int category;
    double weight;     // importance within its category (profile can override)
    double bandFloor;  // minimum half-width of the no-penalty band: a measurement or audibility floor, so a tight
    double sigmaFloor; //   reference set does not punish inaudible differences; minimum softness likewise
    double lo, hi;     // physical limits
    bool mixOneSided;  // dynamics: an unmastered mix may be more dynamic than mastered references without penalty
};

inline const std::vector<MetricInfo>& catalog()
{
    static const std::vector<MetricInfo> c = {
        { "B1_20_40", "Band level 20-40 Hz", "dB", Clarity, 0.4, 1.0, 1.5, -80, 0, false },
        { "B1_40_60", "Band level 40-60 Hz", "dB", Clarity, 0.4, 1.0, 1.5, -80, 0, false },
        { "B1_60_90", "Band level 60-90 Hz", "dB", Clarity, 0.4, 1.0, 1.5, -80, 0, false },
        { "B1_90_120", "Band level 90-120 Hz", "dB", Clarity, 0.4, 1.0, 1.5, -80, 0, false },
        { "B1_120_180", "Band level 120-180 Hz", "dB", Clarity, 0.4, 1.0, 1.5, -80, 0, false },
        { "B1_180_250", "Band level 180-250 Hz", "dB", Clarity, 0.4, 1.0, 1.5, -80, 0, false },
        { "B2", "Spectral tilt 30-300 Hz", "dB/oct", Clarity, 1.0, 0.5, 1.0, -30, 30, false },
        { "B3", "Sub-to-bass ratio (20-60 vs 60-120 Hz)", "dB", Clarity, 1.0, 1.0, 1.5, -60, 60, false },
        { "B4a", "Mud vs bass (150-350 vs 60-120 Hz)", "dB", Clarity, 1.0, 1.0, 1.5, -60, 60, false },
        { "B4b", "Mud vs mids (150-350 Hz vs 0.5-2 kHz)", "dB", Clarity, 1.0, 1.0, 1.5, -60, 60, false },
        { "B5", "Resonance prominence", "dB", Clarity, 0.8, 0.75, 1.0, 0, 40, false },
        { "C4", "Harmonic definition (2-5x f0 vs f0)", "dB", Clarity, 1.0, 1.5, 2.0, -60, 40, false },
        { "C5", "Fundamental clarity (pitched share)", "", Clarity, 0.8, 0.05, 0.08, 0, 1, false },
        { "S1", "Mono fold-down loss (worst band < 150 Hz)", "dB", Clarity, 1.5, 0.25, 0.5, -60, 0, false },
        { "S2", "Low-band correlation (5th percentile)", "", Clarity, 1.5, 0.03, 0.08, -1, 1, false },
        { "S3", "Side-to-mid 20-120 Hz", "dB", Clarity, 1.0, 2.0, 3.0, -60, 20, false },
        { "T4", "Infrasonic energy (5-25 vs 25-120 Hz)", "dB", Dynamics, 0.8, 1.5, 2.0, -80, 20, false },
        { "D1", "Low-band crest factor", "dB", Dynamics, 1.0, 0.5, 1.0, 0, 40, true },
        { "D2", "Low-band dynamic range", "dB", Dynamics, 1.0, 1.0, 1.5, 0, 60, true },
        { "D3", "Note-to-note consistency", "dB", Dynamics, 1.0, 0.5, 0.75, 0, 30, false },
        { "D4", "Pitch-level trend", "dB/oct", Dynamics, 0.6, 1.0, 1.5, -30, 30, false },
        { "D6", "Transient-to-sustain", "dB", Dynamics, 0.8, 0.75, 1.0, -30, 30, true },
        { "D7", "Low-band PLR", "dB", Dynamics, 0.8, 0.5, 1.0, 0, 40, true },
        { "X1_phone", "Bass presence: phone", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_laptop", "Bass presence: laptop", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_bluetooth", "Bass presence: Bluetooth speaker", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_earbuds", "Bass presence: earbuds", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_headphones", "Bass presence: headphones", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_car", "Bass presence: car", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_club", "Bass presence: club PA", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X1_monitors", "Bass presence: ported monitors", "dB", Translation, 0.5, 1.0, 1.5, -80, 40, false },
        { "X2", "Missing-fundamental support", "", Translation, 1.0, 0.05, 0.1, 0, 1, false },
        { "X3", "Low-volume balance shift (80 vs 40 phon)", "dB", Translation, 0.5, 0.5, 1.0, -30, 30, false },
        { "X4", "Cross-system spread", "dB", Translation, 1.0, 0.5, 0.75, 0, 40, false },
        // technical checks: gates only, never profiled
        { "T1", "True peak", "dBTP", Gate, 0, 0, 0, -200, 40, false },
        { "T2", "Clipping events", "", Gate, 0, 0, 0, 0, 1e9, false },
        { "T3", "DC offset", "% FS", Gate, 0, 0, 0, 0, 100, false },
    };
    return c;
}

inline const MetricInfo* findMetric (const std::string& id)
{
    for (auto& m : catalog())
        if (id == m.id)
            return &m;
    return nullptr;
}

// Playback models: starting estimates (design doc, section "Translation simulation"), to be replaced by measured data.
// phon = assumed listening level for the equal-loudness weighting; cutoff = below it a fundamental needs harmonic support.
struct PlaybackSystem
{
    const char* id;
    const char* name;
    double phon, cutoffHz;
    Chain model;
};

inline const std::vector<PlaybackSystem>& playbackSystems()
{
    static const std::vector<PlaybackSystem> s = [] {
        const double fs = kAnalysisRate;
        auto hp2 = [&] (double f) { return butterworth (true, 2, f, fs); };
        std::vector<PlaybackSystem> v;
        v.push_back ({ "phone", "phone", 60, 350, hp2 (350) });
        v.push_back ({ "laptop", "laptop", 60, 180, hp2 (180) });
        Chain bt = hp2 (80);
        bt.push_back (Biquad::peak (150, 0.7, 3.0, fs));
        v.push_back ({ "bluetooth", "Bluetooth speaker", 70, 80, bt });
        v.push_back ({ "earbuds", "earbuds", 70, 0, { Biquad::shelf (true, 100, 3.5, fs) } });
        v.push_back ({ "headphones", "headphones", 75, 0, { Biquad::shelf (true, 120, 4.5, fs) } });
        Chain car = hp2 (30);
        car.push_back (Biquad::shelf (true, 80, 6.0, fs));
        v.push_back ({ "car", "car", 80, 30, car });
        v.push_back ({ "club", "club PA", 85, 30, hp2 (30) });
        v.push_back ({ "monitors", "ported monitors", 80, 45, butterworth (true, 4, 45, fs) });
        return v;
    }();
    return s;
}

// ISO 226:2003 equal-loudness contours, 20 Hz - 2.5 kHz. Returns phon - Lp(f, phon): the dB to add to a level at f to get
// its loudness relative to 1 kHz at that listening level.
inline double equalLoudnessWeightDb (double f, double phon)
{
    static const double F[] = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500 };
    static const double af[] = { 0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330, 0.315,
                                 0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244, 0.243, 0.243 };
    static const double Lu[] = { -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1,
                                 -2.0, -1.1, -0.4, 0.0, 0.3, 0.5, 0.0, -2.7, -4.1, -1.0, 1.7 };
    static const double Tf[] = { 78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4,
                                 11.4, 8.6, 6.2, 4.4, 3.0, 2.2, 2.4, 3.5, 1.7, -1.3, -4.2 };
    constexpr int n = 22;
    auto lp = [&] (int i) {
        const double Af = 4.47e-3 * (std::pow (10.0, 0.025 * phon) - 1.15) + std::pow (0.4 * std::pow (10.0, (Tf[i] + Lu[i]) / 10.0 - 9.0), af[i]);
        return 10.0 / af[i] * std::log10 (Af) - Lu[i] + 94.0;
    };
    if (f <= F[0])
        return phon - lp (0);
    if (f >= F[n - 1])
        return phon - lp (n - 1);
    int i = 0;
    while (F[i + 1] < f)
        ++i;
    const double u = std::log (f / F[i]) / std::log (F[i + 1] / F[i]);
    return phon - (lp (i) + u * (lp (i + 1) - lp (i)));
}
} // namespace lee
