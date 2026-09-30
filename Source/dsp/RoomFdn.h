#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace spat
{
// Small decorrelated stereo room: input diffusion -> pre-delay -> 8-line feedback delay
// network (Householder mix, per-line damping). The two outputs use orthogonal sign
// patterns so they are largely uncorrelated (low interaural correlation aids
// externalisation over headphones).
class RoomFdn
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double r = fs / 48000.0;
        static const int lens48[N] = { 1433, 1777, 2053, 2339, 2677, 3067, 3449, 3889 };
        for (int i = 0; i < N; ++i)
        {
            lens[(size_t) i] = std::max (16, (int) std::lround (lens48[i] * r));
            lines[(size_t) i].assign ((size_t) lens[(size_t) i], 0.0f);
            pos[(size_t) i] = 0;
            damp[(size_t) i] = 0.0f;
        }
        preDelay.assign ((size_t) std::max (1, (int) std::lround (0.012 * fs)), 0.0f);
        preDelayPos = 0;
        ap[0].prepare ((int) std::lround (0.0053 * fs), 0.6f);
        ap[1].prepare ((int) std::lround (0.0079 * fs), 0.6f);
        hpL = hpR = 0.0f;
        hpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 150.0f / (float) fs);
        setDecay (0.6f);
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        std::fill (preDelay.begin(), preDelay.end(), 0.0f);
        for (auto& a : ap)
            a.clear();
        damp.fill (0.0f);
        hpL = hpR = 0.0f;
    }

    // RT60 in seconds
    void setDecay (float rt60)
    {
        rt60 = std::max (0.05f, rt60);
        for (int i = 0; i < N; ++i)
            gain[(size_t) i] = std::pow (10.0f, -3.0f * (float) lens[(size_t) i] / ((float) fs * rt60));
    }

    void process (float x, float& outL, float& outR)
    {
        // pre-delay
        const float pd = preDelay[(size_t) preDelayPos];
        preDelay[(size_t) preDelayPos] = x;
        if (++preDelayPos >= (int) preDelay.size())
            preDelayPos = 0;

        float in = ap[1].process (ap[0].process (pd));

        std::array<float, N> d;
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            d[(size_t) i] = lines[(size_t) i][(size_t) pos[(size_t) i]];
            sum += d[(size_t) i];
        }
        static const float inj[N] = { 1, 1, -1, 1, -1, -1, 1, -1 };
        static const float sL[N] = { 1, -1, 1, -1, 1, -1, 1, -1 };
        static const float sR[N] = { 1, 1, -1, -1, 1, 1, -1, -1 };
        const float mix = 2.0f / (float) N;
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const auto u = (size_t) i;
            float fb = (d[u] - mix * sum) * gain[u];
            damp[u] += 0.65f * (fb - damp[u]); // one-pole low-pass in the loop
            lines[u][(size_t) pos[u]] = damp[u] + inj[i] * in * 0.35f;
            if (++pos[u] >= lens[u])
                pos[u] = 0;
            l += sL[i] * d[u];
            r += sR[i] * d[u];
        }
        const float norm = 0.5f; // ~1/sqrt(N/2)
        l *= norm;
        r *= norm;
        // remove low-frequency build-up
        hpL += hpCoef * (l - hpL);
        hpR += hpCoef * (r - hpR);
        outL = l - hpL;
        outR = r - hpR;
    }

private:
    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0;
        float g = 0.6f;
        void prepare (int len, float gain)
        {
            buf.assign ((size_t) std::max (1, len), 0.0f);
            pos = 0;
            g = gain;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        float process (float x)
        {
            const float b = buf[(size_t) pos];
            const float v = x + g * b;
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size())
                pos = 0;
            return b - g * v;
        }
    };

    double fs = 48000.0;
    std::array<std::vector<float>, N> lines;
    std::array<int, N> lens {}, pos {};
    std::array<float, N> gain {}, damp {};
    std::vector<float> preDelay;
    int preDelayPos = 0;
    Allpass ap[2];
    float hpL = 0.0f, hpR = 0.0f, hpCoef = 0.0f;
};
} // namespace spat
