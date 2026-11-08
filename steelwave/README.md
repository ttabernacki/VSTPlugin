# Steelwave

A physically modelled E9 pedal steel. No samples: every sound is synthesised. The output is dry,
DI-style mono for your own amp and pedals.

**Status: milestone 1, the string model.** The playable plug-in (chord sets, pedals, knee levers,
MIDI) is not built yet. This folder has the DSP core, its tests and a demo renderer.

## The string model (`core/SteelString.h`)
Each string is two digital waveguide loops, one per vibration plane (horizontal and vertical).
They are detuned by under a cent and decay at different rates, which gives the slow beating of a
real string. Each loop is:
- **Time-varying fractional delay:** 6-point Lagrange interpolation, with the length recomputed
  every sample from a smoothed pitch. Bar slides and pedal bends are continuous, with no zipper or clicks.
- **Loss filter:** one pole, designed from T60 at low frequencies and at 5 kHz. Decay times stay
  in seconds as the bar moves.
- **Stiffness:** an allpass cascade, solved per string from a physical target (how sharp the 10th
  partial is, 1.5-3.5 cents). The string gets stiffer as the bar shortens it.
- **Tuning compensation:** the loop's phase delay at the fundamental is subtracted, so every
  string is within 0.06 cents at every bar position and sample rate (tested).

Pitch is split the way the instrument does it:
- **Bar:** shortens the string. The wave speed stays the same.
- **Pedals and knee levers:** change the tension, so the wave speed changes.
- **Vibrato:** bar rocking, applied on top.

The pick and pickup are placed in metres from the bridge. Their comb filters therefore track the
bar the way they do on the instrument.

| Part | Model |
|---|---|
| Pick | Velocity pulse lasting the wave's round trip to the pick point, smoothed over the pick's contact time (harder pick or harder playing = shorter = brighter), plus a metal tick at 2.5-4.5 kHz. A hard pick stretches the string, so the note starts up to ~7 cents sharp and settles in ~70 ms. Re-picking a ringing string first catches it briefly |
| Bar noise | From the bar's speed, on an S-curve (silent while the bar creeps into place, saturating on fast slides). On wound strings, grainy noise ticking at the rate the wrap ridges pass under the bar. On plain strings, a fainter hiss. Band-limited to 400 Hz-3 kHz and heard mostly directly (only 10% excites the string). About 35 dB below a medium pick on wound strings and 43 dB on plain ones (tested) |
| Hand block | Fast ramp of both decay times |
| Pickup | Comb filter at the pickup's position, plus a resonant coil/cable low-pass (single-coil or humbucker) |

CPU: 10 strings use about 4% of one core at 48 kHz.

## Build and listen
```
cmake -B build -DORBITPAN_BUILD_PLUGIN=OFF
cmake --build build --target steelwave_tests steelwave_demo
./build/steelwave/steelwave_tests
./build/steelwave/steelwave_demo <folder>   # writes 01_picks .. 05_vibrato .wav
```

## Roadmap
1. ~~String model~~
2. 10-string E9 engine: A/B/C pedals and LKL/LKR/RKL levers, bridge coupling between strings,
   volume pedal (CC11), sustain pedal
3. Chord and single-note sets: white keys diatonic and bending, black keys held, keyswitches,
   transpose, legato bar glides, mod-wheel vibrato
4. JUCE plug-in (VST3, AU, standalone) with a plain knob panel
