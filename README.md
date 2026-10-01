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
- **Rear** - explicit 'behind' cues scaled by how far behind you the source is (front and sides
  are untouched): a -10 dB high shelf above 4 kHz (pinna shadow), a +3.5 dB band at 1.2 kHz
  (Blauert's 'behind' band), a slightly quieter direct path and more diffuse room. At Rear 1.0
  the front/back spectral distance goes from about 4.5 dB to 12 dB in the DSP tests.
- **Depth** - bipolar, heavily exaggerated push/pull on top of Distance (0 = neutral, bit-identical to
  before). Close: +4 dB, +10 dB proximity bass at 250 Hz, +4 dB presence shelf at 6 kHz, dry signal,
  and near-field ILD (near ear +2 dB, far ear -9 dB for a lateral source). Far: -20 dB, a 12 dB/oct
  low-pass down to ~2 kHz, and room up to 4.5x (direct-to-reverberant ratio falls by ~44 dB).
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
| Rear | 0..1 | extra cues when the source is behind you |
| Depth | -1..1 | far away .. right at your face (0 neutral) |
| Room / Decay | 0..1 | room level / RT60 0.25-2 s |
| Orbit | -2..2 Hz | |

Input is summed to mono; output is stereo. Latency: 2 samples.

## Interface
- **Position pad (top-down view of the head):** drag the dot anywhere. Angle around the head is
  left / right / front / back; distance from the head is Distance (rings at 1, 3 and 10 m). The
  shaded half is behind you (where Rear applies). Double-click resets, mouse wheel changes
  distance. The dot's colour and size show Depth; the trail and hollow ring show Orbit/automation.
- **Height slider (side view):** drag for elevation; double-click resets, wheel nudges.
- **Depth strip:** far <-> close, exaggerated beyond the pad's distance.
- **Knobs:** Focus, Rear (localisation cues), Room, Decay, Orbit. Double-click any control to reset.
- The window is resizable. `ui_snapshot` renders the real editor to PNG without a display.

## Using it in Ableton Live
Live runs on Windows and macOS only, so use the CI build artifacts (Actions tab, `OrbitPan-windows`
or `OrbitPan-macos`) rather than the Linux build.
- **Needs Live 11 or newer** (VST3). On macOS the AU is also built.
- **Install:** Windows `C:\Program Files\Common Files\VST3\OrbitPan.vst3`; macOS
  `~/Library/Audio/Plug-Ins/VST3/OrbitPan.vst3` (AU: `~/Library/Audio/Plug-Ins/Components/`). In Live:
  Preferences > Plug-ins > enable "Use VST3 Plug-in System Folders", then rescan. The macOS build is only
  ad-hoc signed, so clear the download quarantine: `xattr -dr com.apple.quarantine OrbitPan.vst3`.
- **Recording automation:** arm automation (A), then drag the pad. A drag writes **Azimuth + Distance**
  together (two lanes, with touch/release gestures, so Touch and Latch behave as with any plug-in);
  the height slider writes **Elevation**; the depth strip writes **Depth**. All nine parameters are
  automatable and show in the device's automation chooser.
- **Azimuth lane:** it spans two turns (-360..360, centre 0 = front; the lane's tooltip shows
  "front", "45 deg R", "behind", ...). Recorded pad drags are unwrapped, so circling the head - including
  through the back - records one continuous line. To draw it by hand, keep going past 180
  (e.g. 170 -> 190) instead of jumping to -170.
- **Orbit** is locked to Live's timeline: loops, scrubbing, Freeze/Flatten and Export all put the source
  in the same place at the same time.
- **Latency/tail:** 2 samples (reported, so plug-in delay compensation applies); 2.5 s reverb tail.
- Parameters are applied per host block and ramped across it in 64-sample steps (not sample-accurate),
  with additional smoothing in the engine, so large buffers do not make automation sound stepped.

## Verification
- `dsp_tests`: ITD/ILD, front/back and elevation cues, Rear/Depth/Focus behaviour, no clicks.
- `plugin_tests`: parameters and text entry, bus layouts, state recall, block-size independence
  (1 to 4096 samples), 22-192 kHz, 3000 blocks of random automation/NaN input, timeline-locked Orbit.
- Tracktion `pluginval` at strictness 10 and Steinberg's VST3 `validator` (47/47) on the built VST3.
- CI (`.github/workflows/build.yml`) builds and runs all of the above on Linux, Windows and macOS.
  Not yet tried inside Live itself.

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
