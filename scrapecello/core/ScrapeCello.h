#pragma once

// Scrape Cello: a physically modelled cello played for texture as much as tone (no JUCE).
//
// Each note is a bowed string (BowedString.h). How it's bowed is the instrument:
//   Pressure  (aftertouch, MPE pressure): relative to each note's own limit. Low: breathy, whistly
//             "surface" sound. Middle: a full clean tone. Past the limit: the stick-slip breaks down into
//             grit, then a grinding, crunching, subharmonic roar.
//   Position  (mod slider, MPE slide per note): sul tasto (over the fingerboard: soft, flutey) to sul
//             ponticello (at the bridge: glassy, metallic, and it takes more pressure to grind).
//   Speed     (expression pedal, scaled by the Speed knob): a slow bow creaks and grinds sooner.
//   Grit      rosin and hair: grain in the friction while it slips.
// Bow changes on each new note, bow attacks that follow velocity (a hard key bites, a soft one swells),
// finger vibrato that comes in on held notes, pitch glides (mono legato, or MPE per note), and the
// strings ringing on briefly when the bow lifts. A cello body (its main air and wood resonances and the
// bridge hill) heard by a close mic.

#include "../../shared/Mpe.h"
#include "BowedString.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace sc
{
struct CelloParams
{
    double speed = 0.55;       // bow speed (0..1), times the pedal once one is moved
    double pressure = 0.45;    // bow pressure with no aftertouch (0..1, relative to each note's limit)
    double pressureRange = 1.0; // how far aftertouch / MPE pressure push it, towards a full grind
    double position = 0.35;    // 0 sul tasto .. 1 sul ponticello
    double grit = 0.4;         // rosin and hair grain (0..1)
    double brightness = 0.6;   // string brightness (0..1)
    double ring = 1.0;         // how long the strings ring after the bow lifts (x)
    double body = 1.0;         // cello body resonance (0..1)
    double vibDepth = 0.18;    // finger vibrato, semitones (comes in on held notes)
    double vibRate = 5.6;      // Hz
    double vibDelay = 0.45;    // s
    double glide = 0.12;       // s: legato slides (mono) and how fast MPE bends follow
    bool mono = false;         // one bow, legato glides; or up to 4 notes, each its own bow
    bool mpe = true;
    double memberBendRange = 48, masterBendRange = 2;
    int pedalCc = 11;          // bow speed pedal; -1 none
    double level = 1.0;
};

class ScrapeCello
{
public:
    static constexpr int kVoices = 4;

    ScrapeCello() = default;

    void prepare (double fs)
    {
        fs_ = fs;
        for (auto& v : voices_)
        {
            v.str = std::make_unique<BowedString>();
            v.str->prepare (fs, 30.0);
        }
        // the body: a cello's main resonances (air, wood, the bridge hill), as parallel resonators
        static const double modes[][3] = { // Hz, Q, gain
            { 98, 10, 1.0 }, { 172, 18, 0.7 }, { 198, 14, 0.9 }, { 290, 16, 0.5 }, { 392, 16, 0.8 }, { 445, 14, 0.6 },
            { 580, 18, 0.5 }, { 745, 14, 0.45 }, { 980, 12, 0.4 }, { 1320, 10, 0.35 }, { 2250, 4, 0.7 }, { 3300, 5, 0.3 } };
        nModes_ = 0;
        for (const auto& m : modes)
        {
            auto& r = modes_[(size_t) nModes_++];
            const double w = 2 * kPi * m[0] / fs, al = std::sin (w) / (2 * m[1]);
            const double a0 = 1 + al;
            r.b0 = al / a0 * m[2];
            r.b2 = -al / a0 * m[2];
            r.a1 = -2 * std::cos (w) / a0;
            r.a2 = (1 - al) / a0;
        }
        hpCoef_ = std::exp (-2 * kPi * 55.0 / fs);
        micCoef_ = 1.0 - std::exp (-2 * kPi * 6500.0 / fs);
        setParams (prm_);
        reset();
    }
    void reset()
    {
        for (auto& v : voices_)
        {
            if (v.str) v.str->reset();
            v.note = -1;
            v.held = false;
            v.bowForce = v.bowTarget = 0;
        }
        for (auto& r : modes_) r.x1 = r.x2 = r.y1 = r.y2 = 0;
        hpX_ = hpY_ = micY_ = 0;
        mpe_.reset();
        sustain_ = false;
    }
    void setParams (const CelloParams& p)
    {
        if (p.memberBendRange != prm_.memberBendRange || ! init_)
            mpe_.lowerMemberRange = mpe_.upperMemberRange = p.memberBendRange;
        if (p.masterBendRange != prm_.masterBendRange || ! init_)
            mpe_.lowerMasterRange = mpe_.upperMasterRange = mpe_.plainRange = p.masterBendRange;
        mpe_.enabled = p.mpe;
        init_ = true;
        prm_ = p;
    }
    const CelloParams& params() const { return prm_; }

    // --- MIDI -----------------------------------------------------------------------------------------
    void midi (const uint8_t* d, int n)
    {
        if (n < 1) return;
        const int st = d[0] & 0xf0, ch = (d[0] & 0x0f) + 1;
        const int d1 = n > 1 ? d[1] & 0x7f : 0, d2 = n > 2 ? d[2] & 0x7f : 0;
        switch (st)
        {
            case 0x90: if (d2 > 0) noteOn (d1, d2 / 127.0, ch); else noteOff (d1, ch); break;
            case 0x80: noteOff (d1, ch); break;
            case 0xe0: mpe_.pitchBend (ch, ((d2 << 7 | d1) - 8192) / 8191.0); break;
            case 0xd0: mpe_.channelPressure (ch, d1 / 127.0); break;
            case 0xa0: poly_[(size_t) d1] = d2 / 127.0; break;
            case 0xb0: controller (ch, d1, d2); break;
            default: break;
        }
    }
    void controller (int ch, int cc, int value)
    {
        if (mpe_.controller (ch, cc, value))
            return;
        if (cc == prm_.pedalCc) pedal_ = value / 127.0;
        else if (cc == 1) mod_ = value / 127.0;
        else if (cc == 64) setSustain (value >= 64);
        else if (cc == 120 || cc == 123) allNotesOff();
    }
    void noteOn (int note, double vel = 0.8, int ch = 1)
    {
        note = std::clamp (note, 24, 96);
        vel = std::clamp (vel, 0.0, 1.0);
        if (prm_.mono)
        {
            heldStack_[(size_t) nHeld_] = note;
            nHeld_ = std::min (nHeld_ + 1, 15);
            auto& v = voices_[0];
            const bool legato = v.held || (v.note >= 0 && v.bowForce > 0.01);
            v.ch = ch;
            if (legato)
            {
                v.note = note; // slide there under the same bow (glide), no new attack
                v.held = true;
                v.sustained = false;
                return;
            }
            start (v, note, vel, ch);
            return;
        }
        // poly: a free string (or the same note re-bowed), else the oldest
        Voice* best = nullptr;
        for (auto& v : voices_)
            if (v.note == note && (v.held || v.sustained)) best = &v;
        if (! best)
            for (auto& v : voices_)
                if (! v.held && ! v.sustained && (! best || v.age < best->age)) best = &v;
        if (! best)
            for (auto& v : voices_)
                if (! best || v.age < best->age) best = &v;
        start (*best, note, vel, ch);
    }
    void noteOff (int note, int ch = -1)
    {
        if (prm_.mono)
        {
            // drop it from the held stack; if another is still held, glide back to it
            int w = 0;
            for (int i = 0; i < nHeld_; ++i)
                if (heldStack_[(size_t) i] != note) heldStack_[(size_t) w++] = heldStack_[(size_t) i];
            nHeld_ = w;
            auto& v = voices_[0];
            if (nHeld_ > 0)
            {
                v.note = heldStack_[(size_t) (nHeld_ - 1)];
                return;
            }
            if (v.note == note || nHeld_ == 0)
                release (v);
            return;
        }
        for (auto& v : voices_)
            if (v.note == note && v.held && (ch < 0 || ! prm_.mpe || v.ch == ch))
                release (v);
    }
    void allNotesOff()
    {
        sustain_ = false;
        nHeld_ = 0;
        for (auto& v : voices_)
        {
            v.held = v.sustained = false;
            v.bowTarget = 0;
        }
    }
    void setSustain (bool on)
    {
        sustain_ = on;
        if (! on)
            for (auto& v : voices_)
                if (v.sustained) { v.sustained = false; v.bowTarget = 0; v.releaseAt = clock_; }
    }

    // --- audio ----------------------------------------------------------------------------------------
    void process (float* out, int n)
    {
        const double dt = 1.0 / fs_;
        const double glideK = 1.0 - std::exp (-dt / std::max (0.005, prm_.glide * 0.35));
        const double ctlK = 1.0 - std::exp (-dt / 0.012);
        for (int i = 0; i < n; ++i)
        {
            // the pedal and mod wheel, smoothed
            pedalS_ += ctlK * (pedal_ - pedalS_);
            modS_ += ctlK * (mod_ - modS_);
            double sum = 0;
            for (auto& v : voices_)
            {
                if (v.note < 0)
                    continue;
                auto& s = *v.str;
                // pitch: the note, its MPE bend, glides, and finger vibrato once it's been held a moment
                const double bend = mpe_.bendSemis (v.ch);
                v.pitchS += glideK * (v.note + bend - v.pitchS);
                const double held = (double) (clock_ - v.onAt) / fs_ - prm_.vibDelay;
                const double vibIn = v.held || v.sustained ? std::clamp (held / 0.6, 0.0, 1.0) : 0.0;
                v.vibPh += prm_.vibRate * v.vibRate * dt;
                v.vibPh -= std::floor (v.vibPh);
                const double vib = prm_.vibDepth * vibIn * std::sin (2 * kPi * v.vibPh);
                const double hz = 440.0 * std::exp2 ((v.pitchS + vib - 69.0) / 12.0);
                // where the bow is: the knob, the mod wheel, and the note's own slide (MPE)
                const double slide = prm_.mpe ? mpe_.timbre (v.ch) - 0.5 : 0.0;
                const double pos = std::clamp (prm_.position + 0.6 * modS_ * (1.0 - prm_.position) + 0.7 * slide, 0.0, 1.0);
                const double beta = 0.22 * std::pow (0.12, pos); // 0.22 (tasto) .. ~0.026 (ponticello)
                v.betaS += ctlK * (beta - v.betaS);
                s.setBowPosition (v.betaS);
                // the bow: speed from the knob and the pedal, pressure from the knob and the player's pressure
                const double press = mpe_.pressure (v.ch, poly_[(size_t) v.note]);
                const double p = std::clamp (prm_.pressure + press * prm_.pressureRange * (1.0 - prm_.pressure), 0.0, 1.0);
                v.bowForce += v.bowRate * (v.bowTarget - v.bowForce); // the bow settling onto / lifting off the string
                const double nominal = (0.04 + 0.36 * prm_.speed) * (0.6 + 0.4 * v.vel); // the bow speed this player means
                const double speed = nominal * pedalS_;
                // the bow change: the bow slows to a stop and turns at each new note
                const double turn = std::clamp ((double) (clock_ - v.onAt) / (0.025 * fs_), 0.0, 1.0);
                const double vb = v.dir * speed * (0.2 + 0.8 * turn) * v.bowForce;
                // pressure relative to this note's own limit: Schelleng's maximum (2Z vb / (beta (muS - muD))),
                // corrected for where it really tips over in this model (lower at the bridge, higher over the
                // fingerboard). Half pressure: a full clean tone; about two thirds: the gritty edge; full: a grind.
                // against the bow speed meant, not the speed now: slow the bow (the pedal) with the same weight
                // on it and it creaks and grinds; and each bow change, as it slows to turn, scratches a moment
                const double fmax = 2.0 * nominal * v.bowForce / (v.betaS * (BowedString::kMuS - BowedString::kMuD)) + 1e-6;
                const double edge = 0.7 * std::clamp (0.55 + 6.0 * v.betaS, 0.5, 2.0);
                const double rel = 0.05 + 2.6 * std::pow (p, 2.2); // 1 = the edge (at about two thirds)
                const double force = fmax * edge * rel;
                s.setBow (vb, force);
                // grain: always a little, more as it nears the edge, a lot past it
                s.setNoise (prm_.grit * (0.15 + 0.5 * std::clamp ((rel - 0.6) / 0.6, 0.0, 1.5)));
                // losses: a bowed note rings; once the bow has lifted a stopped string dies away
                const double t60 = (v.held || v.sustained ? 4.0 : 0.35 + 0.6 * prm_.ring) * std::clamp (220.0 / hz, 0.4, 2.5);
                s.setLoss (std::pow (10.0, -3.0 / (t60 * hz)), 0.35 + 0.6 * prm_.brightness);
                s.setPitch (hz);
                // a grind is louder, but not by its full force: kept within reach
                const double over = std::max (0.0, rel - 1.0);
                // the corner of the string's motion: kept sharp with the bow near the bridge (glassy, bright),
                // rounded off further away (soft, dark): a low-pass at ~2.5 f0 / beta
                const double fc = std::min (0.45 * fs_, hz * 2.5 / v.betaS);
                const double ck = 1.0 - std::exp (-2 * kPi * fc / fs_);
                v.corner += ck * (s.process (rng_.next()) - v.corner);
                sum += v.corner * v.gain / (1.0 + 0.8 * over);
                if (! v.held && ! v.sustained && s.level() < 1e-10 && v.bowForce < 1e-4)
                    v.note = -1;
            }
            // the body, heard by a close mic
            double body = 0;
            for (int m = 0; m < nModes_; ++m)
            {
                auto& r = modes_[(size_t) m];
                const double y = r.b0 * sum + r.b2 * r.x2 - r.a1 * r.y1 - r.a2 * r.y2;
                r.x2 = r.x1; r.x1 = sum; r.y2 = r.y1; r.y1 = y;
                body += y;
            }
            double y = prm_.body * body * 1.6 + (1.0 - 0.6 * prm_.body) * sum * 0.5;
            const double hp = y - hpX_ + hpCoef_ * hpY_;
            hpX_ = y;
            hpY_ = hp;
            micY_ += micCoef_ * (hp - micY_);
            out[i] = (float) (micY_ * 0.5 * prm_.level);
            ++clock_;
        }
    }

    // --- for tests and display ------------------------------------------------------------------------
    int activeVoices() const { int c = 0; for (auto& v : voices_) c += v.note >= 0; return c; }
    double voicePitch (int note) const
    {
        for (auto& v : voices_) if (v.note == note) return v.pitchS;
        return -1;
    }
    bool voiceStuck (int note) const { for (auto& v : voices_) if (v.note == note) return v.str->stuck(); return false; }
    const mpe::Input& mpeInput() const { return mpe_; }

private:
    struct Voice
    {
        std::unique_ptr<BowedString> str;
        int note = -1, ch = 1;
        bool held = false, sustained = false;
        double vel = 0.8, dir = 1, pitchS = 60, betaS = 0.1, bowForce = 0, bowTarget = 0, bowRate = 0.001, gain = 1;
        double vibPh = 0, vibRate = 1, corner = 0;
        int64_t onAt = 0, releaseAt = 0, age = 0;
    };
    void start (Voice& v, int note, double vel, int ch)
    {
        const bool restart = v.note >= 0;
        v.note = note;
        v.ch = ch;
        v.vel = vel;
        v.held = true;
        v.sustained = false;
        v.onAt = clock_;
        v.age = ++ageCounter_;
        v.dir = (v.dir > 0) ? -1.0 : 1.0; // each new note, the bow turns
        if (! restart)
            v.pitchS = note + mpe_.bendSemis (ch);
        v.bowTarget = 1.0;
        // a hard key bites in ~15 ms; a soft one swells in over ~250 ms
        const double attack = 0.25 - 0.235 * vel;
        v.bowRate = 1.0 - std::exp (-1.0 / (attack * fs_));
        v.gain = 0.5 + 0.5 * vel;
        v.vibPh = 0;
        v.vibRate = 1.0 + 0.06 * rng_.next();
        if (! restart)
        {
            v.betaS = 0.1;
            v.str->reset();
        }
    }
    void release (Voice& v)
    {
        v.held = false;
        if (sustain_)
        {
            v.sustained = true;
            return;
        }
        v.bowTarget = 0.0; // the bow lifts
        v.bowRate = 1.0 - std::exp (-1.0 / (0.04 * fs_));
        v.releaseAt = clock_;
    }

    double fs_ = 48000;
    CelloParams prm_;
    bool init_ = false, sustain_ = false;
    std::array<Voice, kVoices> voices_;
    mpe::Input mpe_;
    std::array<double, 128> poly_ {};
    std::array<int, 16> heldStack_ {};
    int nHeld_ = 0;
    int64_t clock_ = 0, ageCounter_ = 0;
    double pedal_ = 1.0, pedalS_ = 1.0, mod_ = 0, modS_ = 0;
    struct Mode { double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::array<Mode, 16> modes_ {};
    int nModes_ = 0;
    double hpCoef_ = 0.99, hpX_ = 0, hpY_ = 0, micCoef_ = 0.5, micY_ = 0;
    Rng rng_;
};
} // namespace sc
