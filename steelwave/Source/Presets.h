#pragma once

// Factory presets: starting points, switchable from the header or by MIDI program change.
// A preset sets every sound and feel parameter (everything not listed is put back to its default),
// but leaves the rig alone: the set and key you're in, how your controllers are set up, and the level.

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace swpresets
{
struct Preset
{
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
        { "Calibrated", {} },
        { "Nashville", { { "hardness", 0.85f }, { "brightness", 0.65f }, { "sustain", 1.1f }, { "barnoise", 0.4f }, { "behindbar", 0.4f },
                         { "vibdepth", 0.12f }, { "vibrate", 5.8f }, { "strum", 10.0f }, { "glide", 0.2f } } },
        { "Lanois ambient", { { "swellbelow", 0.75f }, { "swelltime", 1.2f }, { "hardness", 0.5f }, { "brightness", 0.4f }, { "sustain", 1.8f },
                              { "sympathetic", 0.9f }, { "behindbar", 0.7f }, { "autovib", 0.6f }, { "vibdelay", 0.9f }, { "vibdepth", 0.18f },
                              { "vibrate", 4.8f }, { "glide", 0.45f }, { "strum", 25.0f } } },
        { "Hawaiian", { { "glide", 0.35f }, { "vibdepth", 0.25f }, { "vibrate", 6.2f }, { "autovib", 0.5f }, { "vibdelay", 0.3f },
                        { "hardness", 0.6f }, { "brightness", 0.55f }, { "barnoise", 0.6f }, { "sustain", 0.9f }, { "slidein", 0.45f },
                        { "slidefrom", 2.0f }, { "slidetime", 0.2f } } },
        { "Tight & bright (live)", { { "timing", 1.0f }, { "hardness", 0.9f }, { "brightness", 0.75f }, { "strum", 6.0f }, { "glide", 0.15f },
                                     { "barnoise", 0.3f } } },
        { "Mellow humbucker", { { "pickup", 1.0f }, { "hardness", 0.45f }, { "brightness", 0.3f }, { "sustain", 1.3f }, { "adv_pickuppos", 70.0f } } },
    };
    return p;
}
} // namespace swpresets
