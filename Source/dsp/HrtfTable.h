#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace spat
{
// Regular azimuth/elevation grid of minimum-phase HRIRs (two sets: measured, and with
// exaggerated direction cues) plus an ITD table, produced by tools/build_hrtf_table.py. Filters are resampled to the host rate in load().
// Coordinates: azimuth 0 = front, +90 = right; elevation +90 = up.
class HrtfTable
{
public:
    bool load (const void* data, size_t size, double targetRate)
    {
        constexpr size_t headerBytes = 36;
        const auto* p = static_cast<const uint8_t*> (data);
        if (p == nullptr || size < headerBytes || std::memcmp (p, "HRT2", 4) != 0)
            return false;

        uint32_t h[4];
        float f[4];
        std::memcpy (h, p + 4, sizeof h);
        std::memcpy (f, p + 20, sizeof f);
        const int srcRate = (int) h[0], srcTaps = (int) h[1];
        azSteps = (int) h[2];
        elSteps = (int) h[3];
        azStep = f[0];
        elStep = f[1];
        elMin = f[2];
        const float scale = f[3];

        const size_t nFilters = (size_t) azSteps * (size_t) elSteps * 2 * kSets;
        const size_t nItd = (size_t) azSteps * (size_t) elSteps;
        if (srcTaps <= 0 || azSteps <= 0 || elSteps <= 0
            || size < headerBytes + nFilters * (size_t) srcTaps * 2 + nItd * 4)
            return false;

        std::vector<int16_t> raw (nFilters * (size_t) srcTaps);
        std::memcpy (raw.data(), p + headerBytes, raw.size() * 2);
        std::vector<float> itdRaw (nItd);
        std::memcpy (itdRaw.data(), p + headerBytes + raw.size() * 2, nItd * 4);

        const double ratio = targetRate / (double) srcRate;
        taps = ratio == 1.0 ? srcTaps : (int) std::ceil ((double) srcTaps * ratio);
        ir.assign (nFilters * (size_t) taps, 0.0f);
        setStride = (size_t) azSteps * (size_t) elSteps * 2 * (size_t) taps;
        itd.resize (nItd);
        for (size_t i = 0; i < nItd; ++i)
            itd[i] = (float) (itdRaw[i] * ratio);

        std::vector<float> src ((size_t) srcTaps);
        for (size_t fi = 0; fi < nFilters; ++fi)
        {
            for (int k = 0; k < srcTaps; ++k)
                src[(size_t) k] = (float) raw[fi * (size_t) srcTaps + (size_t) k] / scale;
            resample (src.data(), srcTaps, ir.data() + fi * (size_t) taps, taps, ratio);
        }
        return true;
    }

    int numTaps() const { return taps; }
    bool isLoaded() const { return taps > 0; }

    // Bilinear interpolation between the four surrounding grid directions.
    // focus 0 = measured filters, 1 = exaggerated direction cues (linear blend between).
    // Outputs are written time-reversed (ready for a forward dot product against a
    // contiguous input window). itdSamples > 0 means the source is on the right.
    void interpolateReversed (float azDeg, float elDeg, float focus, float* hL, float* hR, float& itdSamples) const
    {
        float a = std::fmod (azDeg, 360.0f);
        if (a < 0.0f)
            a += 360.0f;
        const float fa = a / azStep;
        int a0 = (int) fa;
        const float wa = fa - (float) a0;
        a0 %= azSteps;
        const int a1 = (a0 + 1) % azSteps;

        const float fe = std::clamp ((elDeg - elMin) / elStep, 0.0f, (float) (elSteps - 1));
        int e0 = std::min ((int) fe, elSteps - 1);
        const float we = fe - (float) e0;
        const int e1 = std::min (e0 + 1, elSteps - 1);

        focus = std::clamp (focus, 0.0f, 1.0f);
        const float f0 = 1.0f - focus;
        const float w00 = (1 - wa) * (1 - we), w01 = wa * (1 - we), w10 = (1 - wa) * we, w11 = wa * we;
        for (int ear = 0; ear < 2; ++ear)
        {
            float* out = ear == 0 ? hL : hR;
            const size_t o = (size_t) ear * (size_t) taps;
            for (int k = 0; k < taps; ++k)
                out[taps - 1 - k] = 0.0f;
            for (int set = 0; set < kSets; ++set)
            {
                const float sw = set == 0 ? f0 : focus;
                if (sw <= 0.0f)
                    continue;
                const float* p00 = filter (set, e0, a0) + o;
                const float* p01 = filter (set, e0, a1) + o;
                const float* p10 = filter (set, e1, a0) + o;
                const float* p11 = filter (set, e1, a1) + o;
                const float c00 = sw * w00, c01 = sw * w01, c10 = sw * w10, c11 = sw * w11;
                for (int k = 0; k < taps; ++k)
                    out[taps - 1 - k] += c00 * p00[k] + c01 * p01[k] + c10 * p10[k] + c11 * p11[k];
            }
        }
        itdSamples = w00 * itdAt (e0, a0) + w01 * itdAt (e0, a1) + w10 * itdAt (e1, a0) + w11 * itdAt (e1, a1);
    }

private:
    const float* filter (int set, int e, int a) const
    {
        return ir.data() + (size_t) set * setStride + ((size_t) e * (size_t) azSteps + (size_t) a) * 2 * (size_t) taps;
    }
    float itdAt (int e, int a) const { return itd[(size_t) e * (size_t) azSteps + (size_t) a]; }

    // Windowed-sinc resampler for a short filter (band-limits when downsampling).
    static void resample (const float* in, int nIn, float* out, int nOut, double ratio)
    {
        if (ratio == 1.0)
        {
            std::copy (in, in + nIn, out);
            return;
        }
        const double cut = std::min (1.0, ratio);
        const double halfWidth = 12.0 / cut;
        const double pi = 3.14159265358979323846;
        for (int n = 0; n < nOut; ++n)
        {
            const double t = (double) n / ratio;
            const int k0 = (int) std::ceil (t - halfWidth), k1 = (int) std::floor (t + halfWidth);
            double acc = 0.0;
            for (int k = std::max (0, k0); k <= std::min (nIn - 1, k1); ++k)
            {
                const double x = t - (double) k;
                const double arg = pi * cut * x;
                const double s = std::abs (arg) < 1e-9 ? 1.0 : std::sin (arg) / arg;
                const double w = 0.5 * (1.0 + std::cos (pi * x / halfWidth));
                acc += (double) in[k] * cut * s * w;
            }
            out[n] = (float) acc;
        }
    }

    static constexpr int kSets = 2;
    std::vector<float> ir;
    std::vector<float> itd;
    size_t setStride = 0;
    int taps = 0, azSteps = 0, elSteps = 0;
    float azStep = 5.0f, elStep = 5.0f, elMin = -90.0f;
};
} // namespace spat
