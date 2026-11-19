#pragma once

// Factory presets: starting points, chosen from the preset browser or by MIDI program change.
// A preset sets the sound (everything not listed goes back to its default) but leaves the rig alone:
// how your controllers are set up, the pumping mode, MPE, the velocity curve, and the level.

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace popresets
{
struct Preset
{
    const char* category;
    const char* name;
    const char* note; // one line, shown under the nameplate
    std::vector<std::pair<const char*, float>> values; // parameter ID, plain value
};

inline bool isRig (const juce::String& id)
{
    static const char* rig[] = { "pedalcc", "windmode", "mpe", "memberrange", "masterrange", "level", "velcurve", "hold" };
    for (auto* r : rig)
        if (id == r)
            return true;
    return false;
}

// stops: Diapason, Melodia, Dulciana, Celeste, Sub Bass, Flute (stop0..stop5); mic: 0 close, 1 inside the
// case, 2 across the room; pumprate: 0 off, 1 quarter, 2 half, 3 bar, 4 two bars; model: 0 chord, 1 parlour
inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        // --- Basement: the little chord organ, as found ------------------------------------------------
        { "Basement", "Found in the Basement", "Plugged in for the first time in thirty years.", {} },
        { "Basement", "Water Damage", "The bottom of the case sat in an inch of water one spring.",
          { { "neglect", 0.8f }, { "cutout", 1.5f }, { "warble", 1.6f }, { "age", 1.6f }, { "wind", 0.62f }, { "swell", 0.1f } } },
        { "Basement", "Mothball Choir", "Soft stops through the dust sheet, the fan tremolo stirring.",
          { { "stop0", 0 }, { "stop1", 1 }, { "stop2", 1 }, { "stop3", 1 }, { "vox", 1 }, { "neglect", 0.4f }, { "mic", 1 }, { "chiff", 0.15f } } },
        { "Basement", "Last Fuse", "One more chord before the motor gives out.",
          { { "wind", 0.45f }, { "cutout", 2.0f }, { "warble", 2.0f }, { "neglect", 1.0f }, { "air", 1.5f }, { "stop4", 1 } } },
        // --- Parlour: the big wooden reed organ --------------------------------------------------------
        { "Parlour", "Sunday Best", "Full organ in the front room, polished for company.",
          { { "model", 1 }, { "stop1", 1 }, { "stop4", 1 }, { "swell", 0.45f }, { "mic", 2 }, { "chiff", 0.35f }, { "bass", 1 } } },
        { "Parlour", "Grandmother's Hymnal", "Melodia and Dulciana, a little out, the pedals creaking.",
          { { "model", 1 }, { "stop0", 0 }, { "stop1", 1 }, { "stop2", 1 }, { "age", 1.5f }, { "air", 1.3f }, { "neglect", 0.2f },
            { "mic", 2 }, { "wind", 0.7f } } },
        { "Parlour", "Lamp-lit Waltz", "Dulciana and Celeste beating slowly, the Vox Humana turning.",
          { { "model", 1 }, { "stop0", 0 }, { "stop2", 1 }, { "stop3", 1 }, { "vox", 1 }, { "swell", 0.3f }, { "mic", 2 }, { "chiff", 0.1f } } },
        // --- Dreams: further away --------------------------------------------------------------------
        { "Dreams", "Snow Globe", "Small and high: soft stops with the melody an octave up.",
          { { "stop0", 0 }, { "stop2", 1 }, { "stop3", 1 }, { "stop5", 1 }, { "melody", 1 }, { "mic", 2 }, { "chiff", 0.1f }, { "swell", 0.2f },
            { "vox", 1 } } },
        { "Dreams", "Slow Lighthouse", "The feet working slowly, a breath every two bars.",
          { { "stop0", 0 }, { "stop1", 1 }, { "stop4", 1 }, { "pumprate", 4 }, { "pumpdepth", 0.75f }, { "mic", 2 }, { "bass", 1 } } },
        { "Dreams", "Kitchen Radio 1957", "Inside the case, mic against the grille cloth.",
          { { "mic", 1 }, { "air", 1.4f }, { "age", 1.3f }, { "swell", 0.0f }, { "stop2", 1 } } },
        { "Dreams", "Night Ferry", "A parlour organ across a dark room, rocking.",
          { { "model", 1 }, { "stop0", 0 }, { "stop1", 1 }, { "stop3", 1 }, { "stop4", 1 }, { "vox", 1 }, { "mic", 2 }, { "pumprate", 3 },
            { "pumpdepth", 0.4f }, { "bass", 1 } } },
        // --- Lullabies -----------------------------------------------------------------------------
        { "Lullabies", "Music for Sleep", "The softest stops, the lid shut, no chiff.",
          { { "stop0", 0 }, { "stop2", 1 }, { "stop3", 1 }, { "swell", 0.0f }, { "chiff", 0.0f }, { "mic", 2 }, { "wind", 0.62f }, { "vibdepth", 0.2f } } },
        { "Lullabies", "Toy Shop", "Bright and plinky: Flute 4' and a sharp key action.",
          { { "stop5", 1 }, { "melody", 1 }, { "chiff", 0.85f }, { "age", 1.5f }, { "swell", 0.6f } } },
        // --- Pump: in time -------------------------------------------------------------------------
        { "Pump", "Breathing Room", "The wind breathes with every beat.",
          { { "stop1", 1 }, { "pumprate", 1 }, { "pumpdepth", 0.6f }, { "swell", 0.3f } } },
        { "Pump", "Old Bellows Blues", "Parlour organ, pumped hard on the half note.",
          { { "model", 1 }, { "stop4", 1 }, { "pumprate", 2 }, { "pumpdepth", 0.8f }, { "chiff", 0.6f }, { "swell", 0.35f } } },
        // --- Utility -------------------------------------------------------------------------------
        { "Utility", "Clean Parlour", "Factory fresh: in tune, steady, quiet.",
          { { "model", 1 }, { "age", 0.0f }, { "air", 0.3f }, { "cutout", 0.0f }, { "warble", 0.0f }, { "chiff", 0.2f } } },
        { "Utility", "Steady Chord Organ", "The chord organ, without the struggle.",
          { { "cutout", 0.2f }, { "warble", 0.2f } } },
    };
    return p;
}
} // namespace popresets
