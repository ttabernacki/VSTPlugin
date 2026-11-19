#pragma once

// Factory presets: starting points, chosen from the preset browser or by MIDI program change.
// A preset sets the sound and the bowing (everything not listed goes back to its default) but leaves the
// rig alone: how your controllers are set up, MPE, the voices, and the level.

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace scpresets
{
struct Preset
{
    const char* category;
    const char* name;
    std::vector<std::pair<const char*, float>> values; // parameter ID, plain value
};

inline bool isRig (const juce::String& id)
{
    static const char* rig[] = { "pedalcc", "mpe", "memberrange", "masterrange", "level", "mode", "velcurve" };
    for (auto* r : rig)
        if (id == r)
            return true;
    return false;
}

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        // --- Clean: a cello, played straight -----------------------------------------------------------
        { "Clean", "Concert", {} },
        { "Clean", "Warm Tasto", { { "position", 0.12f }, { "pressure", 0.42f }, { "brightness", 0.45f }, { "grit", 0.2f }, { "vibdepth", 0.22f } } },
        { "Clean", "Bright Solo", { { "position", 0.55f }, { "pressure", 0.48f }, { "brightness", 0.8f }, { "vibdepth", 0.24f }, { "vibrate", 5.9f },
                                    { "vibdelay", 0.3f } } },
        { "Clean", "Lament", { { "position", 0.3f }, { "pressure", 0.45f }, { "speed", 0.4f }, { "vibdepth", 0.3f }, { "vibrate", 5.2f },
                               { "vibdelay", 0.6f }, { "glide", 0.25f }, { "ring", 1.6f } } },
        // --- Texture: the bow's grain --------------------------------------------------------------------
        { "Texture", "Rosin Dust", { { "grit", 0.9f }, { "pressure", 0.5f }, { "pressurerange", 0.7f }, { "brightness", 0.6f } } },
        { "Texture", "Glass Ponticello", { { "position", 0.92f }, { "pressure", 0.45f }, { "brightness", 0.85f }, { "vibdepth", 0.08f }, { "grit", 0.3f } } },
        { "Texture", "Creaking Bow", { { "speed", 0.15f }, { "pressure", 0.55f }, { "grit", 0.7f }, { "vibdepth", 0.0f }, { "position", 0.45f } } },
        { "Texture", "Ghost Whisper", { { "pressure", 0.1f }, { "pressurerange", 0.5f }, { "position", 0.05f }, { "brightness", 0.35f }, { "grit", 0.5f },
                                        { "vibdepth", 0.05f } } },
        { "Texture", "Scratch Tone", { { "pressure", 0.62f }, { "pressurerange", 0.6f }, { "grit", 0.8f }, { "position", 0.6f }, { "vibdepth", 0.1f } } },
        // --- Grind: past the edge ------------------------------------------------------------------------
        { "Grind", "Grinder", { { "pressure", 0.58f }, { "pressurerange", 1.0f }, { "grit", 0.6f }, { "brightness", 0.55f } } },
        { "Grind", "Rust Belt", { { "pressure", 0.75f }, { "speed", 0.35f }, { "grit", 0.85f }, { "position", 0.4f }, { "body", 0.8f }, { "vibdepth", 0.0f } } },
        { "Grind", "Subharmonic Growl", { { "pressure", 0.85f }, { "speed", 0.25f }, { "position", 0.2f }, { "brightness", 0.4f }, { "grit", 0.5f },
                                          { "vibdepth", 0.0f } } },
        { "Grind", "Iron Bow", { { "pressure", 0.7f }, { "position", 0.85f }, { "brightness", 0.9f }, { "grit", 0.7f }, { "speed", 0.7f } } },
        // --- Ambient -------------------------------------------------------------------------------------
        { "Ambient", "Fog Drone", { { "pressure", 0.35f }, { "position", 0.15f }, { "brightness", 0.4f }, { "ring", 2.5f }, { "vibdepth", 0.06f },
                                    { "vibrate", 4.2f }, { "grit", 0.35f } } },
        { "Ambient", "Distant Cello", { { "pressure", 0.4f }, { "body", 0.6f }, { "brightness", 0.35f }, { "position", 0.25f }, { "vibdepth", 0.15f },
                                        { "ring", 2.0f } } },
        { "Ambient", "Slow Tides", { { "speed", 0.3f }, { "pressure", 0.42f }, { "pressurerange", 0.8f }, { "glide", 0.6f }, { "ring", 2.2f },
                                     { "vibdepth", 0.1f }, { "vibdelay", 1.2f } } },
        { "Ambient", "Bridge Shimmer", { { "position", 0.97f }, { "pressure", 0.3f }, { "brightness", 0.75f }, { "grit", 0.45f }, { "ring", 2.0f },
                                         { "vibdepth", 0.04f } } },
        // --- Utility -------------------------------------------------------------------------------------
        { "Utility", "Clean Only", { { "pressurerange", 0.25f } } },
        { "Utility", "No Vibrato", { { "vibdepth", 0.0f } } },
    };
    return p;
}
} // namespace scpresets
