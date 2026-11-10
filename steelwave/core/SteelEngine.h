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
constexpr int kChordSets = 8, kSingleSets = 4, kSmartSet = kChordSets + kSingleSets, kSets = kSmartSet + 1;
constexpr int kKeyswitchLo = 24, kKeyswitchHi = 35, kPlayLo = 36;
constexpr int kSmartKeyswitch = 23; // B0: smart notes
constexpr int kSmartLo = 45, kSmartHi = 95; // what the neck can sound in smart notes; other notes fold by octaves
constexpr int kPedalCcFirst = 20;
constexpr int kPedalKeyLo = 36; // A B C LKL LKR RKL RKR on 36..42 when pedal keys are on

inline const char* setName (int s)
{
    if (s >= kSmartSet)
        return "Smart notes";
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

    // --- smart notes, tuning ---
    bool autoBend = true;          // a note played a step from a held one bends that string to it (like a pedal)
    bool legatoSlide = true;       // a single line played legato slides along its string
    double sweeten = 0.0;          // 0..1: thirds and fifths tuned pure against the chord's root, as steel players tune
    bool wheelMelody = false;      // pitch wheel: only the top note moves, a scale step up or down; the rest hold
};

class SteelEngine
{
public:
    SteelEngine()
    {
        (void) chordSets(); // build the voicing tables up front, not on the audio thread
        held_.reserve (128);
        // what each string's pedals and levers can really do, and every playable combination
        const auto& combos = playableCombos();
        nCombos_ = std::min ((int) combos.size(), (int) comboOff_.size());
        for (int c = 0; c < nCombos_; ++c)
        {
            comboOff_[(size_t) c] = copedentOffsets (combos[(size_t) c]);
            comboCount_[(size_t) c] = controlCount (combos[(size_t) c]);
            for (int st = 0; st < kStrings; ++st)
                reach_[(size_t) st] |= 1u << (comboOff_[(size_t) c][(size_t) st] + 4);
        }
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
        smOff_.fill (0);
        smPitch_.fill (0);
        smOnset_.fill (0);
        smVel_.fill (0.7);
        for (auto& g : smGhost_)
            g.n = 0;
        sweet_.fill (0.0);
        groupStart_ = -1000000;
        smRoot_ = -1;
        smQual_ = 0;
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
    bool smartMode() const { return set_ == kSmartSet; }
    // smart notes: the chord being held, for display (root pitch class, or -1; 0 major, 1 minor, 2 diminished)
    int smartRoot() const { return smRoot_; }
    int smartQuality() const { return smQual_; }

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
        if (note == kSmartKeyswitch)
        {
            setSet (kSmartSet);
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
        if (smartMode())
            smartOn (note, vel);
        else if (chordMode())
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
        if (smartMode())
            smartOff (note);
        else if (chordMode())
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
    int stringKey (int s) const { return sst_[(size_t) s].owner; } // smart and single notes: the key playing this string, or -1

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
        o += cc_[(size_t) s] + mpeOff_[(size_t) s] + sweet_[(size_t) s];
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
        smOff_.fill (0);
        for (auto& g : smGhost_)
            g.n = 0;
        sweet_.fill (0.0);
        smRoot_ = -1;
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
        const int bar = chordBar (set_, v, prm_.keyPc, note);
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
            // sweetened against each chord's own root (the chord, and what the wheel bends it to)
            const auto& sl = chordSlots()[(size_t) slot];
            auto sweet = [&] (double off, ChordName c) {
                const int pc = (kE9Open[(size_t) s] + v.bar + (int) std::lround (off) + 1200) % 12;
                return prm_.sweeten * sweetCents ((pc - (4 + c.root) % 12 + 12) % 12) * 0.01;
            };
            st.rest = v.rest[(size_t) s] + sweet (v.rest[(size_t) s], sl.rest);
            st.up = v.up[(size_t) s] + sweet (v.up[(size_t) s], sl.up);
            st.down = v.down[(size_t) s] + sweet (v.down[(size_t) s], sl.down);
            st.inChord = true;
            st.owner = -1;
        }
        if (prm_.wheelMelody)
        {
            // the wheel moves only the chord's top note, a scale step up or down; the rest hold
            int top = -1;
            for (int s : v.strings)
                if (top < 0 || kE9Open[(size_t) s] + v.rest[(size_t) s] > kE9Open[(size_t) top] + v.rest[(size_t) top])
                    top = s;
            for (int s : v.strings)
            {
                auto& st = sst_[(size_t) s];
                st.up = st.down = st.rest;
                if (s != top)
                    continue;
                const int pitch = kE9Open[(size_t) s] + bar + (int) std::lround (v.rest[(size_t) s]);
                const double u = st.rest + scaleStep (pitch, +1) - pitch, d = st.rest + scaleStep (pitch, -1) - pitch;
                st.up = u <= 6.0 ? u : st.rest;
                st.down = d >= -4.0 ? d : st.rest;
            }
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

    // --- smart notes ---------------------------------------------------------------------------
    // Play ordinary notes and chords; the engine finds them on the neck. One bar for all strings;
    // each note goes on the string that needs the least pulling at that bar (real pedal and lever
    // pulls preferred, a little extra pull where the real instrument has none).
    //  - notes arriving together (a chord) choose the bar together
    //  - a later note a half or whole step from a held one bends that string to it, like a pedal;
    //    letting it go while the first key is still down bends back, like releasing the pedal
    //  - a single line played legato slides along its string
    //  - the pitch wheel: with a chord held, up takes the third and fifth up a scale step (I -> IV),
    //    down takes the root down a step (I -> iii); with no chord, every note moves a scale step

    // cents from equal temperament for an interval above the root, tuned pure
    static double sweetCents (int interval)
    {
        switch (interval)
        {
            case 3: return 15.6;   // minor third 6:5
            case 4: return -13.7;  // major third 5:4
            case 7: return 2.0;    // fifth 3:2
            case 8: return 13.7;   // minor sixth 8:5
            case 9: return -15.6;  // major sixth 5:3
            case 11: return -11.7; // major seventh 15:8
            default: return 0.0;
        }
    }
    struct Chord { int root = -1; int qual = 0; }; // root pitch class, 0 maj 1 min 2 dim
    // the triad (or triad with a seventh) these pitches belong to, if any
    static Chord detectChord (const int* pitches, int n)
    {
        uint32_t set = 0;
        int lowest = 1000;
        for (int i = 0; i < n; ++i)
        {
            set |= 1u << (pitches[i] % 12);
            lowest = std::min (lowest, pitches[i]);
        }
        Chord best;
        int bestScore = -1;
        static const int third[3] = { 4, 3, 3 }, fifth[3] = { 7, 7, 6 };
        for (int r = 0; r < 12; ++r)
            for (int q = 0; q < 3; ++q)
            {
                const uint32_t rt = 1u << r, th = 1u << ((r + third[q]) % 12), fi = 1u << ((r + fifth[q]) % 12);
                const uint32_t sevenths = (1u << ((r + 10) % 12)) | (1u << ((r + 11) % 12));
                if ((set & ~(rt | th | fi | sevenths)) != 0 || ! (set & rt))
                    continue;
                const bool hasTh = set & th, hasFi = set & fi;
                if (! hasTh && ! hasFi)
                    continue;
                if (! hasTh && q != 0) // a bare fifth: call it major
                    continue;
                int score = (hasTh ? 4 : 0) + (hasFi ? 2 : 0) + (lowest % 12 == r ? 1 : 0) + (q == 2 ? -1 : 0);
                if (score > bestScore)
                {
                    bestScore = score;
                    best.root = r;
                    best.qual = q;
                }
            }
        return best;
    }
    bool inKey (int p) const
    {
        static const bool sc[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
        return sc[((p - prm_.keyPc) % 12 + 12) % 12];
    }
    int scaleStep (int p, int dir) const
    {
        for (int q = p + dir;; q += dir)
            if (inKey (q))
                return q;
    }
    static double offCost (int s, int off, uint32_t reach)
    {
        if (off == 0) return 0.0;
        if (off >= -4 && off <= 6 && (reach & (1u << (off + 4)))) return 1.0 + 0.1 * std::abs (off);
        if (off >= -2 && off <= 3) return 2.5 + 0.3 * std::abs (off);
        if (off >= -4 && off <= 6) return 6.0;
        (void) s;
        return 1e6;
    }
    // the middle strings first: they leave strings free above and below for the rest of a chord
    static double stringPref (int s)
    {
        static const double pref[kStrings] = { 0.9, 0.9, 0.5, 0.2, 0.0, 0.0, 0.4, 0.4, 0.6, 0.6 };
        return pref[s];
    }
    int ownedCount() const
    {
        int n = 0;
        for (const auto& st : sst_)
            n += st.owner >= 0;
        return n;
    }
    bool othersRinging (int except) const
    {
        for (int s = 0; s < kStrings; ++s)
            if (s != except && (sst_[(size_t) s].owner >= 0 || str_[(size_t) s].isRinging()))
                return true;
        return false;
    }
    // the bar steps to another fret with no change of any sounding pitch (the pulls take up the difference)
    uint32_t revoice (int newBar)
    {
        const int d = newBar - bar_;
        if (d == 0)
            return 0;
        uint32_t moved = 0;
        bar_ = newBar;
        for (int s = 0; s < kStrings; ++s)
        {
            if (sst_[(size_t) s].owner >= 0)
            {
                smOff_[(size_t) s] -= d;
                moved |= 1u << s;
            }
            str_[(size_t) s].setBar (barTarget(), 0.0, false);
        }
        return moved;
    }
    // set every played string's wheel targets and sweetening from what is held; `moved` glides in `sec`
    void analyse (int moved = -1, double sec = 0.03, uint32_t jumpMask = 0)
    {
        int strs[kStrings], rest[kStrings], up[kStrings], down[kStrings], n = 0;
        for (int s = 0; s < kStrings; ++s)
            if (sst_[(size_t) s].owner >= 0)
            {
                strs[n] = s;
                rest[n++] = smPitch_[(size_t) s];
            }
        const Chord c = detectChord (rest, n);
        smRoot_ = n >= 2 ? c.root : -1;
        smQual_ = c.qual;
        int top = 0;
        for (int i = 1; i < n; ++i)
            if (rest[i] > rest[top])
                top = i;
        for (int i = 0; i < n; ++i)
        {
            up[i] = down[i] = rest[i];
            if (prm_.wheelMelody) // the melody note moves along the scale; the chord under it holds
            {
                if (i == top)
                {
                    up[i] = scaleStep (rest[i], +1);
                    down[i] = scaleStep (rest[i], -1);
                }
                continue;
            }
            if (c.root < 0 || n < 2)
            {
                up[i] = scaleStep (rest[i], +1);
                down[i] = scaleStep (rest[i], -1);
                continue;
            }
            const int iv = ((rest[i] - c.root) % 12 + 12) % 12;
            if (iv == 3 || iv == 4 || iv == 6 || iv == 7)
                up[i] = scaleStep (rest[i], +1);
            if (iv == 0)
                down[i] = scaleStep (rest[i], -1);
        }
        auto sweetSet = [&] (const int* pitches, double* out) {
            const Chord cc = detectChord (pitches, n);
            int root = cc.root;
            if (root < 0 && n >= 2) // no triad: tune against the lowest note
                root = *std::min_element (pitches, pitches + n) % 12;
            for (int i = 0; i < n; ++i)
                out[i] = root < 0 ? 0.0 : prm_.sweeten * sweetCents (((pitches[i] - root) % 12 + 12) % 12) * 0.01;
        };
        double sr[kStrings], su[kStrings], sd[kStrings];
        sweetSet (rest, sr);
        sweetSet (up, su);
        sweetSet (down, sd);
        sweet_.fill (0.0);
        for (int i = 0; i < n; ++i)
        {
            const int s = strs[i];
            auto& st = sst_[(size_t) s];
            const int off = smOff_[(size_t) s];
            auto lim = [&] (int target) { const int o = off + target - rest[i]; return o < -4 || o > 6 ? off : o; };
            st.rest = off + sr[i];
            st.up = lim (up[i]) + (lim (up[i]) == off ? sr[i] : su[i]);
            st.down = lim (down[i]) + (lim (down[i]) == off ? sr[i] : sd[i]);
        }
        for (int s = 0; s < kStrings; ++s)
            str_[(size_t) s].setPedal (offsetFor (s), (jumpMask >> s) & 1u ? 0.0 : s == moved ? sec : 0.03);
    }
    // move a sounding string by d semitones: a pedal-style pull, or the bar when it is the only string
    bool moveString (int s, int d, double pullSec, double vel)
    {
        const int newOff = smOff_[(size_t) s] + d;
        const bool sole = ! othersRinging (s);
        const bool realPull = newOff >= -4 && newOff <= 6 && (reach_[(size_t) s] & (1u << (newOff + 4)));
        if (sole && (std::abs (d) > 2 || ! realPull) && bar_ + d >= 0 && bar_ + d <= kTopFret)
        {
            smPitch_[(size_t) s] += d;
            moveBarTo (bar_ + d, true, glideFor (std::abs (d), vel));
            analyse();
            return true;
        }
        if (newOff < -2 || newOff > 3 || std::abs (d) > 2)
            return false;
        smOff_[(size_t) s] = newOff;
        smPitch_[(size_t) s] += d;
        analyse (s, pullSec);
        return true;
    }
    // A chord still arriving that doesn't fit the strings its first notes took: take the whole grip
    // again, bar and strings together, and pick it afresh (a few ms after its first note).
    bool regrip (int note, int p, double vel)
    {
        int keys[kStrings], pitch[kStrings], n = 0;
        double vels[kStrings];
        for (int s = 0; s < kStrings; ++s)
            if (sst_[(size_t) s].owner >= 0)
            {
                keys[n] = sst_[(size_t) s].owner;
                pitch[n] = smPitch_[(size_t) s];
                vels[n++] = smVel_[(size_t) s];
            }
        if (n >= kStrings)
            return false;
        keys[n] = note;
        pitch[n] = p;
        vels[n++] = vel;
        int order[kStrings];
        for (int i = 0; i < n; ++i) order[i] = i;
        std::sort (order, order + n, [&] (int a, int b) { return pitch[a] > pitch[b]; });
        double bestCost = 1e9;
        int bestBar = -1, bestStr[kStrings] = {};
        for (int b = 0; b <= 24; ++b)
        {
            bool used[kStrings] = {};
            int strs[kStrings];
            double c = 0.03 * b;
            for (int i = 0; i < n && c < 1e5; ++i)
            {
                int sBest = -1;
                double cBest = 1e9;
                for (int s = 0; s < kStrings; ++s)
                {
                    const double cs = used[s] ? 1e9 : offCost (s, pitch[order[i]] - kE9Open[(size_t) s] - b, reach_[(size_t) s]) + stringPref (s);
                    if (cs < cBest) { cBest = cs; sBest = s; }
                }
                if (sBest < 0) { c = 1e9; break; }
                used[sBest] = true;
                strs[order[i]] = sBest;
                c += cBest;
            }
            // a grip the real pedals and levers can hold is worth a lot
            for (int k = 0; k < nCombos_ && c < 1e5; ++k)
            {
                bool match = true;
                for (int i = 0; i < n && match; ++i)
                    match = comboOff_[(size_t) k][(size_t) strs[i]] == pitch[i] - kE9Open[(size_t) strs[i]] - b;
                if (match)
                {
                    c -= 3.0 - 0.5 * comboCount_[(size_t) k];
                    break;
                }
            }
            if (c < bestCost)
            {
                bestCost = c;
                bestBar = b;
                std::copy (strs, strs + n, bestStr);
            }
        }
        if (bestBar < 0 || bestCost >= 1e5)
            return false;
        for (int s = 0; s < kStrings; ++s)
        {
            block (s);
            sst_[(size_t) s] = {};
            smOff_[(size_t) s] = 0;
            smGhost_[(size_t) s].n = 0;
        }
        bar_ = bestBar;
        for (auto& st : str_)
            st.setBar (barTarget(), 0.0, false);
        uint32_t mask = 0;
        for (int i = 0; i < n; ++i)
        {
            const int s = bestStr[i];
            sst_[(size_t) s].owner = keys[i];
            smPitch_[(size_t) s] = pitch[i];
            smOff_[(size_t) s] = pitch[i] - kE9Open[(size_t) s] - bar_;
            smVel_[(size_t) s] = vels[i];
            smOnset_[(size_t) s] = clock_;
            const int ch = noteCh_[(size_t) keys[i]];
            mpeOff_[(size_t) s] = ch > 0 ? chBend_[(size_t) ch] * prm_.mpeBendRange : 0.0;
            strPress_[(size_t) s] = ch > 0 ? chPress_[(size_t) ch] : 0.0;
            strY_[(size_t) s] = ch > 0 ? yGain (chY_[(size_t) ch]) : 1.0;
            mask |= 1u << s;
        }
        for (int s = 0; s < kStrings; ++s)
            str_[(size_t) s].setContact (sst_[(size_t) s].owner >= 0 ? 1.0 : kIdleContact);
        analyse (-1, 0.0, mask);
        for (int i = 0; i < n; ++i)
            pick (bestStr[i], vels[i]);
        return true;
    }
    void smartOn (int note, double vel)
    {
        int p = note;
        while (p < kSmartLo) p += 12;
        while (p > kSmartHi) p -= 12;
        held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
        held_.push_back (note);
        const int64_t window = (int64_t) (0.045 * fs_);
        const int ch = noteCh_[(size_t) note];

        // a string this key already has: pick it again
        for (int s = 0; s < kStrings; ++s)
            if (sst_[(size_t) s].owner == note)
            {
                smOnset_[(size_t) s] = clock_;
                pick (s, vel);
                return;
            }

        // a held note a step away (not part of the same chord): bend its string here.
        // A single line played legato: slide along its string.
        if (ch == 0 && (prm_.autoBend || prm_.legatoSlide))
        {
            int best = -1, bestD = 99;
            const int owned = ownedCount();
            for (int s = 0; s < kStrings; ++s)
            {
                const auto& st = sst_[(size_t) s];
                if (st.owner < 0 || clock_ - smOnset_[(size_t) s] < window || smGhost_[(size_t) s].n >= (int) smGhost_[(size_t) s].key.size())
                    continue;
                if (std::find (held_.begin(), held_.end(), st.owner) == held_.end())
                    continue; // ringing on the sustain pedal, not under a finger
                const int d = p - smPitch_[(size_t) s];
                const bool bend = prm_.autoBend && d != 0 && std::abs (d) <= 2;
                const bool slide = prm_.legatoSlide && owned == 1 && d != 0 && std::abs (d) <= 12;
                if ((bend || slide) && std::abs (d) < std::abs (bestD))
                {
                    best = s;
                    bestD = d;
                }
            }
            if (best >= 0)
            {
                auto& g = smGhost_[(size_t) best];
                const int oldKey = sst_[(size_t) best].owner, oldPitch = smPitch_[(size_t) best];
                const double pull = (0.20 - 0.12 * vel) / std::clamp (prm_.pedalSpeed, 0.2, 5.0);
                const bool line = owned == 1; // a single line, not a bend inside a chord
                const bool faded = clock_ - smOnset_[(size_t) best] > (int64_t) (0.45 * fs_);
                if (moveString (best, bestD, pull, vel))
                {
                    g.key[(size_t) g.n] = oldKey;
                    g.pitch[(size_t) g.n++] = oldPitch;
                    sst_[(size_t) best].owner = note;
                    // a line can't live on one pick: once the note has rung a while, the slide to the
                    // next one is picked too (lightly), the way players pick and slide
                    if (line && faded)
                    {
                        smOnset_[(size_t) best] = clock_;
                        pick (best, 0.65 * vel);
                    }
                    return;
                }
            }
        }

        // a new string
        bool active = false;
        for (int s = 0; s < kStrings; ++s)
            active |= sst_[(size_t) s].owner >= 0 || (sustainPedal_ && str_[(size_t) s].isRinging());
        double delay = 0.0;
        int string = -1;
        uint32_t jumped = 0;
        if (! active)
        {
            // nothing sounding: put the bar where this note sits best
            double bestCost = 1e9;
            int bestBar = bar_, bestOff = 0;
            for (int s = 0; s < kStrings; ++s)
                for (int off = -2; off <= 3; ++off)
                {
                    const int b = p - kE9Open[(size_t) s] - off;
                    if (b < 0 || b > 24)
                        continue;
                    const double c = offCost (s, off, reach_[(size_t) s]) + stringPref (s) + 0.04 * std::abs (b - bar_) + 0.02 * b;
                    if (c < bestCost)
                    {
                        bestCost = c;
                        string = s;
                        bestBar = b;
                        bestOff = off;
                    }
                }
            if (string < 0)
                return;
            if (bestBar != bar_)
            {
                for (int s = 0; s < kStrings; ++s)
                    block (s);
                moveBarTo (bestBar, false);
                delay = kLandSec;
            }
            smOff_[(size_t) string] = bestOff;
            groupStart_ = clock_;
        }
        else
        {
            const bool grouping = clock_ - groupStart_ < window && ! sustainPedal_;
            // the chord's first pick is still on the string (it hasn't let go yet): take the whole
            // grip together, as a player's hand does
            if (grouping && clock_ - groupStart_ < (int64_t) (0.0008 * fs_) && regrip (note, p, vel))
                return;
            double bestCost = 1e9;
            int bestBar = bar_;
            // notes arriving together choose the bar together; later ones fit the bar as it is
            for (int b = grouping ? 0 : bar_; b <= (grouping ? 24 : bar_); ++b)
            {
                double c = 0.03 * b; // (moving the bar within a chord's onset costs nothing: the pulls take it up silently)
                int offs[kStrings];
                bool used[kStrings] = {};
                for (int s = 0; s < kStrings && c < 1e5; ++s)
                    if (sst_[(size_t) s].owner >= 0)
                    {
                        used[s] = true;
                        offs[s] = smOff_[(size_t) s] - (b - bar_);
                        c += offCost (s, offs[s], reach_[(size_t) s]);
                    }
                if (c >= 1e5)
                    continue;
                int sBest = -1;
                double cBest = 1e9;
                for (int s = 0; s < kStrings; ++s)
                {
                    if (used[s])
                        continue;
                    const double cs = offCost (s, p - kE9Open[(size_t) s] - b, reach_[(size_t) s]) + stringPref (s);
                    if (cs < cBest)
                    {
                        cBest = cs;
                        sBest = s;
                    }
                }
                if (sBest < 0 || cBest >= 1e5)
                    continue;
                used[sBest] = true;
                offs[sBest] = p - kE9Open[(size_t) sBest] - b;
                c += cBest;
                // a grip the real pedals and levers can hold is worth a lot
                for (int k = 0; k < nCombos_; ++k)
                {
                    bool match = true;
                    for (int s = 0; s < kStrings && match; ++s)
                        match = ! used[s] || comboOff_[(size_t) k][(size_t) s] == offs[s];
                    if (match)
                    {
                        c -= 3.0 - 0.5 * comboCount_[(size_t) k];
                        break;
                    }
                }
                if (c < bestCost)
                {
                    bestCost = c;
                    bestBar = b;
                    string = sBest;
                }
            }
            if (string < 0 && grouping && regrip (note, p, vel))
                return;
            if (string < 0)
            {
                // no string free at this bar: fold an octave if that helps, else leave it out
                for (int alt : { p - 12, p + 12 })
                    for (int s = 0; s < kStrings && string < 0; ++s)
                        if (sst_[(size_t) s].owner < 0 && offCost (s, alt - kE9Open[(size_t) s] - bar_, reach_[(size_t) s]) < 1e5)
                        {
                            string = s;
                            p = alt;
                        }
                if (string < 0)
                    return;
            }
            jumped = revoice (bestBar);
            smOff_[(size_t) string] = p - kE9Open[(size_t) string] - bar_;
        }

        auto& st = sst_[(size_t) string];
        st.owner = note;
        st.inChord = false;
        smPitch_[(size_t) string] = p;
        smOnset_[(size_t) string] = clock_;
        smVel_[(size_t) string] = vel;
        smGhost_[(size_t) string].n = 0;
        mpeOff_[(size_t) string] = ch > 0 ? chBend_[(size_t) ch] * prm_.mpeBendRange : 0.0;
        strPress_[(size_t) string] = ch > 0 ? chPress_[(size_t) ch] : 0.0;
        strY_[(size_t) string] = ch > 0 ? yGain (chY_[(size_t) ch]) : 1.0;
        for (int s = 0; s < kStrings; ++s)
            str_[(size_t) s].setContact (sst_[(size_t) s].owner >= 0 ? 1.0 : kIdleContact);
        analyse (-1, 0.0, jumped | (1u << string)); // the new string's pull is in place before the pick
        // slide-in: a soft note starts below and slides up into pitch once it's picked
        if (slideIn (vel))
        {
            str_[(size_t) string].setPedal (offsetFor (string) - std::clamp (prm_.slideFrom, 0.25, 3.0), 0.0);
            strSlideAt_[(size_t) string] = clock_ + (int64_t) ((delay + 0.004) * fs_);
            slideInSec_ = std::clamp (prm_.slideTime, 0.02, 2.0);
        }
        cancelPicks (string);
        if (delay > 0)
            schedule (string, delay, vel);
        else
            pick (string, vel);
    }
    void smartOff (int note)
    {
        held_.erase (std::remove (held_.begin(), held_.end(), note), held_.end());
        for (int s = 0; s < kStrings; ++s)
        {
            auto& g = smGhost_[(size_t) s];
            // a key that was bent away from and is now let go: forget it
            for (int i = 0; i < g.n;)
                if (g.key[(size_t) i] == note)
                {
                    for (int j = i + 1; j < g.n; ++j)
                    {
                        g.key[(size_t) (j - 1)] = g.key[(size_t) j];
                        g.pitch[(size_t) (j - 1)] = g.pitch[(size_t) j];
                    }
                    --g.n;
                }
                else
                    ++i;
            if (sst_[(size_t) s].owner != note)
                continue;
            // the key bent or slid to is let go while the one before is still down: go back to it
            if (g.n > 0)
            {
                const int key = g.key[(size_t) (g.n - 1)], pitch = g.pitch[(size_t) (g.n - 1)];
                --g.n;
                const double back = 0.15 / std::clamp (prm_.pedalSpeed, 0.2, 5.0);
                if (moveString (s, pitch - smPitch_[(size_t) s], back, 0.6))
                {
                    sst_[(size_t) s].owner = key;
                    continue;
                }
                g.n = 0;
            }
            sst_[(size_t) s].owner = -1;
            if (! sustainPedal_)
                block (s);
        }
        if (held_.empty() && ! sustainPedal_) // all keys up: a palm block over everything
            for (int s = 0; s < kStrings; ++s)
                block (s);
        analyse();
    }

    // --- single-note sets --------------------------------------------------------------------
    void rebuildSingle()
    {
        if (! chordMode() && ! smartMode())
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
    // smart notes
    struct Ghosts { std::array<int, 6> key {}, pitch {}; int n = 0; }; // keys bent away from, still held
    std::array<int, kStrings> smOff_ {}, smPitch_ {};
    std::array<int64_t, kStrings> smOnset_ {};
    std::array<double, kStrings> smVel_ {}, sweet_ {};
    std::array<Ghosts, kStrings> smGhost_ {};
    std::array<std::array<int, kStrings>, 64> comboOff_ {};
    std::array<int, 64> comboCount_ {};
    std::array<uint32_t, kStrings> reach_ {};
    int nCombos_ = 0, smRoot_ = -1, smQual_ = 0;
    int64_t groupStart_ = -1000000;
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
