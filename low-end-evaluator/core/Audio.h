#pragma once

// Audio buffers, WAV reading/writing and sample-rate conversion (no JUCE).
//
// Everything downstream runs at 48 kHz: readWav() accepts 16/24/32-bit PCM and 32/64-bit float at any rate, mono or
// more channels (the first two are used), and toAnalysisRate() converts with a windowed-sinc resampler.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace lee
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kAnalysisRate = 48000.0;

struct Stereo
{
    double fs = kAnalysisRate;
    std::vector<float> L, R;
    size_t size() const { return L.size(); }
    double seconds() const { return (double) L.size() / fs; }
};

namespace detail
{
inline uint32_t u32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t u16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
} // namespace detail

inline bool readWav (const std::string& path, Stereo& out, std::string& err)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f)
    {
        err = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> data;
    {
        unsigned char buf[65536];
        size_t n;
        while ((n = std::fread (buf, 1, sizeof buf, f)) > 0)
            data.insert (data.end(), buf, buf + n);
        std::fclose (f);
    }
    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0)
    {
        err = path + ": not a RIFF/WAVE file";
        return false;
    }
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmBytes = 0;
    for (size_t p = 12; p + 8 <= data.size();)
    {
        const unsigned char* ck = data.data() + p;
        const size_t len = detail::u32 (ck + 4);
        const size_t avail = std::min (len, data.size() - p - 8);
        if (std::memcmp (ck, "fmt ", 4) == 0 && avail >= 16)
        {
            format = detail::u16 (ck + 8);
            channels = detail::u16 (ck + 10);
            rate = detail::u32 (ck + 12);
            bits = detail::u16 (ck + 22);
            if (format == 0xFFFE && avail >= 40)
                format = detail::u16 (ck + 32); // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the format tag
        }
        else if (std::memcmp (ck, "data", 4) == 0)
        {
            pcm = ck + 8;
            pcmBytes = avail;
        }
        p += 8 + len + (len & 1);
    }
    if (! pcm || channels < 1 || rate < 8000)
    {
        err = path + ": missing fmt or data chunk";
        return false;
    }
    const bool isFloat = format == 3;
    if (! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64))))
    {
        err = path + ": unsupported sample format (format " + std::to_string (format) + ", " + std::to_string (bits) + " bit)";
        return false;
    }
    const size_t bps = (size_t) bits / 8, frame = bps * (size_t) channels, n = pcmBytes / frame;
    auto sample = [&] (const unsigned char* p) -> float {
        if (isFloat)
        {
            if (bits == 32)
            {
                float v;
                std::memcpy (&v, p, 4);
                return v;
            }
            double v;
            std::memcpy (&v, p, 8);
            return (float) v;
        }
        if (bits == 16)
            return (float) (int16_t) detail::u16 (p) / 32768.0f;
        if (bits == 24)
            return (float) ((int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8) / 8388608.0f;
        return (float) ((double) (int32_t) detail::u32 (p) / 2147483648.0);
    };
    out.fs = rate;
    out.L.resize (n);
    out.R.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char* p = pcm + i * frame;
        out.L[i] = sample (p);
        out.R[i] = channels > 1 ? sample (p + bps) : out.L[i];
    }
    return true;
}

// 32-bit float stereo
inline bool writeWav (const std::string& path, const Stereo& in)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    const uint32_t n = (uint32_t) in.size(), dataBytes = n * 8, rate = (uint32_t) std::lround (in.fs);
    auto w32 = [&] (uint32_t v) { unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { unsigned char b[2] = { (unsigned char) v, (unsigned char) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 (2);
    w32 (rate);
    w32 (rate * 8);
    w16 (8);
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (dataBytes);
    std::vector<float> inter ((size_t) n * 2);
    for (uint32_t i = 0; i < n; ++i)
    {
        inter[2 * (size_t) i] = in.L[i];
        inter[2 * (size_t) i + 1] = in.R[i];
    }
    std::fwrite (inter.data(), sizeof (float), inter.size(), f);
    return std::fclose (f) == 0;
}

// Windowed-sinc (Kaiser, beta 9, 32 zero crossings) sample-rate conversion. The kernel is tabulated, so a 4-minute track
// converts in about a second. The pass band ends at 0.95 x the lower Nyquist.
inline std::vector<float> resample (const std::vector<float>& x, double fsIn, double fsOut)
{
    if (std::fabs (fsIn - fsOut) < 1e-9 || x.empty())
        return x;
    static constexpr int kZc = 32, kRes = 512; // static: MSVC will not let the table lambda use local constants uncaptured
    static const std::vector<float> table = [] {
        auto i0 = [] (double v) {
            double s = 1.0, t = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                t *= (v / (2.0 * k)) * (v / (2.0 * k));
                s += t;
            }
            return s;
        };
        const double beta = 9.0, norm = i0 (beta);
        std::vector<float> t ((size_t) kZc * kRes + 2);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const double u = (double) i / kRes, r = u / kZc;
            const double sinc = u == 0.0 ? 1.0 : std::sin (kPi * u) / (kPi * u);
            t[i] = r >= 1.0 ? 0.0f : (float) (sinc * i0 (beta * std::sqrt (1.0 - r * r)) / norm);
        }
        return t;
    }();
    const double ratio = fsOut / fsIn, cut = std::min (1.0, ratio) * 0.95;
    const double half = kZc / cut; // kernel half-width in input samples
    const size_t nOut = (size_t) std::floor ((double) x.size() * ratio);
    std::vector<float> y (nOut);
    const int64_t n = (int64_t) x.size(); // not long: 32 bits on Windows
    for (size_t i = 0; i < nOut; ++i)
    {
        const double t = (double) i / ratio;
        const int64_t k0 = (int64_t) std::ceil (t - half), k1 = std::min (n - 1, (int64_t) std::floor (t + half));
        double acc = 0.0;
        for (int64_t k = std::max<int64_t> (0, k0); k <= k1; ++k)
        {
            const double u = std::fabs (t - (double) k) * cut * kRes;
            const size_t j = (size_t) u;
            const double fr = u - (double) j;
            acc += x[(size_t) k] * (table[j] + fr * (table[j + 1] - table[j]));
        }
        y[i] = (float) (acc * cut);
    }
    return y;
}

inline Stereo toAnalysisRate (const Stereo& in)
{
    if (std::fabs (in.fs - kAnalysisRate) < 1e-9)
        return in;
    Stereo o;
    o.fs = kAnalysisRate;
    o.L = resample (in.L, in.fs, kAnalysisRate);
    o.R = resample (in.R, in.fs, kAnalysisRate);
    return o;
}
} // namespace lee
