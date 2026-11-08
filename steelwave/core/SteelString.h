#pragma once

// Steelwave: physically modelled pedal steel string (no JUCE).
//
// One string = two digital waveguide loops (horizontal and vertical polarisation), each:
//   time-varying fractional delay (6-point Lagrange) -> one-pole loss filter -> dispersion allpass
// with the delay length recomputed every sample from the smoothed pitch, so bar slides and pedal
// bends are continuous (no zipper, no clicks). The loop's group delay at the fundamental is
// compensated so the string is in tune at every pitch.
//
// Pitch is split into what moves it physically:
//   bar     - shortens the vibrating length (wave speed unchanged); moving it makes contact noise
//   pedal   - pedals/knee levers change tension (wave speed changes, length doesn't)
//   vibrato - bar rocking; applied on top, lightly smoothed
// Pick and pickup are positioned in metres from the bridge, so their comb filters track the bar
// exactly as on the instrument (fixed travel time, because the wave speed doesn't change with the bar).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sw
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kScaleM = 0.6223; // 24.5" scale length

// --- small building blocks ------------------------------------------------------------

// Critically damped 2nd-order glide: S-shaped, continuous velocity, so a moving delay never kinks.
struct Glide
{
    double x = 0, v = 0, target = 0, w = 1000;
    void setTime (double seconds) { w = 6.6 / std::max (seconds, 0.001); } // ~99% settled after `seconds`
    void jump (double value) { x = target = value; v = 0; }
    double step (double dt)
    {
        // semi-implicit Euler; w*dt <= 0.15 for glides >= 1 ms at 44.1 kHz and up
        v += (w * w * (target - x) - 2.0 * w * v) * dt;
        x += v * dt;
        return x;
    }
};

// Circular delay line read with a 6-point (5th-order) Lagrange interpolator.
// Time-varying safe: no internal state besides the buffer, flat to ~0.35 fs.
class FracDelay
{
public:
    void allocate (int maxDelay)
    {
        int n = 1;
        while (n < maxDelay + 8)
            n <<= 1;
        buf_.assign ((size_t) n, 0.0f);
        mask_ = n - 1;
        w_ = 0;
    }
    void clear() { std::fill (buf_.begin(), buf_.end(), 0.0f); }
    int maxDelay() const { return (int) buf_.size() - 8; }
    void write (float x)
    {
        buf_[(size_t) w_] = x;
        w_ = (w_ + 1) & mask_;
    }
    // value written `d` samples ago (d >= 3; d = 1 is the last write); call before write()
    double read (double d) const
    {
        d = std::clamp (d, 3.0, (double) maxDelay());
        const int n = (int) d;
        const double f = d - n;
        // nodes at delays n-2 .. n+3 (offsets k = -2..3), evaluated at offset f
        double h[6];
        const double fm[6] = { f + 2, f + 1, f, f - 1, f - 2, f - 3 };
        h[0] = -fm[1] * fm[2] * fm[3] * fm[4] * fm[5] / 120.0;
        h[1] = fm[0] * fm[2] * fm[3] * fm[4] * fm[5] / 24.0;
        h[2] = -fm[0] * fm[1] * fm[3] * fm[4] * fm[5] / 12.0;
        h[3] = fm[0] * fm[1] * fm[2] * fm[4] * fm[5] / 12.0;
        h[4] = -fm[0] * fm[1] * fm[2] * fm[3] * fm[5] / 24.0;
        h[5] = fm[0] * fm[1] * fm[2] * fm[3] * fm[4] / 120.0;
        double y = 0;
        for (int k = 0; k < 6; ++k)
            y += h[k] * buf_[(size_t) ((w_ - (n + k - 2)) & mask_)];
        return y;
    }

private:
    std::vector<float> buf_;
    int mask_ = 0, w_ = 0;
};

// Max bridge coupling (per sample, see SteelString::process) and the loop-gain headroom it needs.
// Coupled loops resonating together are stable while (n-1) * coupling < 1 - g: keeping 1 - g at
// least 5x the coupling leaves room for 5 strings sharing an exact resonance (E9 has pairs at most).
constexpr double kMaxCoupling = 1.5e-4;
constexpr double kCouplingHeadroom = 5.0 * kMaxCoupling;

// Loop loss: g (1-a) / (1 - a z^-1). Designed from T60 at low frequencies and at fHigh.
struct LossFilter
{
    double g = 0.999, a = 0.0, z = 0.0;
    void design (double f0, double t60Low, double t60High, double fHigh, double fs)
    {
        const double perSec = f0; // loop passes per second
        g = std::pow (10.0, -3.0 / (perSec * std::max (t60Low, 1e-3)));
        // headroom for bridge coupling: per-period coupling gain from 9 other strings stays below 1 - g
        g = std::min (g, 1.0 - kCouplingHeadroom);
        const double gh = std::pow (10.0, -3.0 / (perSec * std::max (t60High, 1e-3)));
        const double r = std::clamp (gh / g, 0.0, 1.0);
        const double w = 2.0 * kPi * std::min (fHigh, 0.45 * fs) / fs;
        if (r >= 0.99999)
            a = 0.0;
        else
        {
            const double r2 = r * r, b = 1.0 - r2 * std::cos (w), q = 1.0 - r2;
            a = (b - std::sqrt (std::max (b * b - q * q, 0.0))) / q;
        }
        a = std::clamp (a, 0.0, 0.95);
    }
    double phaseDelay (double w) const { return w < 1e-9 ? a / (1.0 - a) : std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w; }
    double process (double x)
    {
        z = g * (1.0 - a) * x + a * z;
        return z;
    }
};

// Stiffness: cascade of identical first-order allpasses (c < 0: high partials arrive early -> sharp).
struct Dispersion
{
    static constexpr int kStages = 2;
    double c = 0.0;
    std::array<double, kStages> s {};
    double phaseDelay (double w) const
    {
        if (c == 0.0)
            return 0.0;
        // A(e^jw) = (c + e^-jw) / (1 + c e^-jw)
        const double nr = c + std::cos (w), ni = -std::sin (w);
        const double dr = 1.0 + c * std::cos (w), di = -c * std::sin (w);
        const double ph = std::atan2 (ni, nr) - std::atan2 (di, dr);
        double pd = -ph / std::max (w, 1e-9);
        return kStages * pd;
    }
    double process (double x)
    {
        if (c == 0.0)
            return x;
        for (auto& st : s)
        {
            const double y = c * x + st;
            st = x - c * y;
            x = y;
        }
        return x;
    }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fc, double q, double fs)
    {
        const double w = 2 * kPi * std::min (fc, 0.45 * fs) / fs, al = std::sin (w) / (2 * q), cw = std::cos (w);
        const double a0 = 1 + al;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0;
        a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void bandpass (double fc, double q, double fs) // constant 0 dB peak
    {
        const double w = 2 * kPi * std::clamp (fc, 20.0, 0.45 * fs) / fs, al = std::sin (w) / (2 * q), cw = std::cos (w);
        const double a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0;
        a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void reset() { z1 = z2 = 0; }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Rng
{
    uint32_t s = 0x9e3779b9u;
    double next() // uniform -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s / 2147483648.0 - 1.0;
    }
};

// --- string description ---------------------------------------------------------------

struct StringSpec
{
    double openMidi = 64;   // open pitch
    bool wound = false;
    double t60Low = 8.0;    // s, low partials
    double t60High = 1.0;   // s, at 5 kHz
    double inharmCents10 = 3.0; // stiffness: how sharp the 10th partial of the open string is (cents)
    double windPitchM = 0.0003; // wrap-wire spacing (wound strings), sets contact-noise pitch
};

// Standard 10-string E9 (string 1 = highest-pitched, nearest the player's far side).
inline std::array<StringSpec, 10> e9Strings()
{
    const double midi[10] = { 66, 63, 68, 64, 59, 56, 54, 52, 50, 47 }; // F# D# G# E B G# F# E D B
    std::array<StringSpec, 10> s {};
    for (int i = 0; i < 10; ++i)
    {
        s[(size_t) i].openMidi = midi[i];
        s[(size_t) i].wound = i >= 5;
        const double t = (midi[i] - 47.0) / (68.0 - 47.0); // 0 low .. 1 high
        s[(size_t) i].t60Low = 11.0 - 5.0 * t;
        s[(size_t) i].t60High = s[(size_t) i].wound ? 0.8 : 1.3;
        // B = pi^3 E d^4 / (64 T L^2): ~3 cents at partial 10 for the plain .014 E, less for
        // thinner plain strings and for wound strings (thin core); grows as the bar shortens the string
        s[(size_t) i].inharmCents10 = s[(size_t) i].wound ? 1.5 : 2.0 + 1.5 * (1.0 - t);
        s[(size_t) i].windPitchM = 0.00025 + 0.0001 * (1.0 - t);
    }
    return s;
}

// --- the string -------------------------------------------------------------------------

struct StringParams
{
    double pickPosM = 0.10;     // pick distance from the bridge
    double pickupPosM = 0.045;  // pickup distance from the bridge
    double hardness = 0.7;      // 0 soft (thumb, flesh) .. 1 hard metal pick
    double brightness = 0.5;    // scales high-frequency decay (0 dull/old strings .. 1 new)
    double sustain = 1.0;       // scales T60 (0.25..2)
    double barNoise = 0.5;      // contact noise level 0..1
    double polDetuneCents = 1.1;// vertical vs horizontal polarisation (slow beating)
};

class SteelString
{
public:
    void prepare (double fs, const StringSpec& spec)
    {
        fs_ = fs;
        spec_ = spec;
        fOpen_ = 440.0 * std::pow (2.0, (spec.openMidi - 69.0) / 12.0);
        // longest loop: open string with pedals lowering by up to 4 semitones
        const int maxD = (int) std::ceil (fs / (fOpen_ * std::pow (2.0, -4.0 / 12.0))) + 16;
        for (auto& p : pol_)
            p.line.allocate (maxD);
        combLine_.allocate ((int) std::ceil (0.004 * fs) + 8);
        rng_.s = 0x1234567u + (uint32_t) (spec.openMidi * 7919);
        cDisp_ = solveDispersion (fs, fOpen_, spec.inharmCents10);
        noiseBp_.lowpass (3000.0, 0.7, fs);
        hpCoef_ = 1.0 - std::exp (-2.0 * kPi * 400.0 / fs);
        bar_.setTime (0.08);
        pedal_.setTime (0.12);
        vib_.setTime (0.002);
        reset();
    }

    void reset()
    {
        for (auto& p : pol_)
        {
            p.line.clear();
            p.loss.z = 0;
            p.disp.s.fill (0.0);
        }
        combLine_.clear();
        bar_.jump (0);
        pedal_.jump (0);
        vib_.jump (0);
        excLen_ = excPos_ = 0;
        excDelay_ = 0;
        damp_ = dampTarget_ = 0;
        dampPulse_ = 0;
        energy_ = 0;
        noiseLev_ = 0;
        noiseBp_.reset();
        bridge_ = 0;
        ridgePh_ = attack_ = 0;
        noiseHp_ = 0;
        dc_ = 0;
        ctl_ = 0;
        update (true);
    }

    void setParams (const StringParams& p) { prm_ = p; }
    const StringSpec& spec() const { return spec_; }

    // positions in semitones above the open string; glide in seconds (0 = jump)
    // noise = false: the bar is lifted and put down (no contact noise for this move)
    void setBar (double semis, double glideSec, bool noise = true)
    {
        silentMove_ = ! noise;
        if (glideSec <= 0) { bar_.jump (semis); return; }
        bar_.setTime (glideSec);
        bar_.target = semis;
    }
    void setPedal (double semis, double glideSec)
    {
        if (glideSec <= 0) { pedal_.jump (semis); return; }
        pedal_.setTime (glideSec);
        pedal_.target = semis;
    }
    void setVibrato (double semis) { vib_.target = semis; }
    // how hard the bar presses this string (1 = a played string, less for ones just under the bar)
    void setContact (double c) { contact_ = std::clamp (c, 0.0, 1.0); }

    double currentSemis() const { return bar_.x + pedal_.x + vib_.x; }
    double currentHz() const { return fOpen_ * std::pow (2.0, currentSemis() / 12.0); }
    bool isSounding() const { return energy_ > 1e-9 || excPos_ < excLen_ || excDelay_ > 0; }
    double level() const { return energy_; } // mean square at the bridge (roughly)
    // ringing audibly and not being blocked
    bool isRinging() const { return (energy_ > 1e-12 || excPos_ < excLen_ || excDelay_ > 0) && dampTarget_ < 0.5; }

    // Pick the string. velocity 0..1. If it's already ringing, the pick first catches it briefly.
    void pluck (double velocity)
    {
        velocity = std::clamp (velocity, 0.0, 1.0);
        damp_ = dampTarget_ = 0;
        const bool ringing = energy_ > 1e-6;
        excDelay_ = ringing ? (int) (0.004 * fs_) : 0;
        dampPulse_ = dampLen_ = excDelay_;
        if (ringing) // ~ -10 dB over the catch, whatever the loop length
            pickMin_ = std::pow (0.3, (fs_ / currentHz()) / (0.5 * excDelay_));
        buildExcitation (velocity);
        // a hard pick stretches the string: it starts a little sharp and settles (tension modulation)
        attack_ = 0.07 * velocity * velocity * (0.5 + 0.5 * prm_.hardness); // semitones (up to 7 cents)
        attackDecay_ = std::exp (-1.0 / (0.07 * fs_));
    }

    // Hand/bar block: 1 = fully muted (fast decay), 0 = ringing. Ramped.
    void setDamp (double amount) { dampTarget_ = std::clamp (amount, 0.0, 1.0); }

    // `bridgeIn`: what the other strings put into this one through the bridge, already scaled by the
    // coupling amount (<= kMaxCoupling times the sum of the other strings' bridge() keeps it stable).
    float process (double bridgeIn = 0.0)
    {
        constexpr int kCtl = 16;
        if (ctl_-- <= 0)
        {
            update (false);
            ctl_ = kCtl - 1;
        }
        const double dt = 1.0 / fs_;
        const double barPrev = bar_.x;
        bar_.step (dt);
        pedal_.step (dt);
        vib_.step (dt);
        attack_ *= attackDecay_;
        const double semis = bar_.x + pedal_.x + vib_.x + attack_;
        const double period = fs_ / (fOpen_ * std::exp2 (semis / 12.0));

        // excitation (after the pick has caught a ringing string)
        double exc = 0.0;
        if (excDelay_ > 0)
            --excDelay_;
        else if (excPos_ < excLen_)
            exc = exc_[(size_t) excPos_++];

        // bar contact noise: from the bar's speed along the string
        const double barVelSemis = (bar_.x - barPrev) * fs_;
        const double L = kScaleM * std::exp2 (-bar_.x / 12.0);
        const double vBar = std::fabs (L * (std::log (2.0) / 12.0) * barVelSemis); // m/s
        // S-curve in speed: silent while the bar creeps into place, saturating on fast slides
        const double sx = std::min (vBar / 0.4, 1.0);
        if (silentMove_ && std::fabs (bar_.target - bar_.x) < 1e-4)
            silentMove_ = false;
        const double speedLev = silentMove_ ? 0.0 : sx * sx * (3.0 - 2.0 * sx) * prm_.barNoise * contact_;
        noiseLev_ += (speedLev > noiseLev_ ? 0.002 : 0.0008) * (speedLev - noiseLev_);
        double noise = 0.0;
        if (noiseLev_ > 1e-5)
        {
            const double wn = rng_.next();
            double g;
            if (spec_.wound)
            {
                // each wrap ridge passing under the bar is a small soft tick: noise grains at the
                // ridge rate (rough, not a whistle)
                ridgePh_ += std::min (vBar / spec_.windPitchM, 6000.0) / fs_;
                ridgePh_ -= std::floor (ridgePh_);
                const double tick = std::exp (-ridgePh_ * 6.0);
                g = wn * (0.35 + 0.65 * tick);
            }
            else
                g = 0.35 * wn; // plain string: a faint smooth hiss
            // band-limit: the bar and pickup hear mostly 400 Hz .. 3.5 kHz of it
            noiseHp_ += hpCoef_ * (g - noiseHp_); // ~400 Hz one-pole, subtracted below
            noise = noiseBp_.process (g - noiseHp_) * (spec_.wound ? 0.08 : 0.027) * noiseLev_;
        }

        // pick catching a ringing string: brief extra loss
        double pickGain = 1.0;
        if (dampPulse_ > 0)
        {
            --dampPulse_;
            const double sn = std::sin (kPi * (double) dampPulse_ / dampLen_);
            pickGain = 1.0 - (1.0 - pickMin_) * sn * sn;
        }

        double out = 0.0;
        const double frac = (double) (kCtl - 1 - ctl_) / kCtl;
        for (int k = 0; k < 2; ++k)
        {
            auto& p = pol_[(size_t) k];
            const double comp = p.compA + (p.compB - p.compA) * frac;
            const double per = k == 0 ? period : period * polRatio_;
            double y = p.line.read (per - comp);
            y = p.disp.process (p.loss.process (y)) * pickGain;
            // a little of the contact noise excites the string; most of it is heard directly
            double in = y + exc * kExcSplit[k] + noise * 0.1 * kExcSplit[k];
            if (k == 0)
            {
                bridge_ = y;
                in += bridgeIn;
            }
            p.line.write ((float) in);
            out += in * kPickupSplit[k];
        }

        // pickup position comb (velocity pickup at a fixed distance from the bridge)
        combLine_.write ((float) out);
        double s = out - combLine_.read (std::max (combDelay_, 3.0));
        // the direct share of contact noise the pickup hears
        s += noise;
        // DC guard
        dc_ += 0.0005 * (s - dc_);
        s -= dc_;
        energy_ += 0.0005 * (out * out - energy_);
        return (float) s;
    }

private:
    static constexpr double kExcSplit[2] = { 0.9, 0.45 };
    static constexpr double kPickupSplit[2] = { 1.0, 0.45 };

    void update (bool init)
    {
        // read targets at the start of this control block
        const double semis = bar_.x + pedal_.x + vib_.x + attack_;
        const double f0 = fOpen_ * std::exp2 (semis / 12.0);
        const double w0 = 2.0 * kPi * f0 / fs_;
        polRatio_ = std::exp2 (-prm_.polDetuneCents / 1200.0);

        // damping (hand block): ramp toward target, interpolate T60 in the log domain
        damp_ += (dampTarget_ - damp_) * (init ? 1.0 : 0.25);
        const double sus = std::clamp (prm_.sustain, 0.25, 2.0);
        const double bright = std::exp2 ((prm_.brightness - 0.5) * 2.0);
        const double tL = std::exp (std::log (spec_.t60Low * sus) * (1 - damp_) + std::log (0.10) * damp_);
        const double tH = std::exp (std::log (spec_.t60High * sus * bright) * (1 - damp_) + std::log (0.04) * damp_);

        for (int k = 0; k < 2; ++k)
        {
            auto& p = pol_[(size_t) k];
            const double fk = k == 0 ? f0 : f0 / polRatio_;
            const double wk = k == 0 ? w0 : w0 / polRatio_;
            p.loss.design (fk, k == 0 ? tL : tL * 0.55, k == 0 ? tH : tH * 0.7, 5000.0, fs_);
            p.disp.c = cDisp_;
            p.compA = init ? 0.0 : p.compB;
            p.compB = p.loss.phaseDelay (wk) + p.disp.phaseDelay (wk);
            if (init)
                p.compA = p.compB;
        }

        // wave speed follows tension (pedals), not the bar
        const double c = 2.0 * kScaleM * fOpen_ * std::exp2 (pedal_.x / 12.0);
        waveSpeed_ = c;
        combDelay_ = 2.0 * prm_.pickupPosM / c * fs_;

    }

    // Allpass coefficient that puts the open string's 10th partial `cents` sharp.
    // Partial k sits where the loop's total phase delay fits k cycles: f_k * tau(f_k) = k * fs.
    static double solveDispersion (double fs, double f0, double cents)
    {
        if (cents <= 0)
            return 0.0;
        auto sharpness = [&] (double c) {
            Dispersion d;
            d.c = c;
            const double w0 = 2 * kPi * f0 / fs;
            const double line = fs / f0 - d.phaseDelay (w0);
            double f = 10 * f0;
            for (int it = 0; it < 30; ++it)
                f = 10 * fs / (line + d.phaseDelay (2 * kPi * std::min (f, 0.49 * fs) / fs));
            return 1200 * std::log2 (f / (10 * f0));
        };
        double lo = -0.9, hi = 0.0; // more negative = sharper
        for (int it = 0; it < 50; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            (sharpness (mid) > cents ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    void buildExcitation (double vel)
    {
        // pick at a fixed distance from the bridge: rectangular velocity pulse of the wave's
        // round-trip time (the ideal pluck's sin(n pi beta)/n spectrum), smoothed by the pick's
        // contact time (harder pick and harder playing = shorter = brighter), plus a pick click.
        const double period = fs_ / currentHz();
        const double W = std::clamp (2.0 * prm_.pickPosM / waveSpeed_ * fs_, 2.0, 0.9 * period);
        const double h = std::clamp (prm_.hardness, 0.0, 1.0);
        const double contactMs = (0.25 + 1.6 * (1.0 - h)) * std::exp2 (-1.5 * (vel - 0.5));
        const int T = std::max (2, (int) std::lround (contactMs * 1e-3 * fs_));
        const int Wi = (int) std::lround (W);
        const int click = (int) (0.0015 * fs_);
        excLen_ = std::min ((int) exc_.size(), std::max (Wi + T, click) + 1);
        std::fill (exc_.begin(), exc_.begin() + excLen_, 0.0);
        // rect (Wi) * Hann (T), normalised to unit peak
        double hsum = 0;
        for (int j = 0; j < T; ++j)
            hsum += 0.5 - 0.5 * std::cos (2 * kPi * (j + 0.5) / T);
        for (int n = 0; n < excLen_; ++n)
        {
            double acc = 0;
            for (int j = 0; j < T; ++j)
            {
                const int m = n - j;
                if (m >= 0 && m < Wi)
                    acc += 0.5 - 0.5 * std::cos (2 * kPi * (j + 0.5) / T);
            }
            exc_[(size_t) n] = acc / hsum;
        }
        const double amp = 0.08 + 0.92 * std::pow (vel, 1.6);
        // metal pick tick: a short decaying burst around 2.5-4.5 kHz (the pick's edge leaving the
        // string), stronger for hard picks and hard playing
        Biquad tick;
        tick.bandpass (2500.0 + 2000.0 * h * vel, 1.4, fs_);
        const double tickAmp = 0.35 * amp * h * (0.4 + 0.6 * vel);
        double prev = 0;
        for (int n = 0; n < excLen_; ++n)
        {
            const double env = n < click ? std::exp (-4.0 * n / click) : 0.0;
            const double wn = rng_.next();
            exc_[(size_t) n] = exc_[(size_t) n] * amp + tickAmp * env * tick.process (wn) + 0.08 * amp * h * env * (wn - prev);
            prev = wn;
        }
        excPos_ = 0;
    }

    struct Pol
    {
        FracDelay line;
        LossFilter loss;
        Dispersion disp;
        double compA = 0, compB = 0;
    };

    double fs_ = 48000, fOpen_ = 329.63, cDisp_ = 0, polRatio_ = 1, waveSpeed_ = 400, combDelay_ = 10;
    StringSpec spec_;
    StringParams prm_;
    std::array<Pol, 2> pol_;
    FracDelay combLine_;
    Glide bar_, pedal_, vib_;
    std::array<double, 4096> exc_ {};
    int excLen_ = 0, excPos_ = 0, excDelay_ = 0, dampPulse_ = 0, dampLen_ = 1, ctl_ = 0;
    double pickMin_ = 1;
    double bridge_ = 0;
    bool silentMove_ = false;
    double contact_ = 1.0;

public:
    // this string's horizontal wave arriving at the bridge (drives the other strings)
    double bridge() const { return bridge_; }

private:
    double attack_ = 0, attackDecay_ = 0, ridgePh_ = 0, hpCoef_ = 0.05;
    double damp_ = 0, dampTarget_ = 0, energy_ = 0, noiseLev_ = 0, noiseHp_ = 0, dc_ = 0;
    Biquad noiseBp_;
    Rng rng_;
};

// Pickup coil + cable: resonant low-pass on the summed strings. One per instrument.
class Pickup
{
public:
    enum Type { SingleCoil, Humbucker };
    void prepare (double fs) { fs_ = fs; set (type_); }
    void set (Type t)
    {
        type_ = t;
        if (t == SingleCoil) lp_.lowpass (6500.0, 1.7, fs_);
        else lp_.lowpass (3600.0, 1.2, fs_);
    }
    void reset() { lp_.reset(); }
    float process (float x) { return (float) lp_.process (x); }

private:
    double fs_ = 48000;
    Type type_ = SingleCoil;
    Biquad lp_;
};
} // namespace sw
