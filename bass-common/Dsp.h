#pragma once

// Small DSP building blocks shared by the bass plug-ins (no JUCE).

#include <cmath>

namespace bass
{
constexpr double kPi = 3.14159265358979323846;

struct Cx
{
    double re = 0.0, im = 0.0;
};
inline Cx operator+ (Cx a, Cx b) { return { a.re + b.re, a.im + b.im }; }
inline Cx operator- (Cx a, Cx b) { return { a.re - b.re, a.im - b.im }; }
inline Cx operator* (Cx a, Cx b) { return { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re }; }
inline Cx operator* (Cx a, double s) { return { a.re * s, a.im * s }; }
inline double mag (Cx a) { return std::sqrt (a.re * a.re + a.im * a.im); }

// 4th-order Linkwitz-Riley low-pass: two cascaded TPT state-variable filters.
class Lr4
{
public:
    void setLowpass (double fc, double sr)
    {
        fc = fc < 0.45 * sr ? fc : 0.45 * sr;
        g_ = std::tan (kPi * fc / sr);
        k_ = 1.4142135623730951;
        a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }
    void reset() { z_[0] = z_[1] = St {}; }
    double process (double x)
    {
        double v2 = 0.0;
        for (auto& z : z_)
        {
            const double v3 = x - z.ic2;
            const double v1 = a1_ * z.ic1 + a2_ * v3;
            v2 = z.ic2 + a2_ * z.ic1 + a3_ * v3;
            z.ic1 = 2.0 * v1 - z.ic1;
            z.ic2 = 2.0 * v2 - z.ic2;
            if (std::fabs (z.ic1) < 1e-20)
                z.ic1 = 0;
            if (std::fabs (z.ic2) < 1e-20)
                z.ic2 = 0;
            x = v2;
        }
        return v2;
    }

private:
    struct St
    {
        double ic1 = 0, ic2 = 0;
    };
    double g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    St z_[2];
};
} // namespace bass
