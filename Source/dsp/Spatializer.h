#pragma once

#include "HrtfTable.h"
#include "RoomFdn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace spat
{
struct SpatParams
{
    float azimuthDeg = 0.0f;   // 0 = front, +90 = right, +-180 = behind
    float elevationDeg = 0.0f; // +90 = straight up
    float distance = 0.25f;    // 0..1, mapped exponentially to 0.3 m .. 15 m
    float room = 0.3f;         // 0..1 reverb level
    float decay = 0.4f;        // 0..1 reverb RT60, 0.25 s .. 2 s
    float orbitHz = 0.0f;      // cycles per second added to azimuth (+ = clockwise from above)
    float focus = 0.7f;        // 0..1 how strongly front/back and up/down cues are exaggerated
    float rear = 0.5f;         // 0..1 extra 'behind' cues: HF shadow, 1 kHz band, less direct / more room
    float depth = 0.0f;        // -1 far away .. +1 right at your face; heavily exaggerated distance cues on top of 'distance'
};

// Binaural source positioner: measured HRIRs (min-phase + fractional ITD), distance
// cues (level, air absorption, constant-level room) and smooth automation handling.
class Spatializer
{
public:
    static constexpr int kSub = 32;         // control-rate sub-block
    static constexpr float kBaseDelay = 2.0f; // samples; keeps 4-point interpolation causal

    bool prepare (double sampleRate, int maxBlockSize, const void* tableData, size_t tableSize)
    {
        if (! table.load (tableData, tableSize, sampleRate))
            return false;
        fs = sampleRate;
        maxBlock = std::max (1, maxBlockSize);
        taps = table.numTaps();
        work.assign ((size_t) (taps - 1 + maxBlock), 0.0f);
        for (auto* v : { &hOldL, &hOldR, &hNewL, &hNewR })
            v->assign ((size_t) taps, 0.0f);
        revL.assign (kSub, 0.0f);
        revR.assign (kSub, 0.0f);
        room.prepare (sampleRate);
        reset();
        return true;
    }

    int latencySamples() const { return (int) kBaseDelay; }

    // Direction currently being rendered (after smoothing and orbit); for UI display.
    float heardAzimuth() const { return curAz; }
    float heardElevation() const { return curEl; }

    void reset()
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (auto& d : delayBuf)
            d.fill (0.0f);
        writePos = 0;
        lp = lp2 = 0.0f;
        for (auto* b : { &rearShelf, &rearPeak, &nearLow, &nearHigh })
            b->clear();
        room.reset();
        initialised = false;
    }

    // inR may be null (mono input). Output buffers must not alias the inputs.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const SpatParams& p)
    {
        for (int done = 0; done < n;)
        {
            const int chunk = std::min (n - done, maxBlock);
            processChunk (inL + done, inR != nullptr ? inR + done : nullptr, outL + done, outR + done, chunk, p);
            done += chunk;
        }
    }

private:
    static float dot (const float* a, const float* b, int n)
    {
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;
        int i = 0;
        for (; i + 8 <= n; i += 8)
        {
            s0 += a[i] * b[i];
            s1 += a[i + 1] * b[i + 1];
            s2 += a[i + 2] * b[i + 2];
            s3 += a[i + 3] * b[i + 3];
            s4 += a[i + 4] * b[i + 4];
            s5 += a[i + 5] * b[i + 5];
            s6 += a[i + 6] * b[i + 6];
            s7 += a[i + 7] * b[i + 7];
        }
        for (; i < n; ++i)
            s0 += a[i] * b[i];
        return ((s0 + s1) + (s2 + s3)) + ((s4 + s5) + (s6 + s7));
    }

    // RBJ biquad, transposed direct form II, one state pair per ear
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] = { 0, 0 }, z2[2] = { 0, 0 };
        float process (int ch, float x)
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }
        void setHighShelf (double fs, double f0, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * f0 / fs;
            const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0); // S = 1
            const double sq = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1) - (A - 1) * cw + sq;
            b0 = (float) (A * ((A + 1) + (A - 1) * cw + sq) / a0);
            b1 = (float) (-2 * A * ((A - 1) + (A + 1) * cw) / a0);
            b2 = (float) (A * ((A + 1) + (A - 1) * cw - sq) / a0);
            a1 = (float) (2 * ((A - 1) - (A + 1) * cw) / a0);
            a2 = (float) (((A + 1) - (A - 1) * cw - sq) / a0);
        }
        void setLowShelf (double fs, double f0, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * f0 / fs;
            const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0); // S = 1
            const double sq = 2.0 * std::sqrt (A) * alpha;
            const double a0 = (A + 1) + (A - 1) * cw + sq;
            b0 = (float) (A * ((A + 1) - (A - 1) * cw + sq) / a0);
            b1 = (float) (2 * A * ((A - 1) - (A + 1) * cw) / a0);
            b2 = (float) (A * ((A + 1) - (A - 1) * cw - sq) / a0);
            a1 = (float) (-2 * ((A - 1) + (A + 1) * cw) / a0);
            a2 = (float) (((A + 1) + (A - 1) * cw - sq) / a0);
        }
        void setPeak (double fs, double f0, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * f0 / fs;
            const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            const double a0 = 1 + alpha / A;
            b0 = (float) ((1 + alpha * A) / a0);
            b1 = (float) (-2 * cw / a0);
            b2 = (float) ((1 - alpha * A) / a0);
            a1 = b1;
            a2 = (float) ((1 - alpha / A) / a0);
        }
    };

    static constexpr int kDelayMask = 1023;

    float readDelay (const std::array<float, kDelayMask + 1>& buf, int wp, float d) const
    {
        const int di = (int) d;
        const float t = d - (float) di;
        const int idx = wp - di;
        const float ym1 = buf[(size_t) ((idx + 1) & kDelayMask)];
        const float y0 = buf[(size_t) (idx & kDelayMask)];
        const float y1 = buf[(size_t) ((idx - 1) & kDelayMask)];
        const float y2 = buf[(size_t) ((idx - 2) & kDelayMask)];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n, const SpatParams& p)
    {
        const int T = taps;
        float* w = work.data();
        constexpr float pi = 3.14159265f;
        constexpr float deg2rad = pi / 180.0f;

        for (int off = 0; off < n; off += kSub)
        {
            const int m = std::min (kSub, n - off);
            const float mf = (float) m;

            // ---- control update -------------------------------------------------
            orbitPhase += p.orbitHz * mf / (float) fs;
            orbitPhase -= std::floor (orbitPhase);
            const float azT = (p.azimuthDeg + 360.0f * orbitPhase) * deg2rad;
            const float elT = std::clamp (p.elevationDeg, -90.0f, 90.0f) * deg2rad;
            const float tx = std::cos (elT) * std::sin (azT);
            const float ty = std::cos (elT) * std::cos (azT);
            const float tz = std::sin (elT);

            const float dirA = 1.0f - std::exp (-mf / ((float) fs * 0.02f));
            const float scalarA = 1.0f - std::exp (-mf / ((float) fs * 0.05f));
            if (! initialised)
            {
                sx = tx; sy = ty; sz = tz;
                distSm = p.distance;
                roomSm = p.room;
                focusSm = p.focus;
                rearSm = p.rear;
                depthSm = p.depth;
                gPrev = distanceGain (distanceMetres (distSm));
                lastRt60 = -1.0f;
            }
            else
            {
                // smoothing in vector space: automation jumping across +-180 takes the short way
                sx += dirA * (tx - sx);
                sy += dirA * (ty - sy);
                sz += dirA * (tz - sz);
                distSm += scalarA * (p.distance - distSm);
                roomSm += scalarA * (p.room - roomSm);
                focusSm += scalarA * (p.focus - focusSm);
                rearSm += scalarA * (p.rear - rearSm);
                depthSm += scalarA * (p.depth - depthSm);
            }
            const float len = std::sqrt (sx * sx + sy * sy + sz * sz);
            if (len > 1e-4f)
            {
                curAz = std::atan2 (sx, sy) / deg2rad;
                curEl = std::asin (std::clamp (sz / len, -1.0f, 1.0f)) / deg2rad;
            }

            // how far behind the listener the source is (0 front/sides .. 1 directly behind)
            float behind = len > 1e-4f ? std::clamp (-sy / len, 0.0f, 1.0f) : 0.0f;
            behind = behind * behind * (3.0f - 2.0f * behind);
            const float rearAmt = behind * std::clamp (rearSm, 0.0f, 1.0f);
            if (std::abs (rearAmt - lastRearAmt) > 1e-3f || ! initialised)
            {
                rearShelf.setHighShelf (fs, 4000.0, -10.0 * rearAmt); // pinna shadow above ~4 kHz
                rearPeak.setPeak (fs, 1200.0, 1.0, 3.5 * rearAmt);    // Blauert 'behind' band
                lastRearAmt = rearAmt;
            }

            // depth: exaggerated push (far) / pull (close) on top of the regular distance control
            const float depthC = std::clamp (depthSm, -1.0f, 1.0f);
            const float nearAmt = std::max (0.0f, depthC), farAmt = std::max (0.0f, -depthC);
            if (std::abs (nearAmt - lastNearAmt) > 1e-3f || ! initialised)
            {
                nearLow.setLowShelf (fs, 250.0, 10.0 * nearAmt);   // proximity effect
                nearHigh.setHighShelf (fs, 6000.0, 4.0 * nearAmt); // close sources are more detailed / present
                lastNearAmt = nearAmt;
            }
            // near-field ILD: a close, lateral source is much louder in the near ear than the far ear
            const float lateral = len > 1e-4f ? sx / len : 0.0f; // +1 = hard right
            const float ildDb = nearAmt * std::abs (lateral);
            const float gRightEnd = std::pow (10.0f, (lateral >= 0.0f ? 2.0f * ildDb : -9.0f * ildDb) / 20.0f);
            const float gLeftEnd = std::pow (10.0f, (lateral >= 0.0f ? -9.0f * ildDb : 2.0f * ildDb) / 20.0f);
            if (! initialised)
            {
                gPrevEarL = gLeftEnd;
                gPrevEarR = gRightEnd;
            }

            float itdNew = 0.0f;
            table.interpolateReversed (curAz, curEl, focusSm, hNewL.data(), hNewR.data(), itdNew);
            if (! initialised)
            {
                hOldL = hNewL;
                hOldR = hNewR;
                dPrevL = kBaseDelay + std::max (0.0f, itdNew);
                dPrevR = kBaseDelay + std::max (0.0f, -itdNew);
            }
            const float dNewL = kBaseDelay + std::max (0.0f, itdNew); // source on the right -> left ear later
            const float dNewR = kBaseDelay + std::max (0.0f, -itdNew);

            const float dMetres = distanceMetres (distSm);
            const float depthDb = 4.0f * nearAmt - 20.0f * farAmt;
            const float gEnd = distanceGain (dMetres) * (1.0f - 0.25f * rearAmt) * std::pow (10.0f, depthDb / 20.0f);
            const float fc = std::clamp (20000.0f / ((1.0f + 0.15f * dMetres) * (1.0f + 7.0f * farAmt)), 900.0f, 0.45f * (float) fs);
            const float lpA = 1.0f - std::exp (-2.0f * pi * fc / (float) fs);
            // far depth adds a second pole (12 dB/oct) so extreme distance really loses its top end
            // (crossfaded in, so the neutral / close sound is left untouched)
            const float pole2 = std::min (1.0f, farAmt * 3.0f);

            const float rt60 = 0.25f + 1.75f * std::clamp (p.decay, 0.0f, 1.0f);
            if (std::abs (rt60 - lastRt60) > 1e-3f)
            {
                room.setDecay (rt60);
                lastRt60 = rt60;
            }
            const float wet = roomSm * 0.8f * (1.0f + 0.8f * rearAmt)            // behind: more diffuse
                              * (1.0f - 0.85f * nearAmt) * (1.0f + 3.5f * farAmt); // close: dry, far: mostly room
            initialised = true;

            // ---- pre-process input: distance filter + gain, feed the room ----------
            for (int i = 0; i < m; ++i)
            {
                const float x = inR != nullptr ? 0.5f * (inL[off + i] + inR[off + i]) : inL[off + i];
                float rl, rr;
                room.process (x, rl, rr);
                revL[(size_t) i] = rl * wet;
                revR[(size_t) i] = rr * wet;
                lp += lpA * (x - lp);
                lp2 += lpA * (lp - lp2);
                const float g = gPrev + (gEnd - gPrev) * (float) (i + 1) / mf;
                w[T - 1 + off + i] = (lp + pole2 * (lp2 - lp)) * g;
            }

            // ---- HRIR convolution with crossfade, ITD delay, room mix ---------------
            const bool crossfade = std::abs (curAz - lastAz) > 1e-3f || std::abs (curEl - lastEl) > 1e-3f
                                   || std::abs (dNewL - dPrevL) > 1e-4f || std::abs (focusSm - lastFocus) > 1e-4f;
            for (int i = 0; i < m; ++i)
            {
                const float* win = w + off + i;
                float yl = dot (hNewL.data(), win, T);
                float yr = dot (hNewR.data(), win, T);
                if (crossfade)
                {
                    const float t = (float) (i + 1) / mf;
                    yl = t * yl + (1.0f - t) * dot (hOldL.data(), win, T);
                    yr = t * yr + (1.0f - t) * dot (hOldR.data(), win, T);
                }
                yl = nearHigh.process (0, nearLow.process (0, rearPeak.process (0, rearShelf.process (0, yl))));
                yr = nearHigh.process (1, nearLow.process (1, rearPeak.process (1, rearShelf.process (1, yr))));
                {
                    const float te = (float) (i + 1) / mf;
                    yl *= gPrevEarL + (gLeftEnd - gPrevEarL) * te;
                    yr *= gPrevEarR + (gRightEnd - gPrevEarR) * te;
                }
                delayBuf[0][(size_t) (writePos & kDelayMask)] = yl;
                delayBuf[1][(size_t) (writePos & kDelayMask)] = yr;
                const float t = (float) (i + 1) / mf;
                const float dl = dPrevL + (dNewL - dPrevL) * t;
                const float dr = dPrevR + (dNewR - dPrevR) * t;
                outL[off + i] = readDelay (delayBuf[0], writePos, dl) + revL[(size_t) i];
                outR[off + i] = readDelay (delayBuf[1], writePos, dr) + revR[(size_t) i];
                ++writePos;
            }

            std::swap (hOldL, hNewL);
            std::swap (hOldR, hNewR);
            dPrevL = dNewL;
            dPrevR = dNewR;
            gPrev = gEnd;
            gPrevEarL = gLeftEnd;
            gPrevEarR = gRightEnd;
            lastAz = curAz;
            lastEl = curEl;
            lastFocus = focusSm;
        }

        std::memmove (w, w + n, (size_t) (T - 1) * sizeof (float));
    }

    static float distanceMetres (float d01) { return 0.3f * std::pow (50.0f, std::clamp (d01, 0.0f, 1.0f)); }
    static float distanceGain (float metres) { return 1.0f / (0.5f + 0.5f * metres); }

    HrtfTable table;
    Biquad rearShelf, rearPeak, nearLow, nearHigh;
    float rearSm = 0.5f, lastRearAmt = 0.0f, depthSm = 0.0f, lastNearAmt = 0.0f;
    float gPrevEarL = 1.0f, gPrevEarR = 1.0f;
    RoomFdn room;
    double fs = 48000.0;
    int maxBlock = 512, taps = 0;

    std::vector<float> work, hOldL, hOldR, hNewL, hNewR, revL, revR;
    std::array<std::array<float, kDelayMask + 1>, 2> delayBuf {};
    int writePos = 0;

    bool initialised = false;
    float sx = 0, sy = 1, sz = 0, curAz = 0, curEl = 0, lastAz = 0, lastEl = 0, lastFocus = 0.7f;
    float distSm = 0.25f, roomSm = 0.3f, focusSm = 0.7f, gPrev = 1.0f, lp = 0.0f, lp2 = 0.0f, lastRt60 = -1.0f;
    float dPrevL = kBaseDelay, dPrevR = kBaseDelay, orbitPhase = 0.0f;
};
} // namespace spat
