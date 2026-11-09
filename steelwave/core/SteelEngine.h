#pragma once

// Steelwave engine: 10 E9 strings sharing one bar, pedals and knee levers, the chord and
// single-note sets, bridge coupling, vibrato, volume pedal and pickup (no JUCE).
//
// MIDI (the plug-in passes events in with sample offsets; the engine is sample-accurate):
//   C1..G1 (24..31)   keyswitch chord sets 1-8      G#1..B1 (32..35)  single-note sets 1-4
//   C2 and up         play (see Voicing.h for the layout)
//   pitch wheel       bend (chord sets: up = a 4th up, down = a 3rd up; single: next/previous scale note)
//   mod wheel         bar vibrato depth          CC64 sustain (let strings ring after release)
//   volume pedal CC   (11 by default)            CC20..26 A, B, C pedals, LKL, LKR, RKL, RKR (real E9 on top)
//   pedal keys        MIDI 36..42 (the lowest keys of a 61-key board), when on: A B C LKL LKR RKL RKR,
//                     held like pedals, with real travel time (harder = faster)
//   aftertouch        bar vibrato (channel or poly pressure)
//   MPE (when on)     channel 1 is the master (wheel, pedals, CCs); on channels 2-16 each note's own pitch
//                     bend slides the bar (chords) or its string (single notes), its pressure shakes it,
//                     and its slide (CC74) fades it

#include "SteelString.h"
#include "Voicing.h"

namespace sw
{
constexpr int kChordSets = 8, kSingleSets = 4, kSets = kChordSets + kSingleSets;
constexpr int kKeyswitchLo = 24, kKeyswitchHi = 35, kPlayLo = 36;
constexpr int kPedalCcFirst = 20;
constexpr int kPedalKeyLo = 36; // A B C LKL LKR RKL RKR on 36..42 when pedal keys are on

inline const char* setName (int s)
{
    return s < kChordSets ? chordSetDefs()[(size_t) s].name : singleSetDefs()[(size_t) (s - kChordSets)].name;
}

struct EngineParams
{
    int keyPc = 4;              // E
    double glide = 0.25;        // legato bar slide, s
    double strumMs = 12.0;      // pick spread across a chord
    double hardness = 0.75, brightness = 0.5, sustain = 1.0, barNoise = 0.5;
    double vibRate = 5.5, vibDepth = 0.15; // Hz, semitones at full mod wheel
    double sympathetic = 0.5;   // bridge coupling 0..1
    Pickup::Type pickup = Pickup::SingleCoil;
    double levelDb = 0.0;
    int volumeCc = 11;          // < 0: no volume pedal
    enum VolumePedal { Passive250k, Passive500k, Active };
    VolumePedal volumePedal = Passive250k; // passive pedals lose treble part-way down
    double behindBar = 0.5;     // string behind the bar ringing (0 = muted by the hand)

    // --- advanced: the physical model's calibration (defaults = the tuned values) ---
    double pickPosMm = 100, pickupPosMm = 45, apertureMm = 18;
    double pickupHorizontal = 0.8, pickupCurve = 1.0, coilShiftOct = 0.0, coilQ = 1.0, cablePf = 700;
    double stiffness = 1.0, planeDetuneCents = 1.1, planeExchange = 1.0, verticalDecay = 0.55;
    double tensionMod = 1.0, growl = 1.0, bodyDamping = 1.0, bodySize = 1.0, barSoftness = 0.35, behindDecay = 1.0;
    double cabinetDrop = 1.0, stopBounce = 1.0;
    double thumbHardness = 0.65, dragTime = 1.0, pickVariation = 1.0;

    // --- performance controls ---
    bool pedalKeys = true;         // MIDI 36..42 play the pedals and levers
    double pedalSpeed = 1.0;       // x how fast pedal keys travel (hard: ~80 ms, soft: ~250 ms at 1x)
    enum ModWheel { ModVibrato, ModPedalsAB, ModBarSlide, ModOff };
    ModWheel modWheel = ModVibrato;
    bool aftertouchVibrato = true; // pressure shakes the bar
    double swellBelow = 0.0;       // auto swell: notes softer than this velocity fade in (0 = off)
    double swellTime = 0.6;        // s
    double autoVibrato = 0.0;      // vibrato that comes in on its own as a note is held (0..1 of Vib depth)
    double vibDelay = 0.5;         // s before it starts
    double slideInBelow = 0.0;     // slide-in: notes softer than this slide up into pitch (0 = off, 1 = always)
    double slideFrom = 1.0;        // semitones below the note the slide starts
    double slideTime = 0.15;       // s
    double glideScaling = 1.0;     // legato slides take longer for bigger jumps and less for harder playing (0 = fixed)
    double barRange = 2.0;         // frets the mod wheel's bar slide covers
    bool tight = false;            // live: shorter pick drag and strum spread
    bool mpe = false;              // MPE: per-note pitch, pressure and slide on channels 2-16
    double mpeBendRange = 48.0;    // semitones of a member channel's full pitch bend
};

class SteelEngine
{
public:
    SteelEngine()
    {
        (void) chordSets(); // build the voicing tables up front, not on the audio thread
        held_.reserve (128);
    }

    void prepare (double fs)
    {
        fs_ = fs;
        const auto specs = e9Strings();
        for (int i = 0; i < kStrings; ++i)
            str_[(size_t) i].prepare (fs, specs[(size_t) i]);
        pickup_.prepare (fs);
        reset();
    }

    void reset()
    {
        for (auto& s : str_)
            s.reset();
        pickup_.reset();
        held_.clear();
        for (auto& st : sst_)
            st = {};
        nPicks_ = 0;
        chordSlot_ = -1;
        bar_ = 0;
        wheel_ = mod_ = 0;
        ccTravel_.fill (0.0);
        keyTravel_.fill (0.0);
        keyTarget_.fill (0.0);
        keyRate_.fill (1.0);
        swell_.fill (1.0);
        swellRate_.fill (0.0);
        pressure_ = pressureS_ = barSlide_ = mpeBar_ = 0.0;
        lastPick_ = 0;
        barSlideAt_ = -1;
        strSlideAt_.fill (-1);
        mpeOff_.fill (0.0);
        strPress_.fill (0.0);
        strPressS_.fill (0.0);
        strY_.fill (1.0);
        strYS_.fill (1.0);
        noteCh_.fill (0);
        chNote_.fill (-1);
        chBend_.fill (0.0);
        chPress_.fill (0.0);
        chY_.fill (1.0);
        chordCh_ = 0;
        sustainPedal_ = false;
        vol_ = volTarget_ = 1.0;
        volLp_ = 0;
        volG_ = 1.0;
        vibPh_ = driftPh_ = 0;
        clock_ = wheelClock_ = 0;
        ccClock_.fill (0);
        rebuildSingle();
        applyParams();
    }

    void setParams (const EngineParams& p)
    {
        const bool keyChanged = p.keyPc != prm_.keyPc;
        prm_ = p;
        applyParams();
        if (keyChanged)
        {
            stopAll();
            rebuildSingle();
        }
    }
    const EngineParams& params() const { return prm_; }

    void setSet (int s)
    {
        s = std::clamp (s, 0, kSets - 1);
        if (s == set_)
            return;
        stopAll();
        set_ = s;
        rebuildSingle();
    }
    int currentSet() const { return set_; }
    bool chordMode() const { return set_ < kChordSets; }

    // --- MIDI ------------------------------------------------------------------------------
    void noteOn (int note, double vel, int ch = 1)
    {
        if (prm_.mpe && ch >= 2 && ch <= 16 && note >= 0 && note < 128)
        {
            noteCh_[(size_t) note] = ch;
            chNote_[(size_t) ch] = note;
        }
        else if (note >= 0 && note < 128)
            noteCh_[(size_t) note] = 0;
        if (note >= kKeyswitchLo && note <= kKeyswitchHi)
        {
            setSet (note - kKeyswitchLo);
            return;
        }
        if (prm_.pedalKeys && note >= kPedalKeyLo && note < kPedalKeyLo + kControls)
        {
            pedalKey (note - kPedalKeyLo, true, vel);
            return;
        }
        if (note < kPlayLo || note > 127)
            return;
        vel = std::clamp (vel, 0.0, 1.0);
        if (chordMode())
            chordOn (note, vel);
        else
            singleOn (note, vel);
    }
    void noteOff (int note, int /*ch*/ = 1)
    {
        if (prm_.pedalKeys && note >= kPedalKeyLo && note < kPedalKeyLo + kControls)
        {
            pedalKey (note - kPedalKeyLo, false, 0.0);
            return;
        }
        if (note < kPlayLo || note > 127)
            return;
        if (chordMode())
            chordOff (note);
        else
            singleOff (note);
    }
    void pitchWheel (double w, int ch = 1) // -1..1
    {
        w = std::clamp (w, -1.0, 1.0);
        if (prm_.mpe && ch >= 2 && ch <= 16) // a finger's own pitch slide
        {
            chBend_[(size_t) ch] = w;
            applyMpe (ch);
            return;
        }
        const double dt = std::max ((double) (clock_ - wheelClock_) / fs_, 0.001);
        const double speed = std::fabs (w - wheel_) / dt; // travel per second
        const double old = wheel_;
        wheel_ = w;
        wheelClock_ = clock_;
        updatePedals (0.008);
        // the pedals behind the wheel hitting their stops
        for (int side : { 1, -1 })
        {
            const bool arrive = old * side < kStopAt && w * side >= kStopAt;
            const bool leave = old * side > 1.0 - kStopAt && w * side <= 1.0 - kStopAt;
            if (! arrive && ! leave)
                continue;
            for (int s = 0; s < kStrings; ++s)
            {
                const auto& st = sst_[(size_t) s];
                const double moveDir = (side > 0 ? st.up : st.down) - st.rest; // which way this string goes
                if (moveDir != 0.0)
                    str_[(size_t) s].bump ((arrive ? 1.0 : -0.5) * stopBounce (speed) * (moveDir > 0 ? 1.0 : -1.0));
            }
        }
    }
    void controller (int cc, double v, int ch = 1) // v 0..1
    {
        if (prm_.mpe && ch >= 2 && ch <= 16)
        {
            if (cc == 74) // a finger's slide (Y): fades its note
            {
                chY_[(size_t) ch] = v;
                applyMpe (ch);
            }
            return; // other member-channel CCs aren't per-note controls
        }
        if (cc == 1)
        {
            mod_ = v;
            if (prm_.modWheel == EngineParams::ModPedalsAB)
                updatePedals (0.01);
            else if (prm_.modWheel == EngineParams::ModBarSlide)
            {
                barSlide_ = std::clamp (prm_.barRange, 0.0, 12.0) * v;
                for (auto& st : str_)
                    st.setBar (barTarget(), 0.03);
            }
        }
        else if (cc == 64)
        {
            const bool on = v >= 0.5;
            if (sustainPedal_ && ! on)
                releaseUnheld();
            sustainPedal_ = on;
        }
        else if (cc == prm_.volumeCc)
            volTarget_ = v;
        else if (cc >= kPedalCcFirst && cc < kPedalCcFirst + kControls)
        {
            const int k = cc - kPedalCcFirst;
            const double dt = std::max ((double) (clock_ - ccClock_[(size_t) k]) / fs_, 0.001);
            const double speed = std::fabs (v - ccTravel_[(size_t) k]) / dt, old = ccTravel_[(size_t) k];
            ccTravel_[(size_t) k] = v;
            ccClock_[(size_t) k] = clock_;
            updatePedals (0.01);
            const bool arrive = old < kStopAt && v >= kStopAt, leave = old > 1.0 - kStopAt && v <= 1.0 - kStopAt;
            if (arrive || leave)
                for (int s = 0; s < kStrings; ++s)
                    if (const int c = e9Copedent()[(size_t) k][(size_t) s]; c != 0)
                        str_[(size_t) s].bump ((arrive ? 1.0 : -0.5) * stopBounce (speed) * (c > 0 ? 1.0 : -1.0));
        }
        else if (cc == 120 || cc == 123)
            stopAll();
    }
    void allNotesOff() { stopAll(); }
    void aftertouch (double v, int ch = 1)
    {
        v = std::clamp (v, 0.0, 1.0);
        if (prm_.mpe && ch >= 2 && ch <= 16) // a finger's own pressure
        {
            chPress_[(size_t) ch] = v;
            applyMpe (ch);
            return;
        }
        pressure_ = v;
    }

    // --- audio -----------------------------------------------------------------------------
    void process (float* out, int n)
    {
        const double dt = 1.0 / fs_;
        const double kappa = kBridgeGainMax * std::clamp (prm_.sympathetic, 0.0, 1.0);
        const double level = std::pow (10.0, prm_.levelDb / 20.0) * kOutputScale;
        for (int i = 0; i < n; ++i)
        {
            // pedal keys travelling
            if ((clock_ & 31) == 0)
                movePedalKeys (32.0 / fs_);
            // slide-ins: once the pick has gone, the bar (or a string) slides up into the note
            if (barSlideAt_ >= 0 && clock_ >= barSlideAt_)
            {
                barSlideAt_ = -1;
                for (auto& st : str_)
                    st.setBar (barTarget(), slideInSec_);
            }
            for (int k = 0; k < kStrings; ++k)
                if (strSlideAt_[(size_t) k] >= 0 && clock_ >= strSlideAt_[(size_t) k])
                {
                    strSlideAt_[(size_t) k] = -1;
                    str_[(size_t) k].setPedal (offsetFor (k), slideInSec_);
                }
            // scheduled picks (strums)
            for (int k = 0; k < nPicks_;)
                if (picks_[(size_t) k].at <= clock_)
                {
                    pick (picks_[(size_t) k].string, picks_[(size_t) k].vel);
                    picks_[(size_t) k] = picks_[(size_t) --nPicks_];
                }
                else
                    ++k;
            // bar vibrato: a hand rocking the bar, so its rate wanders a little
            driftPh_ += 0.37 * dt;
            vibPh_ += prm_.vibRate * (1.0 + 0.04 * std::sin (2 * kPi * driftPh_)) * dt;
            vibPh_ -= std::floor (vibPh_);
            // how much the bar shakes: the mod wheel, pressure into the keys, or vibrato coming in on its
            // own once a note has been held (the strongest wins)
            pressureS_ += pressureCoef_ * ((prm_.aftertouchVibrato ? pressure_ : 0.0) - pressureS_);
            const double held = (double) (clock_ - lastPick_) / fs_ - prm_.vibDelay;
            const double autoV = prm_.autoVibrato * (held <= 0.0 ? 0.0 : held >= 0.6 ? 1.0 : 0.5 - 0.5 * std::cos (kPi * held / 0.6));
            const double depth = std::max ({ prm_.modWheel == EngineParams::ModVibrato ? mod_ : 0.0, pressureS_, anyHeld() ? autoV : 0.0 });
            const double lfo = prm_.vibDepth * std::sin (2 * kPi * vibPh_);
            const double vib = depth * lfo;
            double bsum = 0;
            for (auto& s : str_)
                bsum += s.bridge();
            double sum = 0;
            for (int k = 0; k < kStrings; ++k)
            {
                auto& s = str_[(size_t) k];
                // MPE: each finger's pressure shakes its own string(s), its slide fades them
                strPressS_[(size_t) k] += pressureCoef_ * (strPress_[(size_t) k] - strPressS_[(size_t) k]);
                s.setVibrato (strPressS_[(size_t) k] > depth ? strPressS_[(size_t) k] * lfo : vib);
                strYS_[(size_t) k] += pressureCoef_ * (strY_[(size_t) k] - strYS_[(size_t) k]);
                const double y = s.process (kappa * (bsum - s.bridge())) * strYS_[(size_t) k];
                // auto swell: a soft note fades in, like a volume pedal under that string
                double& sg = swell_[(size_t) k];
                if (sg < 1.0)
                {
                    sg = std::min (1.0, sg + swellRate_[(size_t) k]);
                    sum += y * sg * sg;
                }
                else
                    sum += y;
            }
            vol_ += volCoef_ * (volTarget_ - vol_);
            const double g = std::pow (std::clamp (vol_, 0.0, 1.0), 2.5); // volume pedals are strongly tapered
            if ((clock_ & 15) == 0)
                updateVolumeTone (g);
            double x = pickup_.process ((float) sum);
            volLp_ += volG_ * (x - volLp_); // the pot's resistance into the cable's capacitance
            x = volLp_;
            out[i] = (float) (x * level * g);
            ++clock_;
        }
    }

    // --- for display -------------------------------------------------------------------------
    int bar() const { return bar_; }
    int chordSlot() const { return chordSlot_; }
    double wheel() const { return wheel_; }
    double volume() const { return vol_; }
    bool stringSounding (int s) const { return str_[(size_t) s].isSounding(); }
    double stringLevel (int s) const { return str_[(size_t) s].level(); }
    double controlTravel (int k) const { return travel()[(size_t) k]; }
    double stringSemis (int s) const { return str_[(size_t) s].currentSemis(); }

    static constexpr double kOutputScale = 0.12;
    static constexpr double kLandSec = 0.006;
    static constexpr double kIdleContact = 0.2;
    static constexpr double kCabinetDrop = 0.026; // semitones per string's worth of added tension (A+B ~ -2 cents)
    static constexpr double kStopBounce = 0.03;   // semitones of overshoot from a fast hit
    static constexpr double kStopAt = 0.995;


private:
    struct StringState
    {
        double rest = 0, up = 0, down = 0; // pedal offsets (semitones) at rest / full wheel up / down
        int owner = -1;                     // single sets: the key playing this string
        bool inChord = false;
    };
    struct Pick { int64_t at; int string; double vel; };

    void applyParams()
    {
        StringParams sp;
        sp.hardness = prm_.hardness;
        sp.brightness = prm_.brightness;
        sp.sustain = prm_.sustain;
        sp.barNoise = prm_.barNoise;
        sp.behindBar = prm_.behindBar;
        sp.pickPosM = prm_.pickPosMm * 1e-3;
        sp.pickupPosM = prm_.pickupPosMm * 1e-3;
        sp.apertureM = prm_.apertureMm * 1e-3;
        sp.pickupHorizontal = prm_.pickupHorizontal;
        sp.pickupNonlinear = prm_.pickupCurve;
        sp.stiffness = prm_.stiffness;
        sp.polDetuneCents = prm_.planeDetuneCents;
        sp.planeExchange = prm_.planeExchange;
        sp.verticalDecay = prm_.verticalDecay;
        sp.nonlinear = prm_.tensionMod;
        sp.growl = prm_.growl;
        sp.bodyDamping = prm_.bodyDamping;
        sp.bodySize = prm_.bodySize;
        sp.barSoftness = prm_.barSoftness;
        sp.behindDecay = prm_.behindDecay;
        sp.thumbHardness = prm_.thumbHardness;
        sp.dragTime = prm_.dragTime;
        sp.pickVariation = prm_.pickVariation;
        if (prm_.tight) // live: the pick lets go sooner and catches a ringing string more briefly
        {
            sp.dragTime *= 0.3;
            sp.catchMs = 1.5;
        }
        for (auto& s : str_)
            s.setParams (sp);
        pickup_.set (prm_.pickup, prm_.coilShiftOct, prm_.coilQ);
        volCoef_ = 1.0 - std::exp (-1.0 / (0.01 * fs_));
        pressureCoef_ = 1.0 - std::exp (-1.0 / (0.03 * fs_)); // aftertouch arrives in steps: smooth it
        if (prm_.volumeCc < 0)
            volTarget_ = 1.0;
    }

    // where this string's pedals and levers have it, before cabinet drop
    double rawOffset (int s) const
    {
        const auto& st = sst_[(size_t) s];
        double o = tensionGlide (st.rest, wheel_ >= 0 ? st.up : st.down, std::fabs (wheel_));
        o += cc_[(size_t) s] + mpeOff_[(size_t) s];
        return std::clamp (o, -4.0, 6.0);
    }
    // Cabinet drop: strings pulled sharp add tension to the cabinet, which gives a little, so every
    // string sags slightly flat (A+B: ~2 cents); lowered strings relieve it.
    double cabinetDrop() const
    {
        double extra = 0;
        for (int s = 0; s < kStrings; ++s)
            extra += std::exp2 (rawOffset (s) / 6.0) - 1.0; // added tension, in units of one string's
        return -kCabinetDrop * std::clamp (prm_.cabinetDrop, 0.0, 5.0) * extra;
    }
    double offsetFor (int s) const { return std::clamp (rawOffset (s) + cabinetDrop(), -4.0, 6.0); }
    // a fast move hits the stop harder: up to ~3 cents of overshoot from a quick stab, none when eased in
    double stopBounce (double travelPerSec) const
    {
        return kStopBounce * std::clamp (prm_.stopBounce, 0.0, 5.0) * std::clamp ((travelPerSec - 3.0) / 30.0, 0.0, 1.0);
    }
    void retune (double glide, int except = -1)
    {
        for (int s = 0; s < kStrings; ++s)
            if (s != except)
                str_[(size_t) s].setPedal (offsetFor (s), glide);
    }
    void updatePedals (double glide)
    {
        const auto c = copedentOffsets (travel());
        for (int s = 0; s < kStrings; ++s)
            cc_[(size_t) s] = c[(size_t) s];
        for (int s = 0; s < kStrings; ++s)
            str_[(size_t) s].setPedal (offsetFor (s), glide);
    }

    void block (int s) { str_[(size_t) s].setDamp (1.0); cancelPicks (s); }
    void cancelPicks (int s)
    {
        for (int k = 0; k < nPicks_;)
            if (picks_[(size_t) k].string == s) picks_[(size_t) k] = picks_[(size_t) --nPicks_];
            else ++k;
    }
    void schedule (int s, double delaySec, double vel)
    {
        cancelPicks (s);
        if (nPicks_ < (int) picks_.size())
            picks_[(size_t) nPicks_++] = { clock_ + (int64_t) (delaySec * fs_), s, vel };
    }
    double humanize() { return rng_.next(); } // -1..1

    // the bar is lifted and put down somewhere else: anything still ringing is blocked first
    void moveBarTo (int bar, bool legato, double glideSec = -1.0)
    {
        if (glideSec < 0)
            glideSec = prm_.glide;
        if (bar == bar_ && ! legato)
            return;
        for (int s = 0; s < kStrings; ++s)
        {
            if (legato)
                str_[(size_t) s].setBar (bar + barSlide_ + mpeBar_, glideSec);
            else
                str_[(size_t) s].setBar (bar + barSlide_ + mpeBar_, kLandSec * 0.8, false);
        }
        bar_ = bar;
    }

    // what each pedal/lever is doing: the furthest of its CC, its pedal key and (A, B) the mod wheel
    std::array<double, kControls> travel() const
    {
        std::array<double, kControls> t {};
        for (int k = 0; k < kControls; ++k)
            t[(size_t) k] = std::max (ccTravel_[(size_t) k], keyTravel_[(size_t) k]);
        if (prm_.modWheel == EngineParams::ModPedalsAB)
        {
            t[PedA] = std::max (t[PedA], mod_);
            t[PedB] = std::max (t[PedB], mod_);
        }
        return t;
    }
    void pedalKey (int k, bool down, double vel)
    {
        keyTarget_[(size_t) k] = down ? 1.0 : 0.0;
        // a pedal pressed hard goes down in ~80 ms, gently in ~250 ms; released, it springs back in ~150 ms
        const double sec = (down ? 0.25 - 0.17 * std::clamp (vel, 0.0, 1.0) : 0.15) / std::clamp (prm_.pedalSpeed, 0.2, 5.0);
        keyRate_[(size_t) k] = 1.0 / sec;
    }
    void movePedalKeys (double dt)
    {
        bool moved = false;
        for (int k = 0; k < kControls; ++k)
        {
            double& x = keyTravel_[(size_t) k];
            const double tgt = keyTarget_[(size_t) k];
            if (x == tgt)
                continue;
            const double old = x;
            x = tgt > x ? std::min (tgt, x + keyRate_[(size_t) k] * dt) : std::max (tgt, x - keyRate_[(size_t) k] * dt);
            moved = true;
            const bool arrive = old < kStopAt && x >= kStopAt, leave = old > 1.0 - kStopAt && x <= 1.0 - kStopAt;
            if (arrive || leave)
                for (int s = 0; s < kStrings; ++s)
                    if (const int c = e9Copedent()[(size_t) k][(size_t) s]; c != 0)
                        str_[(size_t) s].bump ((arrive ? 1.0 : -0.5) * stopBounce (keyRate_[(size_t) k]) * (c > 0 ? 1.0 : -1.0));
        }
        if (moved)
            updatePedals (1.5 * dt);
    }
    // every pick goes through here: soft notes swell in when auto swell is on
    void pick (int s, double vel)
    {
        lastPick_ = clock_;
        if (prm_.swellBelow > 0.0 && vel < prm_.swellBelow)
        {
            swell_[(size_t) s] = 0.0;
            swellRate_[(size_t) s] = 1.0 / (std::max (prm_.swellTime, 0.02) * fs_);
            vel = std::max (vel, 0.6); // the attack is hidden under the swell: pick it firmly
        }
        else
            swell_[(size_t) s] = 1.0;
        str_[(size_t) s].pluck (vel);
    }
    bool anyHeld() const { return ! held_.empty() || sustainPedal_; }
    double barTarget() const { return bar_ + barSlide_ + mpeBar_; }
    bool slideIn (double vel) const { return prm_.slideInBelow > 0.0 && (prm_.slideInBelow >= 1.0 || vel < prm_.slideInBelow); }
    // legato slides: a bigger jump takes a little longer, harder playing is quicker
    double glideFor (int frets, double vel) const
    {
        const double feel = (0.55 + 0.45 * std::sqrt (frets / 5.0)) * (1.25 - 0.5 * std::clamp (vel, 0.0, 1.0));
        const double g = std::clamp (prm_.glideScaling, 0.0, 1.0);
        return prm_.glide * ((1.0 - g) + g * feel);
    }
    // MPE slide (CC74): at or above the middle the note is full; below, it fades
    static double yGain (double y) { return std::clamp (y / 0.5, 0.0, 1.0); }
    // a member channel moved: apply it to its note's chord or string
    void applyMpe (int ch)
    {
        const int note = chNote_[(size_t) ch];
        if (note < 0)
            return;
        const bool held = std::find (held_.begin(), held_.end(), note) != held_.end();
        if (chordMode())
        {
            if (! held || held_.back() != note)
                return;
            mpeBar_ = chBend_[(size_t) ch] * prm_.mpeBendRange;
            for (auto& st : str_)
                st.setBar (barTarget(), 0.006);
            pressure_ = chPress_[(size_t) ch];
            for (int s = 0; s < kStrings; ++s)
                if (sst_[(size_t) s].inChord)
                    strY_[(size_t) s] = yGain (chY_[(size_t) ch]);
            return;
        }
        for (int s = 0; s < kStrings; ++s)
            if (sst_[(size_t) s].owner == note)
            {
                mpeOff_[(size_t) s] = chBend_[(size_t) ch] * prm_.mpeBendRange;
                strPress_[(size_t) s] = chPress_[(size_t) ch];
                strY_[(size_t) s] = yGain (chY_[(size_t) ch]);
                str_[(size_t) s].setPedal (offsetFor (s), 0.006);
            }
    }

    void stopAll()
    {
        for (int s = 0; s < kStrings; ++s)
        {
            block (s);
            sst_[(size_t) s] = {};
        }
        nPicks_ = 0;
        held_.clear();
        chordSlot_ = -1;
        updatePedals (0.01);
    }
    void releaseUnheld()
    {
        if (chordMode())
        {
            if (held_.empty())
                for (int s = 0; s < kStrings; ++s)
                    block (s);
        }
        else
            for (int s = 0; s < kStrings; ++s)
                if (sst_[(size_t) s].owner < 0)
                    block (s);
    }

    // --- chord sets ------------------------------------------------------------------------
    void chordOn (int note, double vel)
    {
        held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
        const bool legato = ! held_.empty();
        held_.push_back (note);
        playChord (note % 12, vel, legato, true, note);
    }
    void chordOff (int note)
    {
        const bool wasTop = ! held_.empty() && held_.back() == note;
        held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
        if (! held_.empty())
        {
            if (wasTop) // back to the chord still held: slide there, nothing new picked
                playChord (held_.back() % 12, 0.0, true, false, held_.back());
            return;
        }
        // last key up: a palm block over all the strings (sympathetic ringing included)
        if (! sustainPedal_)
            for (int s = 0; s < kStrings; ++s)
                block (s);
    }
    void playChord (int slot, double vel, bool legato, bool pickNew, int note = -1)
    {
        const auto& v = chordSets()[(size_t) set_][(size_t) slot];
        const int bar = fitBar (v.bar + keyShift (prm_.keyPc));
        const bool barMoves = bar != bar_;
        const double glide = glideFor (std::abs (bar - bar_), pickNew ? vel : 0.6);
        // MPE: the chord follows its note's finger (bend = bar slide, pressure, slide = fade)
        chordCh_ = note >= 0 ? noteCh_[(size_t) note] : chordCh_;
        mpeBar_ = chordCh_ > 0 ? chBend_[(size_t) chordCh_] * prm_.mpeBendRange : 0.0;
        bool inNew[kStrings] = {}, wasIn[kStrings] = {};
        for (int s = 0; s < kStrings; ++s)
            wasIn[s] = sst_[(size_t) s].inChord;
        for (int s : v.strings)
            inNew[s] = true;
        // strings leaving the chord, or that would be dragged along by a lifted bar, are blocked
        for (int s = 0; s < kStrings; ++s)
        {
            if (inNew[s])
                continue;
            if (sst_[(size_t) s].inChord || (barMoves && ! legato))
                block (s);
            sst_[(size_t) s] = {};
        }
        // the bar's contact noise comes mostly from the strings being played
        for (int s = 0; s < kStrings; ++s)
            str_[(size_t) s].setContact (inNew[s] ? 1.0 : kIdleContact);
        for (int s : v.strings)
        {
            auto& st = sst_[(size_t) s];
            st.rest = v.rest[(size_t) s];
            st.up = v.up[(size_t) s];
            st.down = v.down[(size_t) s];
            st.inChord = true;
            st.owner = -1;
        }
        for (int s = 0; s < kStrings; ++s)
        {
            mpeOff_[(size_t) s] = 0.0;
            strPress_[(size_t) s] = 0.0;
            strY_[(size_t) s] = chordCh_ > 0 && inNew[s] ? yGain (chY_[(size_t) chordCh_]) : 1.0;
        }
        if (chordCh_ > 0)
            pressure_ = chPress_[(size_t) chordCh_];
        retune (legato ? glide : 0.004); // the chord's strings, and everyone's cabinet drop
        moveBarTo (bar, legato, glide);
        chordSlot_ = slot;
        // slide-in: a soft chord starts below and slides up into place once it's picked
        if (pickNew && ! legato && slideIn (vel))
        {
            const double from = std::clamp (prm_.slideFrom, 0.25, 5.0);
            const double start = bar - from >= 0 ? barTarget() - from : barTarget() + from; // at the nut: slide down into it
            for (auto& st : str_)
                st.setBar (start, kLandSec * 0.8, false);
            barSlideAt_ = clock_ + (int64_t) ((kLandSec + 0.004) * fs_);
            slideInSec_ = std::clamp (prm_.slideTime, 0.02, 2.0);
        }
        // a lifted bar takes a few ms to land: pick after that, or the attack would scoop
        if (! pickNew)
            return;
        // pick: low string first; on a legato slide only the strings that aren't already ringing
        int order[kStrings];
        const int no = std::min ((int) v.strings.size(), kStrings);
        std::copy (v.strings.begin(), v.strings.begin() + no, order);
        std::sort (order, order + no, [] (int a, int b) { return kE9Open[(size_t) a] < kE9Open[(size_t) b]; });
        const double spread = prm_.strumMs * 1e-3 * chordSetDefs()[(size_t) set_].strumScale * (prm_.tight ? 0.4 : 1.0);
        double t = (barMoves || barSlideAt_ >= 0) && ! legato ? kLandSec : 0.0;
        for (int k = 0; k < no; ++k)
        {
            const int s = order[k];
            // a slide carries the strings already ringing in the chord, however quiet: never re-picked
            if (legato && wasIn[s] && str_[(size_t) s].isRinging())
                continue;
            schedule (s, t + 0.002 * std::fabs (humanize()), vel * (legato ? 0.6 : 1.0) * (1.0 + 0.06 * humanize()));
            t += spread;
        }
    }

    // --- single-note sets --------------------------------------------------------------------
    void rebuildSingle()
    {
        if (! chordMode())
            single_.build (set_ - kChordSets, prm_.keyPc);
    }
    void singleOn (int note, double vel)
    {
        const SingleNote n = single_.lookup (note);
        if (n.string < 0)
            return;
        double delay = 0;
        if (single_.bar() != bar_)
        {
            for (int s = 0; s < kStrings; ++s)
                block (s);
            moveBarTo (single_.bar(), false);
            delay = kLandSec;
        }
        held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
        const bool legato = ! held_.empty();
        auto& st = sst_[(size_t) n.string];
        const bool sameString = legato && st.owner >= 0 && st.owner != note;
        held_.push_back (note);
        st.owner = note;
        st.inChord = false;
        st.rest = n.offset;
        st.up = n.bends ? n.offset + n.bendUp : n.offset;
        st.down = n.bends ? n.offset + n.bendDown : n.offset;
        // MPE: this string follows its note's finger
        const int ch = noteCh_[(size_t) note];
        mpeOff_[(size_t) n.string] = ch > 0 ? chBend_[(size_t) ch] * prm_.mpeBendRange : 0.0;
        strPress_[(size_t) n.string] = ch > 0 ? chPress_[(size_t) ch] : 0.0;
        strY_[(size_t) n.string] = ch > 0 ? yGain (chY_[(size_t) ch]) : 1.0;
        if (sameString) // a legato line on one string: slide there instead of picking again
        {
            str_[(size_t) n.string].setPedal (offsetFor (n.string), glideFor (0, vel) * 0.6);
            retune (0.01, n.string);
            return;
        }
        // slide-in: a soft note starts below and slides up into pitch once it's picked
        const bool slide = slideIn (vel);
        str_[(size_t) n.string].setPedal (offsetFor (n.string) - (slide ? std::clamp (prm_.slideFrom, 0.25, 3.0) : 0.0), 0.003);
        if (slide)
        {
            strSlideAt_[(size_t) n.string] = clock_ + (int64_t) ((delay + 0.004) * fs_);
            slideInSec_ = std::clamp (prm_.slideTime, 0.02, 2.0);
        }
        retune (0.01, n.string);
        cancelPicks (n.string);
        if (delay > 0)
            schedule (n.string, delay, vel);
        else
            pick (n.string, vel);
    }
    void singleOff (int note)
    {
        held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
        for (int s = 0; s < kStrings; ++s)
            if (sst_[(size_t) s].owner == note)
            {
                sst_[(size_t) s].owner = -1;
                if (! sustainPedal_)
                    block (s);
            }
    }

    double fs_ = 48000.0;
    EngineParams prm_;
    std::array<SteelString, kStrings> str_;
    std::array<StringState, kStrings> sst_ {};
    std::array<double, kStrings> cc_ {};
    std::array<double, kControls> ccTravel_ {}, keyTravel_ {}, keyTarget_ {}, keyRate_ {};
    std::array<double, kStrings> swell_ {}, swellRate_ {};
    double pressure_ = 0, pressureS_ = 0, pressureCoef_ = 0.001, barSlide_ = 0, mpeBar_ = 0, slideInSec_ = 0.15;
    int64_t barSlideAt_ = -1;
    std::array<int64_t, kStrings> strSlideAt_ {};
    std::array<double, kStrings> mpeOff_ {}, strPress_ {}, strPressS_ {}, strY_ {}, strYS_ {};
    std::array<int, 128> noteCh_ {};
    std::array<int, 17> chNote_ {};
    std::array<double, 17> chBend_ {}, chPress_ {}, chY_ {};
    int chordCh_ = 0;
    int64_t lastPick_ = 0;
    std::array<Pick, 64> picks_ {};
    int nPicks_ = 0;
    Pickup pickup_;
    SingleMap single_;
    std::vector<int> held_; // reserved up front: never reallocates on the audio thread
    int set_ = 0, chordSlot_ = -1, bar_ = 0;
    // Passive volume pedal: a pot between the pickup and the cable. Part-way down, the resistance it
    // leaves in the signal path (the two halves of the pot in parallel, R t (1 - t), t = the divider
    // ratio) and the cable's capacitance (~700 pF, 20 ft) make a low-pass: darkest around half way
    // (250k: ~3.6 kHz), open at both ends. An active pedal has no loss.
    void updateVolumeTone (double t)
    {
        double fc = 1e9;
        if (prm_.volumePedal != EngineParams::Active)
        {
            const double R = prm_.volumePedal == EngineParams::Passive500k ? 500e3 : 250e3;
            const double C = std::clamp (prm_.cablePf, 50.0, 3000.0) * 1e-12;
            const double rth = R * t * (1.0 - t);
            fc = 1.0 / (2.0 * kPi * std::max (rth, 1.0) * C);
        }
        volG_ = fc >= 0.45 * fs_ ? 1.0 : 1.0 - std::exp (-2.0 * kPi * fc / fs_);
    }
    double volLp_ = 0, volG_ = 1.0;
    double wheel_ = 0, mod_ = 0, vol_ = 1, volTarget_ = 1, volCoef_ = 0.002, vibPh_ = 0, driftPh_ = 0;
    bool sustainPedal_ = false;
    int64_t clock_ = 0, wheelClock_ = 0;
    std::array<int64_t, kControls> ccClock_ {};
    Rng rng_;
};
} // namespace sw
