# Scrape Cello

A physically modelled cello played for texture as much as tone, from a whispered hiss through a full
clean note to a grinding, crunching roar, all from how you bow it. No samples. VST3 and a standalone app
for Windows.

**Install:** run `ScrapeCello-1.0.0-Windows-Setup.exe` (the `ScrapeCello-windows-installer` CI artifact).
It installs the VST3 to `Common Files\VST3` and the app to `Program Files\Scrape Cello`. Unless the build
was code-signed, Windows SmartScreen asks you to confirm.

**First launch:** controller setup opens. Move your pitch slider, mod slider and expression pedal when it
asks, press into a key, and play a few notes soft to hard; skip what you don't have. It fits the cello to
the range each control really reaches, and saves that for every Scrape Cello on the computer
(`%APPDATA%\Scrape Cello\Controller.xml`).

## The interface
The window scales as you resize it.
- **Play page:**
  - **The bows, live:** each sounding note is a string, vibrating with its level. Its bow sits where it
    crosses the string (tasto to ponticello), with an arrow for its direction. Its pressure gauge shows
    where it is against its own edge, coloured light, clean, edge or grind; the line marks the edge.
  - **Ten knobs:** Pressure, Reach, Position, Speed, Grit, Brightness, Body, Vibrato, Glide, Level.
- **Settings:**
  - Left hand: vibrato rate and delay, how long the strings ring.
  - Playing: Voices (poly or mono), the velocity curve, the bow-speed pedal's CC.
  - MPE: on/off and its bend ranges.
  - Controller setup, and a summary of what every control does.
- **Header:** presets (< name >), the favourite star, A/B compare (A, B, copy across), PLAY IT (an audition
  phrase), MIDI LEARN, and CPU load.

## Presets
19 factory presets in five groups. A preset sets the sound and the bowing, but leaves your rig alone:
controllers, MPE, voices, velocity curve and level.

| Group | Presets |
|---|---|
| Clean | Concert, Warm Tasto, Bright Solo, Lament |
| Texture | Rosin Dust, Glass Ponticello, Creaking Bow, Ghost Whisper, Scratch Tone |
| Grind | Grinder, Rust Belt, Subharmonic Growl, Iron Bow |
| Ambient | Fog Drone, Distant Cello, Slow Tides, Bridge Shimmer |
| Utility | Clean Only, No Vibrato |

- **Your own presets:** Save as..., My presets, Delete. They're stored as files in
  `%APPDATA%\Scrape Cello\Presets`.
- **Favourites:** come first in the preset menu.
- **A/B:** flips between two sounds.
- **PLAY IT:** plays a D-A drone pressed into a grind and back, then a short melody leaning on the edge.
- **Program change:** factory presets also answer MIDI program change.
- **MIDI learn:** click a control, move a knob or slider on your keyboard. Assignments are saved with the
  session; Clear all forgets them.

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

The plug-in tests check:
- The parameters and defaults, playing, the pedal as bow speed, and MPE through the plug-in.
- Presets: they set the sound but not the rig; program change; user presets save, load and delete;
  A/B; favourites; the audition phrase.
- MIDI learn, controller setup (finding the right slider and its range, saved for every instance), and
  state.
- The editor: both pages, controller setup, and scaling.

`scrapecello_ui_snapshot <folder>` renders the editor to PNGs.

Demos:
1. A clean legato phrase
2. Pressure, from whisper to grind and back
3. Position, sul tasto to ponticello
4. A slow bow creaking
5. A three-note drone swelling into grinds
6. MPE: one note bends while the other grinds
7. Separate strokes: bite and swell
