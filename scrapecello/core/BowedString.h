#pragma once

// A bowed string: digital waveguide with a friction bow (no JUCE).
//
// The string is two travelling-wave loops meeting at the bow: bow-to-bridge and bow-to-finger. At the bow
// the rosined hair and the string either stick (moving together) or slip (the string flying back under
// the hair), decided each sample by a friction law:
//   mu(dv) = muD + (muS - muD) * v0 / (v0 + |dv|)        (dv: bow speed minus string speed)
// Sticking needs no more force than static friction can give (|F| <= muS * bowForce); otherwise the
// string slips and the friction falls with slip speed. That falling friction is what makes a bowed string
// self-oscillate into a Helmholtz motion (one stick and one slip per cycle: a clean tone).
// Pressure and position decide the regime (Schelleng): too little force for the bow position and the slip
// comes twice a cycle (a breathy, octave-up "surface" sound); the right amount, a clean note; too much,
// the stick won't let go on time and the motion turns raucous: grinding, crunching, dropping into
// subharmonics. Rosin, bow hair and the bow's own grain add noise to the friction as it slips.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sc
{
constexpr double kPi = 3.14159265358979323846;

struct Rng
{
    uint32_t s = 0x2545f491u;
    double next() // -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s / 2147483648.0 - 1.0;
    }
};

// fractional delay line, 3rd-order Lagrange read
class FracDelay
{
public:
    void resize (int n) { buf_.assign ((size_t) n, 0.0); pos_ = 0; }
    void clear() { std::fill (buf_.begin(), buf_.end(), 0.0); }
    void write (double x)
    {
        buf_[(size_t) pos_] = x;
        pos_ = pos_ + 1 == (int) buf_.size() ? 0 : pos_ + 1;
    }
    // read `d` samples ago (d >= 1)
    double read (double d) const
    {
        const int n = (int) buf_.size();
        d = std::clamp (d, 1.0, (double) n - 3.0);
        const int i = (int) d;
        const double f = d - i;
        auto at = [&] (int k) { int j = pos_ - k; while (j < 0) j += n; return buf_[(size_t) j]; };
        const double xm1 = at (i - 1 < 1 ? 1 : i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        // Lagrange around x0 (f in 0..1), taps at -1, 0, 1, 2
        const double c0 = -f * (f - 1) * (f - 2) / 6.0, c1 = (f + 1) * (f - 1) * (f - 2) / 2.0;
        const double c2 = -(f + 1) * f * (f - 2) / 2.0, c3 = (f + 1) * f * (f - 1) / 6.0;
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }
private:
    std::vector<double> buf_;
    int pos_ = 0;
};

class BowedString
{
public:
    // friction (normalised: string impedance 1, so bow force and speeds share units)
    static constexpr double kMuS = 0.8, kMuD = 0.3, kV0 = 0.1;

    void prepare (double fs, double lowestHz)
    {
        fs_ = fs;
        const int n = (int) (fs / lowestHz) + 16;
        toBridge_.resize (n);
        toNut_.resize (n);
        reset();
    }
    void reset()
    {
        toBridge_.clear();
        toNut_.clear();
        bridgeLp_ = nutLp_ = 0;
        stuck_ = true;
        bridgeOut_ = 0;
        energy_ = 0;
    }

    // --- controls (smooth them before calling: these are per-sample) ---
    void setPitch (double hz) { hz_ = std::max (20.0, hz); }
    void setBowPosition (double beta) { beta_ = std::clamp (beta, 0.02, 0.45); } // fraction of the string from the bridge
    void setBow (double speed, double force) { vb_ = speed; fb_ = std::max (0.0, force); }
    void setLoss (double bridgeGain, double brightness) // per-reflection gain, 0..1 brightness (bridge low-pass)
    {
        bridgeGain_ = bridgeGain;
        bridgeCoef_ = std::clamp (brightness, 0.05, 0.98);
    }
    void setNoise (double amount) { noise_ = amount; }

    // pluck/strike: a velocity kick at the bow point (col legno, pizz)
    void kick (double v) { kick_ += v; }

    double process (double noise)
    {
        // the loop: one period, less what the two reflection filters delay at this pitch (their phase delay)
        {
            const double w = 2 * kPi * hz_ / fs_;
            auto lag = [w] (double c) { const double a = 1.0 - c; return std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w; };
            loop_ = std::max (8.0, fs_ / hz_ - lag (bridgeCoef_) - lag (kNutCoef));
        }
        const double lb = std::max (2.0, loop_ * beta_), ln = std::max (2.0, loop_ * (1.0 - beta_));
        // waves arriving at the bow, after their round trips to the bridge and the finger
        const double fromBridgeRaw = toBridge_.read (lb);
        const double fromNutRaw = toNut_.read (ln);
        // bridge: a lossy, slightly low-passed inverting reflection; the finger (nut): softer, a touch more loss
        bridgeLp_ += bridgeCoef_ * (fromBridgeRaw - bridgeLp_);
        const double b = -bridgeGain_ * bridgeLp_;
        nutLp_ += kNutCoef * (fromNutRaw - nutLp_);
        const double a = -0.995 * nutLp_;
        bridgeOut_ = fromBridgeRaw; // what pushes on the bridge (and on into the body)
        // the bow: stick or slip
        const double vh = a + b + kick_; // the string's speed at the bow if the bow weren't there
        kick_ = 0;
        double dv = 0; // bow minus string speed, after the bow has acted
        const double k = 0.5 * fb_;
        const double dvh = vb_ - vh;
        if (k <= 1e-9)
            dv = dvh; // bow off the string
        else if (stuck_ && std::fabs (dvh) <= k * kMuS)
            dv = 0.0; // still sticking: static friction holds it
        else
        {
            // slipping: dv + k*mu(dv) = dvh, on the side dvh points; take the root the motion is on
            const double sgn = dvh >= 0 ? 1.0 : -1.0, h = std::fabs (dvh);
            // x = |dv|: x + k*muD + k*(muS-muD)*v0/(v0+x) = h  ->  x^2 + x(v0 + k muD - h) + v0(k muD - h) + k(muS - muD) v0 = 0
            const double B = kV0 + k * kMuD - h, C = kV0 * (k * kMuD - h) + k * (kMuS - kMuD) * kV0;
            const double disc = B * B - 4 * C;
            if (disc < 0 || h <= k * kMuD)
                dv = 0.0; // no slipping solution: it sticks
            else
            {
                const double x = (-B + std::sqrt (disc)) * 0.5; // the fast-slip root
                dv = x > 0 ? sgn * x : 0.0;
            }
        }
        stuck_ = dv == 0.0;
        // rosin and hair: the friction's grain while slipping, a rasp that grows with force and slip speed
        if (! stuck_ && k > 0)
            dv += noise_ * noise * k * std::min (1.0, std::fabs (dv) * 4.0);
        const double v = vb_ - dv;          // the string's speed at the bow
        const double add = v - vh;          // what the bow put in (F / 2Z)
        toBridge_.write (a + add);          // on towards the bridge: what came from the finger, plus the bow
        toNut_.write (b + add);             // and towards the finger
        energy_ += 0.0005 * (v * v - energy_);
        return bridgeOut_;
    }

    bool stuck() const { return stuck_; }
    double level() const { return energy_; }
    double loopSamples() const { return loop_; }
    static constexpr double kNutCoef = 0.85; // the finger's reflection: a little softer than the bridge's

private:
    double fs_ = 48000, hz_ = 220, loop_ = 100, beta_ = 0.1, vb_ = 0, fb_ = 0, bridgeGain_ = 0.995, bridgeCoef_ = 0.5, noise_ = 0;
    double bridgeLp_ = 0, nutLp_ = 0, bridgeOut_ = 0, kick_ = 0, energy_ = 0;
    bool stuck_ = true;
    FracDelay toBridge_, toNut_;
};
} // namespace sc
