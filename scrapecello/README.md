# Scrape Cello

A physically modelled cello played for texture as much as tone, from a whispered hiss through a full
clean note to a grinding, crunching roar, all from how you bow it. No samples. VST3 and a standalone app,
with a plain auto-generated settings panel for now.

## How it's modelled
- **The bowed string:** two travelling waves, bow-to-bridge and bow-to-finger, meeting at the bow. There,
  each sample, the rosined hair and the string either stick (move together) or slip, decided by a friction
  law: static friction holds until it can't, then friction falls as the slip speeds up. That falling
  friction is what makes a bowed string sing: one stick and one slip per cycle, the Helmholtz motion.
- **Pressure decides the regime** (Schelleng's limits):
  - too little for the bow's position, and the slip comes twice a cycle: breathy, weak;
  - in between, a full clean note;
  - too much, and the stick won't let go on time: the motion turns raucous, grinding, crunching, dropping
    into subharmonics, and sagging flat.

  This comes from the physics, not an effect. The pressure control is calibrated against where each note
  really tips over (lower with the bow near the bridge, higher over the fingerboard), so it feels the same
  anywhere: half way is a clean tone, about two thirds the gritty edge (where it flickers between the two),
  full a grind.
- **Bow position:**
  - Near the bridge (ponticello), the string's motion keeps its sharp corner: glassy and bright. It also
    takes more pressure there to grind.
  - Over the fingerboard (sul tasto), the corner is rounded off: soft and flutey.
  - The level holds as you move.
- **Bow speed:** with the same weight on it, a slower bow creaks and grinds sooner. Each new note turns the
  bow; as it slows to turn it scratches for a moment.
- **Grit:** rosin and hair: grain in the friction while it slips, growing as it nears the edge.
- **Attacks:** a hard key bites (about 35 ms), a soft one swells in (about 150 ms or more).
- **The left hand:** vibrato that comes in on held notes, and glides: mono legato, or MPE per note.
- **Release:** when the bow lifts, the strings ring on a moment, then die away.
- **The body:** a cello's main resonances (the air, the wood, the bridge hill), heard by a close mic.

Four notes grinding at once use about 4% of one core.

## Playing it
| Control | Does |
|---|---|
| **Aftertouch / MPE pressure** | Bow pressure, from the Bow pressure knob up to a full grind (Pressure reach sets how far) |
| **Mod slider (CC1)** | Bow position, towards the bridge |
| **MPE slide (CC74)** | That note's own bow position |
| **Expression pedal (CC11)** | Bow speed, times the Bow speed knob (full until the pedal is moved; heel down, the bow stops) |
| **Pitch bend** | MPE: each note on its own (48 semitones, Ableton's default); the master channel / an ordinary wheel: 2 |
| **Sustain (CC64)** | Keeps bowing after the keys are let go |

- **Voices:** Poly gives four bows, one per note, for drones and chords. Mono gives one bow; legato notes
  glide under it, and letting go glides back to the note still held.
- **Ableton Live:** right-click the plug-in's title bar and choose **Enable MPE Mode**. Each note's
  pressure then grinds it on its own, its slide moves its own bow, and its bend glides it.

## Build, test, listen
```
cmake --build build --target scrapecello_tests scrapecello_demo ScrapeCelloPluginTests ScrapeCello_VST3
ctest --test-dir build -R scrapecello
build/scrapecello/scrapecello_demo <folder>
```
The core tests check:
- Tuning, C2 to C6.
- Clean at the default pressure, grinding at full, and the gritty edge in between.
- A feather-light bow sounding weakly.
- Ponticello brighter than sul tasto at the same level.
- A slow bow creaking.
- Hard and soft attacks, and the release ringing then dying.
- Mono glides and four poly bows.
- MPE per-note bend and pressure.
- Stability and CPU.

The plug-in tests check the parameters, playing, the pedal as bow speed, MPE through the plug-in, state
and the editor.

Demos:
1. A clean legato phrase
2. Pressure, from whisper to grind and back
3. Position, sul tasto to ponticello
4. A slow bow creaking
5. A three-note drone swelling into grinds
6. MPE: one note bends while the other grinds
7. Separate strokes: bite and swell
