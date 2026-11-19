# Pump organ (sound demo)

A physically modelled suction reed organ: no samples. This is the sound engine and a demo renderer,
ahead of the plug-in. The volume pedal is the wind.

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

## The plug-in (first playable version)
VST3 and a standalone app ("Pump Organ"), with a plain auto-generated settings panel for now; the
real interface comes later. Every setting below is a parameter you can automate.
- **Instrument** (Chord organ / Parlour organ), **Wind**, **Pumping** (Easy / Reservoir /
  Expression), the six stops, **Vox Humana**, **Forte**, **Tuning age**, **Air noise**, **Level**.
- **Wind and the pedal:** the expression pedal (CC11; or CC7/4/2, or off) is the wind, scaled by the
  Wind knob. Until a pedal is moved, the organ sounds at the Wind knob's setting, so it plays with no
  pedal at all.
- **Sustain** (CC64) holds notes.
- **Advanced** (a group of its own in the settings panel):
  - **Tuning age** and **Air noise**.
  - **Low-wind cut-out:** how much notes flutter and cut out on little wind. At 0 they simply fade as
    the wind drops, steady. At 1 (as built) the chord organ flutters from about 60% of its wind down. At
    2 it starts sooner (about 80%) and drops in and out hard. At 55% pedal a held note wavers 0.3 dB, 3.5
    dB and 35 dB at 0, 1 and 2.
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
cmake --build build --target pumporgan_tests pumporgan_demo
ctest --test-dir build -R pumporgan
build/pumporgan/pumporgan_demo <folder>
```
`pumporgan_plugin_tests` checks the plug-in: parameters and defaults, sound with no pedal, the pedal as
wind, per-note bend and pressure through the plug-in's MIDI path, MPE off, state, and the editor.
The engine tests check tuning across C2-C7, the speech threshold and speech times, loudness and brightness
rising with wind, Melodia mellower than Diapason, the Celeste beating, the reservoir filling and
running dry, Expression responding only to strokes, the wind sagging under a big chord, the Celeste's
beat rate, the worn tuning spread, the pitch wander, and for the chord organ: the fan's spin-up, its
weaker wind, the slow sag and droop under a big chord, MPE (per-note bend on member channels, master
bends, RPN ranges and the MPE Configuration Message, a bend sent before the note-on, per-note pressure
vibrato, poly aftertouch with MPE off, slide choking a note, 15 notes on 15 channels), how it labours, warbles and sputters on little
wind (and holds steady on full), and its wear. Then stability and CPU.

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
