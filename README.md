# OrbitPan

Binaural 3D panner (VST3 + standalone) for headphones. Put it on a track and automate
where the sound sits around your head: azimuth, elevation and distance.

## How it works
- **Direction** - measured HRIRs (SADIE II, KU100 dummy head) stored as minimum-phase filters
  plus a separate fractional ITD, bilinearly interpolated on a 5 degree grid and crossfaded
  every 32 samples, so automation is click-free. Direction is smoothed as a 3D vector, so a
  jump from +170 to -170 degrees takes the short way round.
- **Focus** - crossfades between the measured filters and a set whose front/back and up/down
  spectral differences are exaggerated (ITD/ILD untouched). Generic HRTFs give weak
  elevation and front/back cues; this trades some naturalness for clarity. 0.7 by default.
- **Distance** - level, air-absorption low-pass, and a constant-level decorrelated 8-line FDN
  room, so the direct-to-reverberant ratio falls as the source moves away (this is also what
  keeps sounds outside the head).
- **Orbit** - free-running rotation added to azimuth, in Hz.

| Parameter | Range | Notes |
|---|---|---|
| Azimuth | -180..180 deg | 0 front, +90 right, +-180 behind |
| Elevation | -90..90 deg | +90 straight up |
| Distance | 0..1 | 0.3 m .. 15 m, exponential |
| Focus | 0..1 | direction-cue exaggeration |
| Room / Decay | 0..1 | room level / RT60 0.25-2 s |
| Orbit | -2..2 Hz | |

Input is summed to mono; output is stereo. Latency: 2 samples.

## Build
```
cmake -B build -DCMAKE_BUILD_TYPE=Release        # fetches JUCE 8.0.4
cmake --build build --target OrbitPan_VST3        # plugin
cmake --build build --target dsp_tests render_demo
ctest --test-dir build                            # DSP checks (ITD/ILD, cues, clicks, ...)
build/render_demo Resources/hrtf_d1_5deg.bin demos   # demo WAVs to audition on headphones
```
Linux needs the usual JUCE dev packages (ALSA, X11, freetype, GL). Use
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` for a local checkout, or
`-DORBITPAN_BUILD_PLUGIN=OFF` to build only the DSP tools.

Regenerate the HRTF table: `python3 tools/build_hrtf_table.py D1_48K_24bit_256tap_FIR_SOFA.sofa Resources/hrtf_d1_5deg.bin`
(needs numpy and h5py).

## Limitations
- Generic, non-individual HRTF: front/back and elevation accuracy varies by listener.
- No head tracking and no near-field (<1 m) ILD modelling; distance is level/spectrum/room based.
- Bluetooth codecs can smear the 6-12 kHz pinna cues; wired headphones localise best.

See `NOTICE` for HRTF data attribution.
