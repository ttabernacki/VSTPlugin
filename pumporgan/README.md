# Pump organ (sound demo)

A physically modelled suction reed organ: no samples. This is the sound engine and a demo renderer,
ahead of the plug-in. The volume pedal is the wind.

## Two instruments
- **Chord organ** (default): a small 1950s-60s home organ in a plastic case.
  - **The fan:** a cheap electric blower pulls the wind. It spins up over a second or so when switched
    on, never runs quite steady, and hums and whooshes quietly.
  - **Weak wind:** its most is under half a parlour organ's, so the reeds swing less: softer, darker,
    slower to speak.
  - **Saggy:** a big chord is more than the fan can keep up with. The wind sags (to about 70% under an
    8-note chord), settling over a moment as the fan labours, and everything droops flat and quieter: a
    held note goes about 10 cents flat when a big chord comes in.
  - **Struggling on little wind:** below about 60% of its full wind it labours, more the less it gets.
    - Gusts: the fan's air comes unevenly, in gusts of a fixed size. At full wind they're nothing; at a
      trickle they're a big part of what's there.
    - Wow: a slow, uneven warble in everything's pitch, and each reed's own wander grows.
    - Sputter: each reed's grip on the wind flutters on its own, so notes in a chord swell and fade
      unevenly, and at a trickle they nearly drop out and come back.
    - For one held note: about 6 cents of drift at full wind, 12 at 55% pedal, 18 at 40% and 27 at 30%.
      Its level holds within a dB down to about 55% pedal, then swings about 14 dB at 40% and drops
      almost out at 30%.
    - The parlour organ only labours below 30% of its wind, and less.
  - **Worn:** keys up to about 8 cents off. Some reeds are weak and lazy (about 1 in 8) and some buzzy
    (about 1 in 12).
  - **The case:** a boxy plastic case, thin in the bass, dull on top.
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
  - Each key sits a few cents off its mark (up to about 4 on the parlour organ, 8 on the chord organ), all its stops roughly together. Each reed
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

## Build, test, listen
```
cmake --build build --target pumporgan_tests pumporgan_demo
ctest --test-dir build -R pumporgan
build/pumporgan/pumporgan_demo <folder>
```
The tests check tuning across C2-C7, the speech threshold and speech times, loudness and brightness
rising with wind, Melodia mellower than Diapason, the Celeste beating, the reservoir filling and
running dry, Expression responding only to strokes, the wind sagging under a big chord, the Celeste's
beat rate, the worn tuning spread, the pitch wander, and for the chord organ: the fan's spin-up, its
weaker wind, the slow sag and droop under a big chord, how it labours, warbles and sputters on little
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
