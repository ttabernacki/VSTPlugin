#pragma once

// FFT, biquads, zero-phase filtering, decimation and Welch auto/cross spectra (no JUCE).

#include "Audio.h"

#include <complex>
#include <vector>

namespace lee
{
using cd = std::complex<double>;

// in-place iterative radix-2 FFT; size must be a power of two
inline void fft (std::vector<cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// RBJ cookbook biquad, transposed direct form II in double
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static Biquad lowpass (double f, double q, double fs)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        return { (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
    }
    static Biquad highpass (double f, double q, double fs)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        return { (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
    }
    static Biquad peak (double f, double q, double gainDb, double fs)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        const double a0 = 1 + al / A;
        return { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 };
    }
    static Biquad shelf (bool low, double f, double gainDb, double fs) // shelf slope S = 1
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * f / fs, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        const double sg = low ? 1.0 : -1.0;
        const double a0 = (A + 1) + sg * (A - 1) * c + sA;
        return { A * ((A + 1) - sg * (A - 1) * c + sA) / a0, sg * 2 * A * ((A - 1) - sg * (A + 1) * c) / a0,
                 A * ((A + 1) - sg * (A - 1) * c - sA) / a0, -sg * 2 * ((A - 1) + sg * (A + 1) * c) / a0,
                 ((A + 1) + sg * (A - 1) * c - sA) / a0 };
    }

    double power (double f, double fs) const // |H(e^jw)|^2
    {
        const cd z = std::polar (1.0, -2 * kPi * f / fs);
        const cd h = (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
        return std::norm (h);
    }
};

using Chain = std::vector<Biquad>;

// Butterworth sections: order 2 or 4
inline Chain butterworth (bool high, int order, double f, double fs)
{
    Chain c;
    if (order == 2)
        c.push_back (high ? Biquad::highpass (f, 0.70710678, fs) : Biquad::lowpass (f, 0.70710678, fs));
    else
        for (double q : { 0.54119610, 1.30656296 })
            c.push_back (high ? Biquad::highpass (f, q, fs) : Biquad::lowpass (f, q, fs));
    return c;
}

inline double chainPower (const Chain& c, double f, double fs)
{
    double p = 1.0;
    for (auto& b : c)
        p *= b.power (f, fs);
    return p;
}

inline void filterInPlace (std::vector<double>& x, const Chain& c)
{
    for (auto& b : c)
    {
        double z1 = 0, z2 = 0;
        for (auto& v : x)
        {
            const double in = v, out = b.b0 * in + z1;
            z1 = b.b1 * in - b.a1 * out + z2;
            z2 = b.b2 * in - b.a2 * out;
            v = out;
        }
    }
}

inline std::vector<float> filtered (const std::vector<float>& x, const Chain& c)
{
    std::vector<double> d (x.begin(), x.end());
    filterInPlace (d, c);
    return std::vector<float> (d.begin(), d.end());
}

// Zero-phase (forward-backward) filtering with odd-reflection padding, so the edges do not ring. The magnitude response is
// the chain's squared.
inline std::vector<double> filtfilt (const std::vector<double>& x, const Chain& c, size_t pad)
{
    if (x.empty())
        return {};
    pad = std::min (pad, x.size() - 1);
    std::vector<double> y;
    y.reserve (x.size() + 2 * pad);
    for (size_t i = pad; i >= 1; --i)
        y.push_back (2 * x[0] - x[i]);
    y.insert (y.end(), x.begin(), x.end());
    for (size_t i = 1; i <= pad; ++i)
        y.push_back (2 * x.back() - x[x.size() - 1 - i]);
    filterInPlace (y, c);
    std::reverse (y.begin(), y.end());
    filterInPlace (y, c);
    std::reverse (y.begin(), y.end());
    return std::vector<double> (y.begin() + (long) pad, y.begin() + (long) (pad + x.size()));
}

// 48 kHz -> 6 kHz: zero-phase 8th-order Butterworth at 2.6 kHz (-0.13 dB at 2 kHz, aliases into 0-2 kHz below -55 dB)
constexpr int kDecim = 8;
inline std::vector<double> decimate (const std::vector<float>& x, double fs)
{
    Chain lp = butterworth (false, 4, 2600.0, fs);
    auto more = butterworth (false, 4, 2600.0, fs);
    lp.insert (lp.end(), more.begin(), more.end());
    const auto y = filtfilt (std::vector<double> (x.begin(), x.end()), lp, 2048);
    std::vector<double> d (y.size() / kDecim);
    for (size_t i = 0; i < d.size(); ++i)
        d[i] = y[i * kDecim];
    return d;
}

// Welch one-sided power spectral densities of two channels and their cross spectrum (Hann, 50 % overlap), in power per Hz,
// so a band's power is the sum of PSD x df over its bins and spectra at different rates compare directly.
struct Spectra
{
    double fs = 0, df = 0;
    std::vector<double> LL, RR, LRre; // Re(L conj(R))

    // band sums over [f1, f2)
    void band (double f1, double f2, double& ll, double& rr, double& lr) const
    {
        ll = rr = lr = 0;
        const size_t k1 = (size_t) std::max (1.0, std::ceil (f1 / df)), k2 = std::min (LL.size(), (size_t) std::ceil (f2 / df));
        for (size_t k = k1; k < k2; ++k)
        {
            ll += LL[k];
            rr += RR[k];
            lr += LRre[k];
        }
        ll *= df;
        rr *= df;
        lr *= df;
    }
    double midPower (double f1, double f2) const // power of (L+R)/2
    {
        double ll, rr, lr;
        band (f1, f2, ll, rr, lr);
        return 0.25 * (ll + rr + 2 * lr);
    }
    double midPsd (size_t k) const { return 0.25 * (LL[k] + RR[k] + 2 * LRre[k]); }
};

inline Spectra welch (const std::vector<double>& L, const std::vector<double>& R, double fs, size_t N)
{
    Spectra s;
    s.fs = fs;
    s.df = fs / (double) N;
    s.LL.assign (N / 2 + 1, 0.0);
    s.RR = s.LL;
    s.LRre = s.LL;
    const size_t n = L.size();
    if (n == 0)
        return s;
    const size_t len = std::min (N, n), hop = std::max<size_t> (1, len / 2);
    std::vector<double> w (len);
    double w2 = 0;
    for (size_t i = 0; i < len; ++i)
    {
        w[i] = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) len);
        w2 += w[i] * w[i];
    }
    std::vector<cd> a (N), b (N);
    int frames = 0;
    for (size_t start = 0; start + len <= n; start += hop)
    {
        std::fill (a.begin(), a.end(), cd {});
        std::fill (b.begin(), b.end(), cd {});
        for (size_t i = 0; i < len; ++i)
        {
            a[i] = L[start + i] * w[i];
            b[i] = R[start + i] * w[i];
        }
        fft (a);
        fft (b);
        for (size_t k = 0; k <= N / 2; ++k)
        {
            s.LL[k] += std::norm (a[k]);
            s.RR[k] += std::norm (b[k]);
            s.LRre[k] += (a[k] * std::conj (b[k])).real();
        }
        ++frames;
    }
    const double scale = 2.0 / (fs * w2 * std::max (1, frames));
    for (size_t k = 0; k <= N / 2; ++k)
    {
        s.LL[k] *= scale;
        s.RR[k] *= scale;
        s.LRre[k] *= scale;
    }
    return s;
}
} // namespace lee
