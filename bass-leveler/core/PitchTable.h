#pragma once

// Per-pitch table of measured note levels, and the correction derived from it.
// Two metrics per note:  [0] "balance" (fundamental relative to its harmonics, dB)
//                        [1] "level"   (absolute fundamental level, dB)
// Header-only, no allocation, no dependencies.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bnl
{
class PitchTable
{
public:
    static constexpr int kPitches = 128; // MIDI note numbers
    static constexpr int kKeep = 24;     // observations remembered per pitch (ring)
    static constexpr float kMaxCorrDb = 18.0f;

    PitchTable() { clear(); }

    void clear()
    {
        std::memset (values, 0, sizeof (values));
        std::memset (counts, 0, sizeof (counts));
        std::memset (median, 0, sizeof (median));
        std::memset (corr, 0, sizeof (corr));
        target_[0] = target_[1] = 0.0f;
        observed_ = 0;
    }

    void add (int pitch, float balanceDb, float levelDb)
    {
        if (pitch < 0 || pitch >= kPitches)
            return;
        const int slot = counts[pitch] % kKeep;
        values[0][pitch][slot] = balanceDb;
        values[1][pitch][slot] = levelDb;
        if (counts[pitch] < 60000)
            ++counts[pitch];
    }

    int count (int pitch) const { return pitch < 0 || pitch >= kPitches ? 0 : std::min<int> (counts[pitch], kKeep); }
    int totalNotes() const
    {
        int s = 0;
        for (int p = 0; p < kPitches; ++p)
            s += counts[p];
        return s;
    }
    int observedPitches() const { return observed_; }
    float measuredMedian (int metric, int pitch) const { return median[metric][pitch]; }
    float target (int metric) const { return target_[metric]; }

    // Recompute medians, the common target level and each pitch's correction (dB, at strength 1).
    void rebuild()
    {
        for (int m = 0; m < 2; ++m)
        {
            float vals[kPitches], weights[kPitches];
            int pitches[kPitches], n = 0;
            for (int p = 0; p < kPitches; ++p)
            {
                const int c = count (p);
                if (c == 0)
                    continue;
                float tmp[kKeep];
                for (int i = 0; i < c; ++i)
                    tmp[i] = values[m][p][i];
                median[m][p] = medianOf (tmp, c);
                vals[n] = median[m][p];
                weights[n] = (float) std::min (c, 4);
                pitches[n++] = p;
            }
            observed_ = n;
            std::memset (corr[m], 0, sizeof (corr[m]));
            if (n == 0)
                continue;

            // target = weighted median of the per-pitch medians: robust against a few outlier pitches
            int order[kPitches];
            for (int i = 0; i < n; ++i)
                order[i] = i;
            std::sort (order, order + n, [&] (int a, int b) { return vals[a] < vals[b]; });
            float total = 0.0f;
            for (int i = 0; i < n; ++i)
                total += weights[i];
            float acc = 0.0f, target = vals[order[n - 1]];
            for (int i = 0; i < n; ++i)
            {
                acc += weights[order[i]];
                if (acc >= 0.5f * total)
                {
                    target = vals[order[i]];
                    break;
                }
            }
            target_[m] = target;

            bool seen[kPitches] = {};
            for (int i = 0; i < n; ++i)
            {
                const int p = pitches[i], c = count (p);
                // fewer observations -> trust the correction a little less
                const float c1 = (target - vals[i]) * (float) c / ((float) c + 0.5f);
                corr[m][p] = std::max (-kMaxCorrDb, std::min (kMaxCorrDb, c1));
                seen[p] = true;
            }
            // unseen pitches between / next to seen ones: interpolate, fade out beyond 3 semitones
            for (int p = 0; p < kPitches; ++p)
            {
                if (seen[p])
                    continue;
                int lo = -1, hi = -1;
                for (int d = 1; d <= 3; ++d)
                {
                    if (lo < 0 && p - d >= 0 && seen[p - d])
                        lo = p - d;
                    if (hi < 0 && p + d < kPitches && seen[p + d])
                        hi = p + d;
                }
                if (lo >= 0 && hi >= 0)
                {
                    const float t = (float) (p - lo) / (float) (hi - lo);
                    corr[m][p] = corr[m][lo] * (1.0f - t) + corr[m][hi] * t;
                }
                else if (lo >= 0)
                    corr[m][p] = 0.5f * corr[m][lo];
                else if (hi >= 0)
                    corr[m][p] = 0.5f * corr[m][hi];
            }
        }
    }

    // Correction in dB (strength 1) at a fractional MIDI pitch.
    float correction (int metric, float midi) const
    {
        if (midi <= 0.0f)
            return corr[metric][0];
        if (midi >= (float) (kPitches - 1))
            return corr[metric][kPitches - 1];
        const int i = (int) std::floor (midi);
        const float f = midi - (float) i;
        return corr[metric][i] * (1.0f - f) + corr[metric][i + 1] * f;
    }

    // ---- persistence (only pitches that have data) ---------------------------------
    void serialise (std::vector<uint8_t>& out) const
    {
        out.clear();
        out.push_back (1); // version
        int n = 0;
        for (int p = 0; p < kPitches; ++p)
            n += count (p) > 0;
        out.push_back ((uint8_t) n);
        for (int p = 0; p < kPitches; ++p)
        {
            const int c = count (p);
            if (c == 0)
                continue;
            out.push_back ((uint8_t) p);
            out.push_back ((uint8_t) c);
            for (int m = 0; m < 2; ++m)
                for (int i = 0; i < c; ++i)
                {
                    uint8_t b[4];
                    std::memcpy (b, &values[m][p][i], 4);
                    out.insert (out.end(), b, b + 4);
                }
        }
    }

    bool deserialise (const uint8_t* data, size_t size)
    {
        clear();
        if (size < 2 || data[0] != 1)
            return false;
        const int n = data[1];
        size_t pos = 2;
        for (int k = 0; k < n; ++k)
        {
            if (pos + 2 > size)
                return false;
            const int p = data[pos++], c = data[pos++];
            if (p >= kPitches || c < 1 || c > kKeep || pos + (size_t) c * 8 > size)
                return false;
            for (int m = 0; m < 2; ++m)
                for (int i = 0; i < c; ++i)
                {
                    float v;
                    std::memcpy (&v, data + pos, 4);
                    pos += 4;
                    if (! std::isfinite (v))
                        return false;
                    values[m][p][i] = v;
                }
            counts[p] = (uint16_t) c;
        }
        rebuild();
        return true;
    }

private:
    static float medianOf (float* a, int n)
    {
        std::sort (a, a + n);
        return (n & 1) ? a[n / 2] : 0.5f * (a[n / 2 - 1] + a[n / 2]);
    }

    float values[2][kPitches][kKeep];
    uint16_t counts[kPitches];
    float median[2][kPitches];
    float corr[2][kPitches];
    float target_[2];
    int observed_ = 0;
};
} // namespace bnl
