#pragma once

// The Advanced page: the physical model's calibration, one table used by the processor (parameters),
// the editor (knobs) and the tests. Defaults are the tuned values; every knob's default sits at 12
// o'clock, so you can hear which way you've moved it.

#include "../core/SteelEngine.h"

#include <juce_core/juce_core.h>

namespace swadv
{
enum Group { String, BodyBar, Pickup, Picking, kGroups };
inline const char* groupName (int g)
{
    static const char* n[] = { "STRING", "BODY & BAR", "PICKUP", "PICKING" };
    return n[g];
}

struct Param
{
    const char* id;
    const char* name;  // full name (host, tooltip)
    const char* label; // knob caption
    Group group;
    float min, max, def;
    const char* unit;  // "x", "ct", "mm", "s", "pF", "oct", "" (plain)
    double sw::EngineParams::*field;
    const char* help;
};

inline const std::vector<Param>& params()
{
    using E = sw::EngineParams;
    static const std::vector<Param> p = {
        { "adv_stiffness", "Stiffness", "STIFFNESS", String, 0.0f, 3.0f, 1.0f, "x", &E::stiffness,
          "Inharmonicity: how sharp the upper partials run (steel strings are stiff). More = glassier, bell-like." },
        { "adv_detune", "Plane detune", "PLANE DETUNE", String, 0.0f, 5.0f, 1.1f, "ct", &E::planeDetuneCents,
          "Tuning difference between the vertical and horizontal vibration: the slow beating in the tail." },
        { "adv_exchange", "Plane exchange", "EXCHANGE", String, 0.0f, 4.0f, 1.0f, "x", &E::planeExchange,
          "How fast the two vibration planes trade energy through the bridge: the bloom in the sustain." },
        { "adv_vdecay", "Vertical decay", "VERT DECAY", String, 0.1f, 1.0f, 0.55f, "x", &E::verticalDecay,
          "Decay time of the vertical plane re the horizontal. Lower = a faster first drop (two-stage decay)." },
        { "adv_tension", "Tension modulation", "TENSION MOD", String, 0.0f, 3.0f, 1.0f, "x", &E::tensionMod,
          "How far a hard pick stretches the string sharp before it settles." },
        { "adv_growl", "Growl", "GROWL", String, 0.0f, 5.0f, 1.0f, "x", &E::growl,
          "Extra partials from the tension pulling on the bridge lengthwise, on hard picks." },

        { "adv_bodydamp", "Body damping", "BODY DAMP", BodyBar, 0.0f, 3.0f, 1.0f, "x", &E::bodyDamping,
          "Energy the cabinet takes from the strings at the bridge (and passes to the other strings)." },
        { "adv_bodysize", "Body size", "BODY SIZE", BodyBar, 0.5f, 2.0f, 1.0f, "x", &E::bodySize,
          "Scales the cabinet: bigger = its resonances lower (104-1180 Hz at 1x)." },
        { "adv_barsoft", "Bar softness", "BAR SOFTNESS", BodyBar, 0.0f, 0.8f, 0.35f, "", &E::barSoftness,
          "How much the bar (not a hard stop) darkens notes as it moves up the neck." },
        { "adv_behinddecay", "Behind-bar decay", "BEHIND DECAY", BodyBar, 0.1f, 4.0f, 1.0f, "s", &E::behindDecay,
          "How long the string behind the bar rings (its level is the Behind bar knob)." },
        { "adv_cabdrop", "Cabinet drop", "CABINET DROP", BodyBar, 0.0f, 4.0f, 1.0f, "x", &E::cabinetDrop,
          "How much the other strings sag flat when pedals pull strings sharp (A+B ~ 1.3-2 cents at 1x)." },
        { "adv_bounce", "Stop bounce", "STOP BOUNCE", BodyBar, 0.0f, 4.0f, 1.0f, "x", &E::stopBounce,
          "Pitch overshoot when a pedal or the wheel is stabbed into its stop (~3 cents at 1x)." },

        { "adv_pickuppos", "Pickup position", "PICKUP POS", Pickup, 20.0f, 120.0f, 45.0f, "mm", &E::pickupPosMm,
          "Pickup's distance from the bridge. Closer = thinner and brighter, further = rounder." },
        { "adv_aperture", "Pickup field width", "FIELD WIDTH", Pickup, 0.0f, 40.0f, 18.0f, "mm", &E::apertureMm,
          "Length of string the pickup hears at once. Wider rounds off the top, most on the low strings." },
        { "adv_horiz", "Horizontal sensitivity", "HORIZONTAL", Pickup, 0.0f, 1.0f, 0.8f, "", &E::pickupHorizontal,
          "Pickup's sensitivity to side-to-side motion (up-and-down = 1). Changes the decay shape." },
        { "adv_curve", "Pickup curve", "PICKUP CURVE", Pickup, 0.0f, 3.0f, 1.0f, "x", &E::pickupCurve,
          "Field getting stronger as the string nears the pole: 2nd harmonic and push on hard picks." },
        { "adv_coil", "Coil resonance", "COIL RES", Pickup, -1.0f, 1.0f, 0.0f, "oct", &E::coilShiftOct,
          "Moves the coil/cable resonance (single coil 6.5 kHz, humbucker 3.6 kHz) up or down." },
        { "adv_coilq", "Coil Q", "COIL Q", Pickup, 0.3f, 3.0f, 1.0f, "x", &E::coilQ,
          "Height of the coil resonance peak: more = a more pronounced presence bump." },
        { "adv_cable", "Cable capacitance", "CABLE", Pickup, 100.0f, 2000.0f, 700.0f, "pF", &E::cablePf,
          "Cable after a passive volume pedal: more = more treble loss part-way down." },

        { "adv_pickpos", "Pick position", "PICK POS", Picking, 50.0f, 200.0f, 100.0f, "mm", &E::pickPosMm,
          "Where the fingers pick, from the bridge (the thumb ~2 cm further). Closer = brighter, thinner." },
        { "adv_thumb", "Thumb hardness", "THUMB", Picking, 0.2f, 1.0f, 0.65f, "", &E::thumbHardness,
          "Thumb pick (strings 6-10) hardness re the metal finger picks." },
        { "adv_drag", "Pick drag", "DRAG", Picking, 0.0f, 3.0f, 1.0f, "x", &E::dragTime,
          "How long a pick drags the string before letting go (0.8-2.5 ms at 1x)." },
        { "adv_variation", "Pick variation", "VARIATION", Picking, 0.0f, 2.0f, 1.0f, "x", &E::pickVariation,
          "How much each pick differs from the last (where it lands, angle, contact)." },
    };
    return p;
}

inline juce::String format (const Param& p, float v)
{
    const juce::String u (p.unit);
    if (u == "x") return juce::String (v, 2) + "x";
    if (u == "ct") return juce::String (v, 1) + " ct";
    if (u == "mm") return juce::String (juce::roundToInt (v)) + " mm";
    if (u == "s") return juce::String (v, 2) + " s";
    if (u == "pF") return juce::String (juce::roundToInt (v)) + " pF";
    if (u == "oct") return (v >= 0 ? "+" : "") + juce::String (v, 2) + " oct";
    return juce::String (v, 2);
}
} // namespace swadv
