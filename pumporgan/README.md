# Pump organ (sound demo)

A physically modelled American suction reed organ: no samples. This is the sound engine and a demo
renderer, ahead of the plug-in. You pump it with the volume pedal.

## How it's modelled
- **Reeds.** Each reed is a brass tongue swinging through a slot (a damped mass-spring). Air from the
  room rushes through the slot into the low-pressure chest, and the tongue chops that flow into
  near-square pulses, shut each time it passes through the slot. The chopped flow is the sound.
  - A reed speaks only above a threshold suction, and speaks slower near it. Bass reeds are slowest:
    about 180 ms for C2 against 25 ms for C6 at moderate wind.
  - More suction swings the tongue harder: louder, and brighter as the chops get sharper.
  - A hard-driven tongue sags a few cents flat.
- **A worn-in instrument** (Tuning age; 1 = worn in, 0 = factory fresh).
  - Each key sits a few cents off its mark (up to about 4), all its stops roughly together. Each reed
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
don't fold back as haze. Ten notes on all six stops (52 reeds) use about 18% of one core.

## Build, test, listen
```
cmake --build build --target pumporgan_tests pumporgan_demo
ctest --test-dir build -R pumporgan
build/pumporgan/pumporgan_demo <folder>
```
The tests check tuning across C2-C7, the speech threshold and speech times, loudness and brightness
rising with wind, Melodia mellower than Diapason, the Celeste beating, the reservoir filling and
running dry, Expression responding only to strokes, the wind sagging under a big chord, the Celeste's
beat rate, the worn tuning spread, the pitch wander, stability and CPU.

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
