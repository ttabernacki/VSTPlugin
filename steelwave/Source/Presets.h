#pragma once

// Factory presets: starting points, chosen from the preset browser or by MIDI program change.
// A preset sets every sound and feel parameter (everything not listed is put back to its default),
// but leaves the rig alone: the mode and key you're in, how your controllers are set up, and the level.

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace swpresets
{
struct Preset
{
    const char* category;
    const char* name;
    std::vector<std::pair<const char*, float>> values; // parameter ID, plain value
};

// parameters a preset never touches
inline bool isRig (const juce::String& id)
{
    static const char* rig[] = { "set", "key", "volcc", "voltype", "pedalkeys", "modwheel", "aftertouch", "mpe", "bendrange", "level" };
    for (auto* r : rig)
        if (id == r)
            return true;
    return false;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        // --- Ambient / cinematic ---------------------------------------------------------------
        { "Ambient", "Slow Dawn", { { "swellbelow", 1.0f }, { "swelltime", 1.4f }, { "hardness", 0.5f }, { "brightness", 0.4f }, { "sustain", 1.8f },
                                    { "sympathetic", 0.9f }, { "behindbar", 0.7f }, { "autovib", 0.6f }, { "vibdelay", 0.9f }, { "vibdepth", 0.18f },
                                    { "vibrate", 4.8f }, { "glide", 0.45f } } },
        { "Ambient", "Glass Horizon", { { "swellbelow", 0.8f }, { "swelltime", 0.9f }, { "hardness", 0.8f }, { "brightness", 0.8f }, { "sustain", 1.9f },
                                        { "sympathetic", 1.0f }, { "adv_pickuppos", 32.0f }, { "adv_stiffness", 1.4f }, { "autovib", 0.3f }, { "vibdelay", 1.2f } } },
        { "Ambient", "Cathedral Swells", { { "swellbelow", 1.0f }, { "swelltime", 2.2f }, { "sustain", 2.0f }, { "brightness", 0.55f }, { "sympathetic", 1.0f },
                                           { "behindbar", 0.9f }, { "adv_behinddecay", 2.5f }, { "autovib", 0.4f }, { "vibdelay", 1.5f }, { "vibrate", 4.4f } } },
        { "Ambient", "Low Tide", { { "swellbelow", 0.9f }, { "swelltime", 1.6f }, { "pickup", 1.0f }, { "brightness", 0.25f }, { "hardness", 0.4f },
                                   { "sustain", 1.7f }, { "adv_pickuppos", 80.0f }, { "sympathetic", 0.8f }, { "glide", 0.6f } } },
        { "Ambient", "Aurora", { { "swellbelow", 0.7f }, { "swelltime", 1.0f }, { "brightness", 0.7f }, { "sustain", 1.6f }, { "adv_exchange", 2.0f },
                                 { "adv_detune", 2.2f }, { "sympathetic", 0.9f }, { "autovib", 0.5f }, { "vibdepth", 0.1f }, { "vibrate", 5.0f } } },
        { "Ambient", "Drift (slide-in)", { { "slidein", 1.0f }, { "slidefrom", 2.0f }, { "slidetime", 0.4f }, { "swellbelow", 0.6f }, { "swelltime", 0.8f },
                                           { "sustain", 1.6f }, { "brightness", 0.45f }, { "glide", 0.7f }, { "barnoise", 0.3f } } },
        { "Ambient", "Frozen Lake", { { "swellbelow", 1.0f }, { "swelltime", 3.0f }, { "sustain", 2.0f }, { "brightness", 0.6f }, { "hardness", 0.3f },
                                      { "sympathetic", 1.0f }, { "vibdepth", 0.0f }, { "adv_bodydamp", 0.6f }, { "sweeten", 1.0f } } },
        { "Ambient", "Shimmer Bed", { { "swellbelow", 0.85f }, { "swelltime", 1.2f }, { "brightness", 0.9f }, { "adv_coil", 0.4f }, { "adv_coilq", 1.6f },
                                      { "sustain", 1.8f }, { "behindbar", 1.0f }, { "adv_behinddecay", 3.0f }, { "sympathetic", 1.0f } } },
        { "Ambient", "Night Drive", { { "swellbelow", 0.5f }, { "swelltime", 0.7f }, { "pickup", 1.0f }, { "brightness", 0.5f }, { "sustain", 1.5f },
                                      { "autovib", 0.7f }, { "vibdelay", 0.6f }, { "vibdepth", 0.2f }, { "vibrate", 5.2f }, { "glide", 0.5f } } },
        { "Ambient", "Weightless", { { "swellbelow", 1.0f }, { "swelltime", 1.8f }, { "hardness", 0.2f }, { "brightness", 0.35f }, { "sustain", 2.0f },
                                     { "barnoise", 0.0f }, { "behindbar", 0.0f }, { "sympathetic", 0.6f }, { "sweeten", 1.0f }, { "glide", 0.9f } } },
        { "Ambient", "Tape Memory", { { "swellbelow", 0.6f }, { "swelltime", 0.9f }, { "brightness", 0.3f }, { "adv_coil", -0.5f }, { "adv_detune", 3.0f },
                                      { "sustain", 1.4f }, { "autovib", 0.8f }, { "vibdepth", 0.14f }, { "vibrate", 4.2f }, { "vibdelay", 0.3f } } },
        { "Ambient", "Wide Open Sky", { { "swellbelow", 0.75f }, { "swelltime", 1.3f }, { "brightness", 0.65f }, { "sustain", 1.9f }, { "sympathetic", 1.0f },
                                        { "adv_bodysize", 1.4f }, { "adv_vdecay", 0.75f }, { "autovib", 0.35f }, { "vibdelay", 1.0f } } },

        // --- Americana / indie -----------------------------------------------------------------
        { "Americana", "Front Porch", { { "hardness", 0.7f }, { "brightness", 0.5f }, { "sustain", 1.1f }, { "barnoise", 0.6f }, { "behindbar", 0.6f },
                                        { "vibdepth", 0.15f }, { "vibrate", 5.4f }, { "autovib", 0.4f }, { "vibdelay", 0.5f }, { "glide", 0.28f } } },
        { "Americana", "Band In A Room", { { "hardness", 0.75f }, { "brightness", 0.45f }, { "sustain", 1.0f }, { "barnoise", 0.7f }, { "adv_growl", 2.0f },
                                           { "adv_tension", 1.4f }, { "adv_variation", 1.5f }, { "sympathetic", 0.7f }, { "autovib", 0.3f } } },
        { "Americana", "Dusty Road", { { "pickup", 1.0f }, { "hardness", 0.65f }, { "brightness", 0.35f }, { "sustain", 0.9f }, { "barnoise", 0.8f },
                                       { "adv_coil", -0.3f }, { "adv_pickuppos", 60.0f }, { "vibdepth", 0.18f }, { "autovib", 0.5f }, { "vibdelay", 0.4f } } },
        { "Americana", "Songwriter Pad", { { "swellbelow", 0.45f }, { "swelltime", 0.5f }, { "hardness", 0.6f }, { "brightness", 0.5f }, { "sustain", 1.3f },
                                           { "autovib", 0.45f }, { "vibdelay", 0.7f }, { "glide", 0.35f }, { "sweeten", 0.8f } } },
        { "Americana", "Crying Steel", { { "hardness", 0.8f }, { "brightness", 0.6f }, { "sustain", 1.2f }, { "vibdepth", 0.22f }, { "vibrate", 5.8f },
                                         { "autovib", 0.7f }, { "vibdelay", 0.35f }, { "slidein", 0.4f }, { "slidefrom", 1.0f }, { "slidetime", 0.18f } } },
        { "Americana", "Laurel Canyon", { { "hardness", 0.6f }, { "brightness", 0.55f }, { "sustain", 1.2f }, { "adv_pickuppos", 55.0f }, { "barnoise", 0.5f },
                                          { "autovib", 0.35f }, { "vibdepth", 0.12f }, { "glide", 0.32f }, { "sympathetic", 0.6f } } },
        { "Americana", "Heartland", { { "hardness", 0.85f }, { "brightness", 0.6f }, { "sustain", 1.0f }, { "adv_pickpos", 80.0f }, { "barnoise", 0.6f },
                                      { "vibdepth", 0.16f }, { "autovib", 0.4f }, { "glide", 0.22f } } },
        { "Americana", "Worn Strings", { { "brightness", 0.2f }, { "hardness", 0.7f }, { "sustain", 0.8f }, { "adv_stiffness", 0.7f }, { "adv_bodydamp", 1.5f },
                                         { "barnoise", 0.9f }, { "vibdepth", 0.17f }, { "autovib", 0.5f } } },
        { "Americana", "Indie Wash", { { "swellbelow", 0.55f }, { "swelltime", 0.6f }, { "brightness", 0.6f }, { "sustain", 1.4f }, { "adv_growl", 2.5f },
                                       { "behindbar", 0.9f }, { "sympathetic", 0.9f }, { "autovib", 0.3f }, { "glide", 0.4f } } },
        { "Americana", "Honey & Rust", { { "pickup", 1.0f }, { "hardness", 0.55f }, { "brightness", 0.45f }, { "sustain", 1.3f }, { "adv_coilq", 1.5f },
                                         { "vibdepth", 0.2f }, { "vibrate", 5.0f }, { "autovib", 0.6f }, { "vibdelay", 0.5f }, { "sweeten", 0.9f } } },

        // --- Classic ---------------------------------------------------------------------------
        { "Classic", "Nashville Clean", { { "hardness", 0.85f }, { "brightness", 0.65f }, { "sustain", 1.1f }, { "barnoise", 0.4f }, { "behindbar", 0.4f },
                                          { "vibdepth", 0.12f }, { "vibrate", 5.8f }, { "strum", 10.0f }, { "glide", 0.2f } } },
        { "Classic", "Bakersfield Bite", { { "hardness", 1.0f }, { "brightness", 0.85f }, { "sustain", 0.9f }, { "adv_pickuppos", 30.0f }, { "adv_pickpos", 70.0f },
                                           { "glide", 0.15f }, { "vibdepth", 0.1f } } },
        { "Classic", "Hawaiian", { { "glide", 0.35f }, { "vibdepth", 0.25f }, { "vibrate", 6.2f }, { "autovib", 0.5f }, { "vibdelay", 0.3f },
                                   { "hardness", 0.6f }, { "brightness", 0.55f }, { "barnoise", 0.6f }, { "sustain", 0.9f }, { "slidein", 0.45f },
                                   { "slidefrom", 2.0f }, { "slidetime", 0.2f } } },
        { "Classic", "Mellow Humbucker", { { "pickup", 1.0f }, { "hardness", 0.45f }, { "brightness", 0.3f }, { "sustain", 1.3f }, { "adv_pickuppos", 70.0f } } },

        // --- Utility ---------------------------------------------------------------------------
        { "Utility", "Calibrated", {} },
        { "Utility", "Tight & Bright (live)", { { "timing", 1.0f }, { "hardness", 0.9f }, { "brightness", 0.75f }, { "strum", 6.0f }, { "glide", 0.15f },
                                                { "barnoise", 0.3f } } },
        { "Utility", "Equal Tempered", { { "sweeten", 0.0f } } },
        { "Utility", "Every Key Picks", { { "autobend", 0.0f }, { "legatoslide", 0.0f } } },
    };
    return p;
}
} // namespace swpresets
