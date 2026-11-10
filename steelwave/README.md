# Steelwave

A physically modelled E9 pedal steel instrument for Windows (VST3 and a standalone app). No samples:
every sound is synthesised. The output is dry, DI-style mono for your own amp and pedals.

You play ordinary notes and chords, and the instrument finds them on a real 10-string E9 neck: one bar
shared by all strings, A/B/C pedals and four knee levers. Bends and slides come from how you move
your fingers. A second mode gives one key per chord or scale note.

**Install:** run `Steelwave-1.0.0-Windows-Setup.exe` (the `Steelwave-windows-installer` CI artifact). It
puts the VST3 in `Common Files\VST3` and the standalone app in `Program Files\Steelwave`. The installer
is not code-signed, so Windows SmartScreen will ask you to confirm.

## Smart notes (the default mode)
Play as you would a piano. The engine puts each note on the string that needs the least pulling at the
current bar position, preferring grips the real pedals and levers can hold.

- **Chords:** notes that arrive together choose the bar together. C-E-G lands on strings 8-6-5 at fret
  8, G7 on fret 3 with the lever that lowers string 2, and so on. Where the real instrument has no
  pull for a note, the string is pulled anyway (forgiving, not strict).
- **Pedal-style bends:** hold a note or chord and press a key a half or whole step from one of the held
  notes. That string bends to the new note with no new pick. Let the new key go while the first is
  still down and it bends back, like releasing a pedal. Hold C-E-G and press F and A for the classic
  I to IV move. Notes pressed together (within about 45 ms) are a chord, not a bend.
- **Legato slides:** a single line played legato slides along its string, moving the bar. Once a note
  has rung for a while, the slide to the next one is also picked lightly, so a line doesn't die away.
- **Pitch wheel**, two modes (Settings > Playing > Pitch wheel), both in smart notes and chord sets:
  - *Melody note* (default): only the top note of what you hold moves, a scale step up or down in the
    Key setting, and the rest holds. One voice bending against held ones is the classic steel sound.
  - *Chord move*: bends along the scale of the Key setting. With a chord held, up takes the third and
    fifth up a scale step (I to IV) and down takes the root down a step (I to iii). With no chord,
    every note moves a scale step. In chord sets it moves to the chord a 4th up or a 3rd up.
- **Range:** A2 to B6. Notes outside it fold in by octaves.
- **Sweeten:** tunes thirds and fifths pure against the chord's root (a major third is 13.7 cents
  flat), the way steel players tune. 60% by default; 0 is equal temperament.

Auto bend and Legato slide can each be switched off (Settings > Playing).

## Interface
- **Play page:** Mode, Key, eight knobs (Tone, Pick, Sustain, Vibrato, Glide, Swell, Sweeten, Level) and
  the neck, which shows the bar, the ringing strings and notes, the chord, the pedals and levers, the
  wheel and the volume pedal.
- **Settings:** three sub-pages. *Sound* has strum, bar noise, behind-bar, vibrato rate, sympathetic
  strings, pickup and volume pedal. *Playing* is described under "Controls page" below. *Model* is the
  23 calibration knobs under "Advanced page".
- **Presets** (header): 30 factory presets in four categories (Ambient 12, Americana 10, Classic 4,
  Utility 4), your own presets (Save as..., My presets, Delete), and < > to step through them. Factory
  presets also answer MIDI program change. A preset sets the sound and feel but leaves your rig alone:
  mode, key, controller setup and level. Your presets are files in `%APPDATA%\Steelwave\Presets`.
- **MIDI learn** (header): click a control, then move a knob, slider or pedal on your controller.

## Controls, all modes

| Input | Does |
|---|---|
| **B0** (MIDI 23) | Keyswitch: smart notes |
| **C1-G1** (MIDI 24-31) | Keyswitch: chord sets 1-8 |
| **G#1-B1** (32-35) | Keyswitch: single-note sets 1-4 |
| **MIDI 36-42** | Pedal keys (on by default; the lowest 7 keys of a 61-key board): A, B, C, LKL, LKR, RKL, RKR. Hold them like pedals: they travel like pedals (hit harder for a faster pedal, ~80-250 ms) and land on their stops |
| **MIDI 43 and up** | Play (layout below; the octave doesn't matter for chords). With pedal keys off, from MIDI 36 |
| **Pitch wheel** | Bend (below) |
| **Mod wheel** | Selectable: bar vibrato (default), the A+B pedals (a non-springing strip holds a pedal bend), a bar slide of up to 2 frets, or off |
| **Aftertouch** | Bar vibrato: press into the keys (channel or poly pressure) |
| **Expression, CC11** | Volume pedal (strongly tapered like a real one). Choosable: CC11/7/4/2/off |
| **Sustain, CC64** | Strings ring after you let go (otherwise letting go is a palm block) |
| **CC20-26** | The real E9 pedals and levers on top of anything: A, B, C, LKL, LKR, RKL, RKR |

## Chord sets (the second mode)
The layout is relative to the key, read like C major: white keys are I ii iii IV V vi vii°.
Black keys:
- C#: I in another voicing
- D#: II
- F#: iv
- G#: #V°
- A#: bVII

The pitch wheel bends to another diatonic chord:
- **Up:** the chord a 4th up (I→IV, ii→V, V→I ...).
- **Down:** the chord a 3rd up, by lowering the root (I→iii, IV→vi ...).

Playing a new chord while still holding the last one slides the bar there (legato, **Glide** sets the
time) without picking the strings again. Releasing back to a chord still held slides back.

| Set | Strings | Notes |
|---|---|---|
| 1 Classic | 3-6 | The standard 4-string grips |
| 2 Full | 3-6, 8, 10 | 5-6 strings, with the bass |
| 3 High | 1-5 | Including the chromatic strings 1 and 2 |
| 4 Low | 5-10 | |
| 5 Close | 3-5 | Triads |
| 6 Octave up | 3-6 | Bar 12 and up |
| 7 Wide | 3, 5, 6, 8, 10 | Spread voicings |
| 8 Pad | 1-10, 6-8 at once | Slow strum |

**The chords climb with the keyboard.** Each key's chord is voiced to sit a semitone higher than the key
below it (C# is the alternate I, voiced near I), so going up the keys goes up in pitch, the way block
chords do on a keyboard. A key change moves the whole set together, so that order holds in every key.
The octave you play in picks the register: the home octave is MIDI 60-71, an octave up moves the bar up
12 frets, and an octave down moves it down 12, where it fits. The bar can go to fret 26, two past the
neck's markings.

**How the voicings are found:** they're found on the neck (`core/Voicing.h`), not typed in. For each
chord the search looks for:
- one bar position, strings from the set's group and at most two pedals/levers held;
- exactly the chord's notes, root in the bass where possible;
- no unisons and no big gaps.

The wheel bends use a real pedal/lever combination when one reaches the target chord. That's 109 of
the 192 bends, e.g. I→IV is A+B, I→iii is LKR, vi→ii is B+C. Otherwise each string moves at most 2
semitones to the nearest chord note, like a custom changer would. For example, IV→V moves every string
up a whole step.

In the key of E, the Classic I is open E at the nut: wheel up gives A+B, wheel down gives LKR. Other
keys move the bar (kept between frets 0 and 22).

## Single-note sets
- **White keys:** the major scale of the key (C3 plays the root near E3). They bend with the wheel to
  the next or previous scale note.
- **Black keys:** the white key below them, held still and on another string where possible. Hold one
  under a bending white key for the classic "one note bends, one doesn't" move.
- All notes sit at one bar position, on whichever string (open, or with its pedal/lever) has them. This
  is how steel players play scales.

Sets: Scale, Scale high (bar +12), Whole-step bends, Half-step bends.

## Controls page (Settings > Playing)
How it's played:
- **Pedal keys** (on/off) and **Pedal speed**.
- **Mod wheel** function and **Aftertouch** (vibrato/off).
- **Auto swell:** notes softer than a set velocity fade in like a volume-pedal swell; harder notes pick as usual. **Swell time** sets how long it takes to open.
- **Auto vibrato** and **Vib delay:** vibrato that comes in on its own once a note has been held.

- **Slides:** **Slide-in** (notes softer than a set velocity, or every note, slide up into pitch after the pick), **Slide from** (how far below, in semitones) and **Slide time**. **Glide feel**: legato slides take longer for bigger jumps and less for harder playing. **Bar range**: how far the mod wheel's bar slide goes (1-12 frets).
- **MPE** (channels 2-16; channel 1 stays the master for the wheel, pedals and CCs). Each finger's pitch slide moves the bar (chords) or its own string (single notes), its pressure gives it its own vibrato, and its slide (CC74) fades it. **MPE bend range** sets 12, 24 or 48 semitones (48 is the MPE default).
- **Timing:** Natural, or Tight for live playing (picks let go sooner and chords strum tighter).

Vibrato depth is the strongest of the mod wheel, aftertouch and auto vibrato.

**MIDI learn** (top): turn it on, click any knob or menu, and move a knob, slider or pedal on your
controller. Each learned control shows its CC. A learned CC drives that control instead of its usual
job, and the assignments are saved with the session. **Clear all** forgets them. The page also shows the
pedals and levers live and summarises what each control does. The same pedal strip is on the Play page.

## Knobs (Play page and Settings > Sound)
| Knob | Range | |
|---|---|---|
| Glide | 30-1200 ms | Legato bar slide time |
| Strum | 0-60 ms | Pick spread across a chord, low string first |
| Picking hand | Thumb pick (plastic, softer, ~2 cm further from the bridge, pushing the string down and across) on strings 6-10; metal finger picks (pulling up and across) on 1-5. Each pick catches the string and drags it 0.8-2.5 ms before slipping off (longer for soft strokes and the thumb), with a faint scrape while it drags, more on wound strings. The release is a velocity pulse lasting the wave's round trip to the pick point, smoothed over the contact time (harder = brighter), plus a metal tick. No two picks are quite alike: position varies by +-6 mm, and stroke angle and contact time vary too. Re-picking a ringing string first catches it briefly |
| Brightness | 0-100 % | How long the high partials ring (old → new strings) |
| Sustain | 25-200 % | Decay times |
| Bar noise | 0-100 % | Contact noise of the bar sliding over the strings |
| Behind bar | 0-100 % | How freely the strings behind the bar ring (0 = muted by the hand) |
| Vib rate / depth | 3-8 Hz, 0-50 cents | Bar vibrato at full mod wheel |
| Sympathetic | 0-100 % | Unpicked strings ringing along through the bridge |
| Level | -24..+12 dB | |

Also: Set, Key, Pickup (single coil / humbucker), the Volume pedal CC, and Pedal type (passive 250k,
passive 500k, or active). The panel shows the neck, with
the bar, the strings ringing and their notes, the current chord, and what the wheel will do (which
pedals, or "voice-led").

## Advanced page (Settings > Model)
The **ADVANCED** tab exposes the physical model's calibration: 23 knobs, automatable and saved with the
session. Every knob's default (the tuned value) sits at 12 o'clock, so you can always hear which way
you've moved it. **Reset to calibrated** puts them all back. Hover a knob for what it does.

| Group | Knobs |
|---|---|
| String | Stiffness (inharmonicity), Plane detune (the slow beating), Exchange (how fast the two vibration planes trade energy: the bloom), Vert decay (the two-stage decay), Tension mod (hard-pick pitch rise), Growl |
| Body & bar | Body damp (energy the cabinet takes, and passes to the other strings), Body size (its resonances), Bar softness, Behind decay, Cabinet drop, Stop bounce |
| Pickup | Pickup pos (mm from the bridge), Field width, Horizontal (sensitivity to side-to-side motion), Pickup curve, Coil res (+-1 octave), Coil Q, Cable (pF, the passive volume pedal's tone loss) |
| Picking | Pick pos (mm from the bridge), Thumb (hardness re the finger picks), Drag, Variation |

The same values live in `core/` as defaults (`StringParams`, `EngineParams`); the table that drives the
parameters, the knobs and the tests is `Source/AdvancedParams.h`.

## The model
**Strings** (`core/SteelString.h`). Each string is two coupled digital waveguide loops, one per
vibration plane. Each loop has:
- a time-varying fractional delay (6-point Lagrange) recomputed every sample, so slides and bends are
  continuous;
- loss and stiffness filters (stiffness solved per string from physical inharmonicity);
- tuning compensation (including the body absorber), within 0.75 cents everywhere at moderate
  playing. The horizontal plane (the long tail) is on pitch; the vertical sits 1.1 cents sharp for
  the slow beating. Loud notes are slightly sharp, as on a real string.

Pitch is split the way the instrument does it:
- the **bar** shortens the string;
- **pedals and levers** change the tension;
- **vibrato** rocks the bar.

The pick and pickup are placed in metres from the bridge, so their comb filters follow the bar.

| Part | Model |
|---|---|
| Pick | Velocity pulse lasting the wave's round trip to the pick point, smoothed over the pick's contact time (harder = brighter), plus a metal tick at 2.5-4.5 kHz. A hard pick stretches the string, so the note starts up to ~7 cents sharp and settles in ~70 ms. Re-picking a ringing string first catches it briefly |
| Bar noise | From the bar's speed, on an S-curve (silent while the bar creeps into place). On wound strings, grainy ticks at the rate the wrap ridges pass under the bar. Band-limited to 400 Hz-3 kHz. As a magnetic pickup hears it: mostly through the played strings it sets moving, only a little directly. Played strings make it; strings merely under the bar make 20 %. About 48 dB below a medium pick on a wound string, and a chord slide's scrape about 48 dB below the picked chord (tested). A lifted and re-placed bar makes none |
| Tension modulation | A vibrating string stretches: its tension, and so its pitch, rises with the squared slope along it (for travelling waves, the squared wave summed over one period), computed every sample. A hard pick starts ~4-6 cents sharp and settles as the high partials die; soft picks barely move. Wound and low strings stay sharp a little longer. The tension fluctuation also pulls on the bridge lengthwise, adding faint extra partials (growl) that grow with the square of the playing strength |
| Body and bridge | Five cabinet/changer resonances (104-1180 Hz) plus a broadband part absorb energy from every string at the bridge. The vertical plane pushes on the top and loses ~4x more than the horizontal. Partials near a body resonance die faster, so each note decays unevenly, as on a real instrument. What the body absorbs drives the other strings (sympathetic resonance, strongest near the body modes). This is passive by construction: a string gets back at most 81% of what the strings gave the body, so the network always loses energy |
| Two planes | Each string's horizontal and vertical vibrations trade energy through the bridge: a slow, lossless rotation applied once per trip round the string, full exchange in ~2.4-3.6 s (a little different per string). With the faster-decaying vertical plane this gives the two-stage decay of real strings: a fast early drop (~28 dB/s) and then a long tail (~11 dB/s), with a mild bloom. Coupled planes push their frequencies apart slightly, so the long tail sits a fraction of a cent low |
| Hand | Palm/hand blocks ramp both decay times down |
| Behind the bar | The bar splits each string. The bar-to-nut part (1 - 2^(-fret/12) of the string) is short, high and usually out of key: at fret 3 on the E string it rings ~2 kHz over a G4. Moving the bar or vibrato moves it the opposite way to the note. It's a short, heavily damped second loop (T60 ~1 s). The pick's impulse passes through the bar and rings it, and the pickup hears it go by (about 30 dB below the note at Behind bar 100%). Where both halves share a pitch (fret 12) they also trade a little energy, losslessly: no drain, pitch within 0.3 cents |
| Bar | A heavy rod resting on the string, not a hard stop: high partials die a little faster the further up the neck (up to ~25% at the 24th fret) |
| Volume pedal | A passive pedal is a pot between the pickup and the cable. Part-way down, the resistance it leaves in the path (R t(1-t)) and ~700 pF of cable make a low-pass: open at both ends, darkest around half level (250k: ~3.6 kHz, about 7 dB less treble; 500k darker). Swells get brighter as they open, as with a real passive pedal. Active pedals have no loss |
| Pickup | Senses motion toward the pole (vertical) more than across it (0.8 : 1). It hears the wave going in and coming back from the bridge (a comb at its position), averaged over its ~18 mm field, which rounds off the top of the low strings where the waves are slow. The field strengthens as the string swings toward the pole (sensitivity (1 - y/d)^-2), adding a little second harmonic on hard picks (~21 dB below the note, ~36 dB on soft ones; pole pieces sit lower under the wound strings). Then the coil/cable resonance |
| Pedals and levers | Pull tension in proportion to their travel, and pitch follows the square root of tension: half way through a whole-step pull is ~1.05 semitones. Cabinet drop: strings pulled sharp load the cabinet, which gives, so the other strings sag flat (A+B: ~1.3-2 cents); lowering levers relieve it. Stops: a quick stab into a stop overshoots up to ~3 cents and settles in ~50 ms (the changer springing back); an eased-in move doesn't. Releasing onto the rest stop bounces half as much |

**Engine** (`core/SteelEngine.h`): 10 strings sharing one bar, sample-accurate MIDI, strums, the
volume pedal and vibrato. 10 strings use about 8% of one core at 48 kHz.

## Build, test, listen
```
cmake -B build                                   # fetches JUCE
cmake --build build --target Steelwave_VST3 Steelwave_Standalone steelwave_tests steelwave_engine_tests SteelwavePluginTests
ctest --test-dir build -R steelwave
./build/steelwave/steelwave_demo <folder>        # 01-05 the string model, 06-08 the whole instrument via MIDI
```
CI builds the VST3, the standalone app and the installer for Windows (link-time optimised on tags and manual runs only), and runs pluginval at
strictness 10. Bundles are in the `Steelwave-windows` artifact.

The tests cover:
- tuning, glides, decay, picking, bar noise, tension modulation, the two-stage decay, body
  resonances, behind the bar, the pickup, the picking hand and stability (`steelwave_tests`);
- every voicing in every key, the single-note maps, the wheel, legato, release/sustain, the volume
  pedal and its tone, CC pedals, pedal mechanics (travel, cabinet drop, stops), sympathetic coupling and a
  60 s random-MIDI stress run (`steelwave_engine_tests`);
- host-style tests: parameters, state, keyswitches, bit-identical output at any block size, sample
  rates, bypass, the editor and both its pages, and every Advanced knob at both ends of its range
  (finite, under 0 dBFS, and audibly changing the sound) (`SteelwavePluginTests`).
