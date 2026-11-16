#pragma once

// Pump organ: a physically modelled American suction reed organ (no JUCE).
//
// The player's feet (here, the volume pedal) work exhauster bellows that pull air out of a reservoir.
// Every key opens a pallet under its reeds; air from the room rushes through each reed's slot into the
// low-pressure chest, and the brass tongue swinging in the slot chops that flow. The chopped flow is the
// sound. So the wind decides everything:
//   - a reed only speaks above a threshold suction, and speaks slower near it (bass reeds slowest);
//   - more suction swings the tongue further: louder, and brighter as it clears the slot on both sides;
//   - a hard-driven tongue sags a few cents flat;
//   - every open reed draws on the same chest, so a big chord sags the wind for everyone.
//
// Reed:   a damped mass-spring tongue (x'' + 2 zeta w x' + w^2 x = F), excited by the flow, with the
//         self-limiting the slot gives (van der Pol form). Simulated at 2x the output rate.
// Flow:   U = aperture(x) * sqrt(suction): the slot is shut while the tongue is inside it and opens
//         smoothly as it swings out either side (the tongue rests a little off the slot, so the two sides differ).
// Wind:   Easy (pedal position = suction), Reservoir (each pedal stroke pumps air out; reeds use it up),
//         Expression (the reservoir bypassed: the strokes themselves are the wind).
// Case:   reed cells (per stop), the wooden case's resonances, the swell lid (Forte), the Vox Humana fan,
//         a close mic in front of the reed board; air hiss and pallet and bellows noise.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ro
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kLowKey = 36, kHighKey = 96, kKeys = kHighKey - kLowKey + 1; // C2..C7, a 5-octave reed organ

// --- stops (ranks of reeds) -------------------------------------------------------------------
enum Stop { Diapason, Melodia, Dulciana, Celeste, SubBass, Flute, kStops };
struct RankVoice
{
    const char* name;
    int octave;        // 0 = 8', -1 = 16', +1 = 4'
    double cents;      // tuned off the others (Celeste)
    double drive;      // how hard the flow drives the tongue (voicing: softer reeds swing less)
    double set;        // where the tongue rests, off the slot (more: brighter, more even harmonics)
    double cell;       // reed cell low-pass, in multiples of the reed's pitch (mellow .. bright)
    double gain;
    int lowKey, highKey; // compass (Sub Bass: the bottom octave and a half)
};
inline const std::array<RankVoice, kStops>& rankVoices()
{
    static const std::array<RankVoice, kStops> r = { {
        { "Diapason 8'", 0, 0.0, 1.0, 0.16, 14.0, 1.0, kLowKey, kHighKey },
        { "Melodia 8'", 0, 0.0, 0.85, 0.10, 6.0, 0.9, kLowKey, kHighKey },
        { "Dulciana 8'", 0, 0.0, 0.55, 0.09, 5.0, 0.55, kLowKey, kHighKey },
        { "Celeste 8'", 0, 7.0, 0.55, 0.09, 5.0, 0.55, 48, kHighKey }, // tuned sharp against the Dulciana; treble only, as usual
        { "Sub Bass 16'", -1, 0.0, 0.9, 0.12, 7.0, 1.1, kLowKey, kLowKey + 17 },
        { "Flute 4'", 1, 0.0, 0.7, 0.07, 3.5, 0.6, kLowKey, kHighKey },
    } };
    return r;
}

enum class Wind { Easy, Reservoir, Expression };

struct OrganParams
{
    Wind wind = Wind::Easy;
    std::array<bool, kStops> stops { true, false, false, false, false, false }; // Diapason
    bool voxHumana = false;     // the fan tremolo
    double voxRate = 5.2;       // Hz
    bool forte = false;         // swell lid open: louder, brighter
    double airNoise = 1.0;      // hiss, pallets and bellows (0..2)
    double tuningAge = 1.0;     // reeds a little off their marks (0 = perfect)
    double level = 1.0;
};

struct Rng
{
    uint32_t s = 0x2545f491u;
    double next() // -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s / 2147483648.0 - 1.0;
    }
};

// One free reed: the tongue, its slot, its cell
struct Reed
{
    // set up
    double c0 = 0.1;          // 2 sin(pi f / fs_i): the tongue's step (symplectic Euler, tuned exactly)
    double zeta = 0.002;      // the tongue's own losses
    double drive = 1.0, set = 0.15, gain = 1.0;
    double cellCoef = 0.5;    // one-pole low-pass of the reed cell
    // state
    double x = 0, v = 0, env = 0, cellY = 0, pallet = 0, palletTarget = 0, palletRate = 0.002, flowMean = 0;
    bool active = false;

    static constexpr double kSlot = 0.2;   // half the slot's depth, in tongue-swing units
    static constexpr double kLimit = 0.4;  // amplitude limiting (the tongue fills the slot)
    static constexpr double kGrowth = 0.2; // drive at full suction (e-folds in ~1.6 cycles)
    static constexpr double kSag = 0.003;  // a hard-driven tongue goes flat (~5 cents at full wind)
    static constexpr double kOpen = 0.08;   // the opening around the tongue that limits the flow, in swing units
    static constexpr double kInside = 0.35; // how far the slot opens on the cell side, relative to the room side

    // one step at the inner rate. suction 0..~1.2; noise -1..1. Returns the flow through the slot.
    double step (double suction, double noise)
    {
        pallet += (palletTarget - pallet) * palletRate;
        const double p = std::max (0.0, suction) * pallet;
        const double g = kGrowth * drive * p - 2.0 * zeta;            // net negative damping from the flow
        const double c = c0 * (1.0 - kSag * env);                     // pitch sags with the swing
        // turbulence nudges the tongue (that's how it starts), more with more flow
        const double kick = 2e-4 * noise * std::sqrt (p);
        v += -c * c * x + c * (g - kLimit * x * x) * v + c * kick;
        x += v;
        env += 0.0005 * (x * x - env);
        // the slot opens as the tongue swings out of it. Out towards the room it opens wide; in towards
        // the chest it swings into the reed cell, whose walls keep that side narrower.
        const double y = x + set;
        const double zo = y - kSlot, zi = -y - kSlot;
        // Once clear of the slot the flow is limited by the opening around the tongue, so the flow is a
        // train of near-square pulses, chopped shut each time the tongue passes through: the harder it
        // swings, the shorter and sharper the chop (brighter).
        auto opening = [] (double z) {
            const double r = 0.5 * (z + std::sqrt (z * z + 1e-4));
            return r * kOpen / (kOpen + r);
        };
        const double ap = 0.01 + opening (zo) + kInside * opening (zi);
        const double u = ap * std::sqrt (p);
        flowMean += 0.0005 * (u - flowMean);
        // the cell, then what radiates: the change in flow (a pulsing source), level-matched across the compass
        const double prev = cellY;
        cellY += cellCoef * (u - cellY);
        return (cellY - prev) / c0 * gain;
    }
    bool quiet() const { return palletTarget == 0.0 && pallet < 1e-4 && env < 1e-8; }
};

// Biquad (RBJ), direct form I
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void peak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), cw = std::cos (w);
        const double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * cw / a0; b2 = (1 - al * A) / a0; a1 = -2 * cw / a0; a2 = (1 - al / A) / a0;
    }
    void lowpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * std::min (f, 0.45 * fs) / fs, al = std::sin (w) / (2 * q), cw = std::cos (w), a0 = 1 + al;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    void bandpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), cw = std::cos (w), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
    }
    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

class ReedOrgan
{
public:
    ReedOrgan() { reeds_.resize ((size_t) (kKeys * kStops)); active_.reserve (reeds_.size()); }

    void prepare (double fs)
    {
        fs_ = fs;
        fsi_ = 2.0 * fs; // the reeds run at twice the output rate
        Rng tune;
        tune.s = 0x9e3779b9u;
        for (int k = 0; k < kKeys; ++k)
            for (int r = 0; r < kStops; ++r)
            {
                auto& rd = reed (k, r);
                rd = {};
                const auto& rv = rankVoices()[(size_t) r];
                const double off = tune.next(), dv = tune.next(), sv = tune.next();
                detune_[(size_t) (k * kStops + r)] = off; // each reed a little off its mark (scaled by Tuning age)
                const double f = freqOf (k, r, 0.0);
                rd.zeta = 1.0 / (2.0 * 250.0);
                rd.drive = rv.drive * (1.0 + 0.04 * dv);
                rd.set = rv.set * (1.0 + 0.15 * sv);
                rd.gain = rv.gain;
                rd.cellCoef = 1.0 - std::exp (-2 * kPi * std::min (f * rv.cell, 0.4 * fsi_) / fsi_);
                rd.c0 = stepFor (f);
            }
        // case: the wooden box's resonances, and the mic in front of the reed board
        body_[0].peak (fs, 170.0, 1.2, 4.0);
        body_[1].peak (fs, 420.0, 1.6, 2.5);
        body_[2].peak (fs, 1150.0, 2.0, -2.0);
        body_[3].peak (fs, 2600.0, 1.4, 2.0);
        hissBp_.bandpass (fs, 3500.0, 0.7);
        thumpLp_.lowpass (fs, 900.0, 0.7);
        pumpLp_.lowpass (fs, 400.0, 0.7);
        // 2x down-sampling: a 31-tap windowed-sinc half-band
        for (int i = 0; i < kFir; ++i)
        {
            const double m = i - (kFir - 1) / 2.0;
            const double sinc = m == 0 ? 0.5 : std::sin (kPi * 0.5 * m) / (kPi * m);
            fir_[(size_t) i] = sinc * (0.42 - 0.5 * std::cos (2 * kPi * i / (kFir - 1)) + 0.08 * std::cos (4 * kPi * i / (kFir - 1)));
        }
        double s = 0;
        for (double c : fir_) s += c;
        for (double& c : fir_) c /= s;
        setParams (prm_);
        reset();
    }

    void reset()
    {
        for (auto& r : reeds_)
        {
            const auto c0 = r.c0, z = r.zeta, d = r.drive, st = r.set, g = r.gain, cc = r.cellCoef;
            r = {};
            r.c0 = c0; r.zeta = z; r.drive = d; r.set = st; r.gain = g; r.cellCoef = cc;
        }
        active_.clear();
        held_.fill (false);
        reservoir_ = 0.0;
        suction_ = 0.0;
        pedalPos_ = pedalSmooth_ = pedalVel_ = 0.0;
        dcX_ = dcY_ = 0.0;
        firPos_ = 0;
        firBuf_.fill (0.0);
    }

    void setParams (const OrganParams& p)
    {
        // retune for Tuning age, and re-stop held keys
        const bool retune = p.tuningAge != prm_.tuningAge;
        const auto oldStops = prm_.stops;
        prm_ = p;
        if (retune)
            for (int k = 0; k < kKeys; ++k)
                for (int r = 0; r < kStops; ++r)
                    reed (k, r).c0 = stepFor (freqOf (k, r, prm_.tuningAge));
        for (int k = 0; k < kKeys; ++k)
            if (held_[(size_t) k])
                for (int r = 0; r < kStops; ++r)
                    if (oldStops[(size_t) r] != prm_.stops[(size_t) r])
                        setPallet (k, r, prm_.stops[(size_t) r], 0.6);
        // the swell lid: closed, the case muffles the reeds; Forte opens it
        swell_.lowpass (fs_, prm_.forte ? 7500.0 : 3200.0, 0.6);
    }
    const OrganParams& params() const { return prm_; }

    // --- playing ---------------------------------------------------------------------------
    void noteOn (int note, double vel = 0.8)
    {
        const int k = note - kLowKey;
        if (k < 0 || k >= kKeys)
            return;
        held_[(size_t) k] = true;
        for (int r = 0; r < kStops; ++r)
            if (prm_.stops[(size_t) r])
                setPallet (k, r, true, vel);
        thump_ += 0.6 * (0.4 + 0.6 * vel); // the pallet and key action
    }
    void noteOff (int note)
    {
        const int k = note - kLowKey;
        if (k < 0 || k >= kKeys)
            return;
        held_[(size_t) k] = false;
        for (int r = 0; r < kStops; ++r)
            setPallet (k, r, false, 0.5);
        thump_ += 0.25;
    }
    void allNotesOff()
    {
        for (int n = kLowKey; n <= kHighKey; ++n)
            if (held_[(size_t) (n - kLowKey)])
                noteOff (n);
    }
    // the volume pedal: 0 (heel) .. 1 (toe). How it becomes wind depends on the Wind mode.
    void pedal (double pos) { pedalPos_ = std::clamp (pos, 0.0, 1.0); }

    // --- audio -------------------------------------------------------------------------------
    void process (float* out, int n)
    {
        const double dt = 1.0 / fs_;
        const double pedalCoef = 1.0 - std::exp (-dt / 0.006), velCoef = 1.0 - std::exp (-dt / 0.04);
        for (int i = 0; i < n; ++i)
        {
            // the feet: where the pedal is, and how fast it's moving
            const double prev = pedalSmooth_;
            pedalSmooth_ += pedalCoef * (pedalPos_ - pedalSmooth_);
            pedalVel_ += velCoef * ((pedalSmooth_ - prev) / dt - pedalVel_);
            const double stroke = std::fabs (pedalVel_); // pedal travel per second, either way
            // what the open reeds are drawing
            double draw = 0;
            for (int idx : active_)
                draw += reeds_[(size_t) idx].flowMean;
            updateWind (stroke, draw, dt);
            // the chest: the suction the reeds see, sagging under load
            const double chest = suction_ / (1.0 + kLoadSag * draw);
            // two reed steps per output sample, then down-sampled
            for (int h = 0; h < 2; ++h)
            {
                double sum = 0;
                for (size_t a = 0; a < active_.size(); ++a)
                    sum += reeds_[(size_t) active_[a]].step (chest, rng_.next());
                pushFir (sum);
            }
            double y = firOut();
            // DC (the mean flow) out: the mic hears the pulsing, not the draught
            const double dc = y - dcX_ + 0.9995 * dcY_;
            dcX_ = y;
            dcY_ = dc;
            y = dc * 6.0;
            // Vox Humana: a fan in the case, its blades sweeping past the reeds
            if (prm_.voxHumana)
            {
                voxPh_ += prm_.voxRate * dt;
                voxPh_ -= std::floor (voxPh_);
                y *= 1.0 + 0.22 * std::sin (2 * kPi * voxPh_);
            }
            // case, swell lid, mic
            for (auto& b : body_)
                y = b.process (y);
            y = swell_.process (y) * (prm_.forte ? 1.5 : 1.0);
            // noises: air hiss through the open reeds, pallets and keys, the bellows working
            const double nz = prm_.airNoise;
            if (nz > 0)
            {
                y += nz * 0.04 * hissBp_.process (rng_.next()) * std::sqrt (draw * chest);
                thump_ *= 0.995;
                y += nz * 0.05 * thumpLp_.process (rng_.next() * thump_);
                y += nz * 0.015 * pumpLp_.process (rng_.next()) * std::min (stroke, 4.0);
            }
            out[i] = (float) (y * prm_.level);
            if ((++clock_ & 255) == 0)
                prune();
        }
    }

    // --- for display and tests -----------------------------------------------------------------
    double suction() const { return suction_; }
    double reservoir() const { return reservoir_; }
    int activeReeds() const { return (int) active_.size(); }
    double reedAmplitude (int note, int stop) const
    {
        const int k = note - kLowKey;
        return k >= 0 && k < kKeys ? std::sqrt (2.0 * reeds_[(size_t) (k * kStops + stop)].env) : 0.0;
    }
    double reedFrequency (int note, int stop) const { return freqOf (note - kLowKey, stop, prm_.tuningAge); }

    // wind constants
    static constexpr double kLoadSag = 0.15;     // chest suction / (1 + this x total draw)
    static constexpr double kPumpGain = 0.35;    // Reservoir: fill per unit of pedal travel
    static constexpr double kUse = 0.8;          // Reservoir: emptied per unit of draw per second
    static constexpr double kExprGain = 0.55;    // Expression: suction per unit/s of pedal travel

private:
    Reed& reed (int k, int r) { return reeds_[(size_t) (k * kStops + r)]; }
    // the tongue's step for a pitch. Stepping a fast tongue with the flow's damping in the loop runs
    // a little sharp (by ~K c^2 cents): tuned that much flat to land on pitch.
    double stepFor (double f) const
    {
        double c = 2.0 * std::sin (kPi * f / fsi_);
        c *= std::exp2 (-kStepTune * c * c / 1200.0);
        return c;
    }
    static constexpr double kStepTune = 400.0;
    double freqOf (int k, int r, double age) const
    {
        const auto& rv = rankVoices()[(size_t) r];
        const double cents = rv.cents + 1.5 * age * detune_[(size_t) (k * kStops + r)];
        return 440.0 * std::pow (2.0, (kLowKey + k + 12 * rv.octave - 69) / 12.0 + cents / 1200.0);
    }
    void setPallet (int k, int r, bool open, double vel)
    {
        const auto& rv = rankVoices()[(size_t) r];
        const int note = kLowKey + k;
        if (open && (note < rv.lowKey || note > rv.highKey))
            return;
        auto& rd = reed (k, r);
        rd.palletTarget = open ? 1.0 : 0.0;
        // a pallet opens in ~4-12 ms (faster for a firm key), closes in ~6 ms
        const double ms = open ? 12.0 - 8.0 * std::clamp (vel, 0.0, 1.0) : 6.0;
        rd.palletRate = 1.0 - std::exp (-1.0 / (ms * 1e-3 * fsi_));
        if (open && ! rd.active)
        {
            rd.active = true;
            active_.push_back (k * kStops + r);
        }
    }
    void prune()
    {
        for (size_t a = 0; a < active_.size();)
        {
            auto& rd = reeds_[(size_t) active_[a]];
            if (rd.quiet())
            {
                const auto c0 = rd.c0, z = rd.zeta, d = rd.drive, st = rd.set, g = rd.gain, cc = rd.cellCoef;
                rd = {};
                rd.c0 = c0; rd.zeta = z; rd.drive = d; rd.set = st; rd.gain = g; rd.cellCoef = cc;
                active_[a] = active_.back();
                active_.pop_back();
            }
            else
                ++a;
        }
    }
    void updateWind (double stroke, double draw, double dt)
    {
        double target = 0;
        switch (prm_.wind)
        {
            case Wind::Easy:
                // the pedal is the wind: heel = none, toe = full (a gentle curve, like a swell pedal)
                target = std::pow (pedalSmooth_, 1.4);
                suction_ += (1.0 - std::exp (-dt / 0.03)) * (target - suction_);
                break;
            case Wind::Reservoir:
            {
                // every stroke pumps; the reeds use it up; the safety valve stops it overfilling
                reservoir_ += kPumpGain * stroke * dt - kUse * draw * dt - 0.004 * dt;
                valve_ = reservoir_ > 1.0;
                reservoir_ = std::clamp (reservoir_, 0.0, 1.0);
                // the reservoir's springs hold the suction steady until it's nearly empty
                const double s = std::clamp (reservoir_ / 0.25, 0.0, 1.0);
                target = (s * s * (3.0 - 2.0 * s)) * (0.85 + 0.15 * reservoir_);
                suction_ += (1.0 - std::exp (-dt / 0.05)) * (target - suction_);
                break;
            }
            case Wind::Expression:
            {
                // no reservoir: the strokes themselves are the wind (the bellows smooth them a little)
                target = std::min (1.2, kExprGain * stroke);
                const double tau = target > suction_ ? 0.04 : 0.18;
                suction_ += (1.0 - std::exp (-dt / tau)) * (target - suction_);
                break;
            }
        }
    }
    void pushFir (double x)
    {
        firBuf_[(size_t) firPos_] = x;
        firPos_ = (firPos_ + 1) % kFir;
    }
    double firOut() const
    {
        double y = 0;
        for (int i = 0; i < kFir; ++i)
            y += fir_[(size_t) i] * firBuf_[(size_t) ((firPos_ + i) % kFir)];
        return y;
    }

    double fs_ = 48000.0, fsi_ = 96000.0;
    OrganParams prm_;
    std::vector<Reed> reeds_;
    std::vector<int> active_;
    std::array<double, kKeys * kStops> detune_ {};
    std::array<bool, kKeys> held_ {};
    double suction_ = 0, reservoir_ = 0, pedalPos_ = 0, pedalSmooth_ = 0, pedalVel_ = 0;
    bool valve_ = false;
    double dcX_ = 0, dcY_ = 0, voxPh_ = 0, thump_ = 0;
    std::array<Biquad, 4> body_;
    Biquad swell_, hissBp_, thumpLp_, pumpLp_;
    static constexpr int kFir = 31;
    std::array<double, kFir> fir_ {}, firBuf_ {};
    int firPos_ = 0;
    int64_t clock_ = 0;
    Rng rng_;
};
} // namespace ro
