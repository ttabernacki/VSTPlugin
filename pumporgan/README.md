# Pump Organ

A physically modelled suction reed organ, no samples: a little 1950s chord organ found in a basement, or a
full parlour reed organ. The volume pedal is the wind. VST3 and a standalone app for Windows.

**Install:** run `PumpOrgan-1.0.0-Windows-Setup.exe` (the `PumpOrgan-windows-installer` CI artifact). It
installs the VST3 to `Common Files\VST3` and the app to `Program Files\Pump Organ`. Unless the build was
code-signed, Windows SmartScreen asks you to confirm.

**First launch:** controller setup opens. Move your pitch slider, mod slider and expression pedal when it
asks, press into a key, and play a few notes soft to hard; skip what you don't have. The mod slider becomes
the swell lever and the pedal the wind, over the range each really reaches. It's saved for every Pump Organ
on the computer (`%APPDATA%\Pump Organ\Controller.xml`).

## The interface
An old reed organ's front: walnut, ivory, brass and Bakelite. The window scales as you resize it.
- **The stop rail:** ivory tablets on red felt, drawn out when on. Diapason 8', Melodia 8', Dulciana 8',
  Celeste 8', Sub Bass 16', Flute 4' (black ink); Vox Humana, Forte, Bass Coupler, Melody Coupler, Drone
  Hold (red ink).
- **The wind gauge:** the chest's suction against the most the wind can give, with a damped needle. The red
  arc is where the organ labours (it moves with Cut-out); under the needle it says whether the organ is
  singing, labouring, struggling or gasping.
- **The blower** (chord organ: the fan, spinning up with its motor) or **the bellows** (parlour organ,
  breathing with the wind), and a glass tube: the pedal (Easy), the air left (Reservoir), or the strokes
  (Expression).
- **Eight Bakelite knobs:** Wind, Swell, Chiff, Level; Pump (auto-pump depth), Cut-out, Warble, Neglect.
- **The swell shutters:** louvres turning open with the swell lever, the reeds' glow behind them.
- **Instrument, Microphone, Auto-pump** selectors, with a beat lamp breathing in time.
- **The keyboard, live:** each key glows with its reeds, amber, reddening and flickering as the wind
  struggles. Coupled notes are pressed by an unseen hand (sage), latched notes are pinned (brass), ciphers
  whisper (lilac). The HOLD lamp in the cheek block shows Drone hold.
- **Behind the panel** (a service card): Pumping and the wind pedal, controller setup; Pressure, vibrato and
  the Touch (velocity) curve; MPE and its ranges; Tuning age and Air noise.
- **Header:** the preset nameplate (click for the menu; its line says what the preset is), < >, the
  favourite star, A/B compare (A, B, copy across), Play it (an audition phrase), MIDI learn, CPU load.

## Playing aids
- **Swell lever** (the mod slider, CC1, by default): opens the swell shutters from the Swell knob's setting
  the rest of the way, louder (about 5 dB fully open) and brighter. Forte throws them wide.
- **Auto-pump:** the feet working on their own, in time with the host: once per beat, half note, bar or two
  bars, Pump knob deep. In Easy mode the wind swells and sags with each stroke (about 5 dB at full depth); with
  a reservoir or in Expression mode it makes the strokes itself, as hard as the pedal says.
- **Drone hold:** latches the notes you're holding. They keep sounding, and sagging the wind, while you play
  over them; turn it off and they stop. Key **B1** (just below the keyboard) toggles it, and a sostenuto
  pedal (CC66) does the same.
- **Couplers:** Bass adds your lowest note an octave down, Melody your highest an octave up, on the same
  stops. They follow as the lowest and highest notes change, and bend with the note they come from.
- **Microphone:** Close (in front of the reed board), Inside the case (boxy, woolly, a thick low-mid hump,
  the keys and fan right there), Across the room (softer, with a 1.2 s room behind it).
- **Neglect** (0 as built .. 1 a damp basement): more sticky keys, leakier wind (more sag), and **ciphers**:
  on release some keys' pallets don't quite close, and their reeds whisper on for half a second to a few
  seconds. At 0.5 about 1 key in 7 ciphers, at 1 about half.
- **Chiff:** a hard key snaps the pallet open with a puff of air (at full chiff its first 60 ms is about 4 dB
  louder than with none); a soft one eases in.

## Presets
17 factory presets in six groups. A preset sets the sound but leaves your rig alone: controllers, the
pumping mode, MPE, velocity curve, Drone hold and level.

| Group | Presets |
|---|---|
| Basement | Found in the Basement, Water Damage, Mothball Choir, Last Fuse |
| Parlour | Sunday Best, Grandmother's Hymnal, Lamp-lit Waltz |
| Dreams | Snow Globe, Slow Lighthouse, Kitchen Radio 1957, Night Ferry |
| Lullabies | Music for Sleep, Toy Shop |
| Pump | Breathing Room, Old Bellows Blues |
| Utility | Clean Parlour, Steady Chord Organ |

- **Your own presets:** Save as..., My presets, Delete, in the nameplate's menu. Stored in
  `%APPDATA%\Pump Organ\Presets`.
- **Favourites** come first in the menu. **A/B** flips between two sounds. **Program change** picks factory
  presets.
- **Play it:** C, Am, F, G, C with a melody on top, the wind swelling in on the pedal, the swell lever
  opening for the climax, and the wind easing away.
- **MIDI learn:** click a knob, stop tablet or selector, move a control on your keyboard. Saved with the
  session; Clear all forgets them.

## Two instruments
- **Chord organ** (default): a small 1950s-60s home organ in a plastic case, found in a basement.
  - **The fan:** a cheap electric blower pulls the wind. It spins up over a second or so when switched
    on, never runs quite steady, and hums and whooshes quietly.
  - **Weak wind:** its most is about a third of a parlour organ's, so the reeds swing about half as
    far: softer, darker, slower to speak, about 9 dB under the parlour organ at the same pedal. Leaky
    pallets and cracked leather waste some of it all the time.
  - **Saggy:** every note is more than the tired fan wants to give. The wind sags with each one,
    settling over about a third of a second as the fan labours. Everything droops flat and quieter: a
    held note goes about 16 cents flat and a fifth weaker when a big chord comes in, and turning the
    wind down flattens it too.
  - **Struggling on little wind:** below about 60% of its full wind it labours, more the less it gets.
    - Gusts: the fan's air comes unevenly, in gusts of a fixed size. At full wind they're nothing; at a
      trickle they're a big part of what's there.
    - Wow: a slow, uneven warble in everything's pitch, and each reed's own wander grows.
    - Sputter: each reed's grip on the wind flutters on its own, so notes in a chord swell and fade
      unevenly, and at a trickle they nearly drop out and come back.
    - For one held note: about 12 cents of drift at full wind, 17 at 75% pedal, 27 at 55% and 37 at
      40%. Its level holds within a dB or so down to 75% pedal, wavers about 4 dB at 55%, and from 40%
      down it drops in and out.
    - The parlour organ only labours below 30% of its wind, and less.
  - **Worn:** keys up to about 13 cents off, and each reed's pitch wanders about 1.6 cents. About 1 reed
    in 5 is weak and lazy and about 1 in 8 buzzy, and three keys stick, their notes hanging on a moment
    after you let go.
  - **The case:** a boxy plastic case, thin in the bass, dusty and dull on top. The fan is louder and
    wobblier than it should be.
- **Parlour organ:** a full-size foot-pumped reed organ in a wooden case: strong, steady wind.

## How it's modelled
- **Reeds.** Each reed is a brass tongue swinging through a slot (a damped mass-spring). Air from the
  room rushes through the slot into the low-pressure chest, and the tongue chops that flow into
  near-square pulses, shut each time it passes through the slot. The chopped flow is the sound.
  - A reed speaks only above a threshold suction, and speaks slower near it. Bass reeds are slowest:
    about 180 ms for C2 against 25 ms for C6 at moderate wind.
  - More suction swings the tongue harder: louder, and brighter as the chops get sharper.
  - A hard-driven tongue sags a few cents flat.
- **A worn-in instrument** (Tuning age; 1 = as found, 0 = factory fresh).
  - Each key sits a few cents off its mark (up to about 4 on the parlour organ, 13 on the chord organ), all its stops roughly together. Each reed
    sits a little off that (about 1 cent), so a key's stops beat gently against each other.
  - Every reed's pitch wanders slowly (about 0.5 cent). The beats between reeds drift irregularly
    instead of sweeping evenly like a phaser.
  - Each reed is voiced a little differently: some speak lazier, some buzzier or duller, some louder.
- **Air.** Turbulence in each reed's flow pulses with the reed, plus hiss, a low draught in the chest,
  and a slight flutter in the wind. Together they sit about 23 dB under the tone (Air noise scales it).
- **Wind.** All open reeds draw on one chest, so a big chord sags the wind for everyone.
  - **Easy** (default): pedal position is the suction. Heel is silent, toe is full.
  - **Reservoir**: every pedal stroke, either way, pumps air out of a spring-loaded reservoir that the
    reeds use up. It holds steady while there's air in it, and a safety valve stops it overfilling. Stop
    pumping and the sound sags and dies away.
  - **Expression**: the reservoir is bypassed and the strokes themselves are the wind, so every stroke
    is heard.
- **Stops.**
  - Diapason 8' (bright), Melodia 8' (mellow), Dulciana 8' (soft).
  - Celeste 8': Dulciana-voiced, treble only. It is tuned sharp by a slow beat (about 1.3 Hz in the
    bass to 2 Hz at the top), the way celestes are tuned, rather than by a fixed number of cents, which
    beats ever faster up the keyboard.
  - Sub Bass 16': bottom octave and a half.
  - Flute 4'.
  - Vox Humana: a fan tremolo.
  - Forte: opens the swell lid.
- **Case and mic.** Reed cells, the wooden case's resonances and the swell lid, heard by a close mic in
  front of the reed board, plus air hiss, pallet and key noise, and the bellows.

The reeds run at 4x the output rate, through a steep down-sampling filter, so their sharp pulses
don't fold back as haze. Ten notes on all six stops (52 reeds) use about 25% of one core.

## Playing it
| Control | Does |
|---|---|
| **Expression pedal (CC11)** | The wind, times the Wind knob (full until the pedal is moved; heel: no wind). Or CC7/4/2, or off |
| **Mod slider (CC1)** | The swell lever |
| **Aftertouch / MPE pressure** | Per-note vibrato (or that note's own air, or off) |
| **MPE slide (CC74)** | That note's own air |
| **Pitch bend** | MPE: each note on its own (48 semitones); the master channel / an ordinary wheel: 2 |
| **Sustain (CC64)** | Holds notes |
| **Sostenuto (CC66), key B1** | Drone hold |
| **Velocity** | The key action: chiff, and how fast the pallet opens |

- **Advanced** (a group of its own in a host's parameter list): Tuning age, Air noise, Low-wind cut-out and
  warble, Neglect.
  - **Low-wind cut-out:** how much notes flutter and cut out on little wind. At 0 they simply fade as the
    wind drops, steady. At 1 (as built) the chord organ flutters from about 60% of its wind down. At 2 it
    starts sooner (about 80%) and drops in and out hard. At 55% pedal a held note wavers 0.3 dB, 3.5 dB and
    35 dB at 0, 1 and 2.
  - **Low-wind warble:** how much the pitch warbles on little wind, from none to twice as built.

### MPE
On by default. Each note plays on its own MIDI channel, so each can be bent, pressed and slid on its
own.
- **Per-note pitch bend:** each note's channel bends that note alone, by **MPE note bend range** (48
  semitones by default, the MPE standard and Ableton Live's default; or 12, 24, 96). The master
  channel (1, or 16 for an upper zone) bends every note by the **Pitch wheel range** (2 by default).
- **Pressure** (MPE pressure, channel pressure or poly aftertouch): **per-note vibrato** by default.
  Each note gets its own vibrato, up to **Vibrato depth** (35 cents by default) at **Vibrato rate**,
  quickening a little the harder you press. Or **Swell**, where pressing opens that note's own air
  (louder, brighter), or **Off**.
- **Slide** (CC74): that note's own air, from all but choked (down) through normal (middle) to a
  little more (up). Or off.
- **Bend ranges by RPN:** a controller or host can set them with RPN 0; the MPE Configuration Message
  (RPN 6) sets the zones and resets the ranges to 48/2, as the spec says.
- **Ableton Live:** turn on MPE for the plug-in (right-click its title bar: Enable MPE Mode). Live
  sends exactly this: each note on its own channel, its bend, pressure and slide sent just before the
  note-on and then as they move, at 48 semitones. That holds whether it comes from an MPE controller,
  an MPE clip, or Live's MPE editing tools. A note starts wherever its bend already is, with no glide.
- **MPE off:** for an ordinary keyboard, any pitch bend bends everything by the pitch wheel range,
  channel pressure presses every note, and poly aftertouch presses one key. With MPE on, an ordinary
  keyboard on channel 1 still works the same way: channel 1 is the master.

## Build, test, listen
```
cmake --build build --target pumporgan_tests pumporgan_demo PumpOrganPluginTests PumpOrgan_VST3
ctest --test-dir build -R pumporgan
build/pumporgan/pumporgan_demo <folder>
```
The engine tests check tuning across C2-C7, the speech threshold and speech times, loudness and brightness
rising with wind, Melodia mellower than Diapason, the Celeste beating, the reservoir filling and running dry,
Expression responding only to strokes, the wind sagging under a big chord, the worn tuning spread, the pitch
wander, and for the chord organ: the fan's spin-up, its weaker wind, the sag and droop, how it labours,
warbles and sputters on little wind, and its wear. MPE: per-note bend on member channels, master bends, RPN
ranges and the MPE Configuration Message, a bend sent before the note-on, per-note pressure vibrato, poly
aftertouch with MPE off, slide choking a note, 15 notes on 15 channels. The playing aids: the swell lever
(louder, brighter; Forte the same as full), auto-pump breathing in time (and pumping by itself in Expression
mode), Drone hold and sostenuto, both couplers following the outer notes, the three mics (inside darker, the
room's tail, levels within 6 dB), ciphers growing with Neglect and dying away, and chiff. Then stability
and CPU.

The plug-in tests check the parameters and the Advanced group, sound with no pedal, the pedal as wind, MPE
through the plug-in's MIDI path, the swell lever, key B1 toggling Drone hold, auto-pump, presets (sound, not
rig), program change, user presets, A/B, favourites, the audition, MIDI learn, controller setup (the swell
lever and the wind pedal found and fitted, saved for every instance), state, and the editor (front, behind
the panel, controller setup, scaling).

`pumporgan_ui_snapshot <folder>` renders the editor to PNGs.

Demos:
1. Diapason C2-C6
2. A chord through a wind swell
3. A hymn in Easy mode
4. The stops
5. Reservoir: pumping, then the feet stop
6. Expression strokes
7. Ambient Celeste with Vox Humana
8. A big chord sagging the wind
9. The treble with the higher stops (Diapason, Dulciana, Celeste, Flute 4')
10. A hymn phrase worn in, then factory fresh
11. The chord organ switched on: its fan spins up under a held chord
12. The same phrase on the chord organ, then the parlour organ
13. Struggling on low wind: a chord held as the pedal eases to a trickle, up to full, and down again
14. A melody on a trickle of wind

Demos 1-11 are the chord organ. Except for demo 11, each starts with the fan already running.
