# Low-End Evaluator

Analysis-only: grades the 20-250 Hz region of a mix and lists numeric fixes. It never changes the audio.

**The judge is yours.** Instead of shipping genre profiles, you feed it reference mixes that have the low end you want
(your "vibe"). It measures each one, learns the range they span on every metric, and saves that as a profile. Your
mix is then scored against that profile, and each recommendation says what to change, by how much, and what it is
predicted to do.

## Status

| Phase | What | State |
|---|---|---|
| 1 | Headless core (`core/`, no JUCE) + `lee` CLI + synthetic-defect tests | **done** |
| 2 | Plug-in shell: capture the mix live or from a loop; drop reference files onto the plug-in to build or extend a profile (same core, background thread) | next |
| 3 | UI: grades, evidence tabs (spectrum vs. reference band, dynamics, stereo, translation), recommendation cards, A/B snapshots, 4 s heat strip | |
| 4 | Validation on real material: your reference sets, leave-one-out stability, blind A/B of applied fixes | |

## Workflow

```
lee profile -o my_vibe.json --name "Dark trap" refs/          # 8-20 reference mixes (WAV, any rate/bit depth)
lee judge my_mix.wav -p my_vibe.json --report r1.json         # grades + ranked fixes
# ...apply fixes, bounce the same section...
lee judge my_mix_v2.wav -p my_vibe.json --compare r1.json     # which recommendations got resolved
```

- `--stage mix` (default): an unmastered mix is not penalised for being *more* dynamic than mastered references, and
  true peak is not gated. Use `--stage master` for a mastered file.
- The profile is plain JSON. Edit any `center`/`band`/`sigma`/`weight`, or set `"muted": true` on a metric you mean
  (a deliberately wide sub, say): it then leaves scoring and recommendations.
- `lee profile` prints a **leave-one-out check**: each reference judged against a profile of the others. A reference
  that scores low does not fit the set: listen to it before deciding whether it belongs.

## How the profile is learned

For each metric over the references: center = median; no-penalty band = interquartile range (10th-90th percentile
with fewer than 10 references, since quartiles of a handful of tracks are noise); sigma = half the 10th-90th
percentile width, widened by sqrt(10/n) for small sets. Per-metric floors keep a very consistent reference set from
punishing inaudible differences. The references' 1/3-octave curve (re 500 Hz-2 kHz, so it is level-independent and
clear of low-mid moves) is stored the same way and drives EQ suggestions. Metrics a reference could not measure
reliably (confidence < 0.3) are left out of that metric's statistics.

## Metrics (34 scored + 3 technical gates)

Measured on a full mix (the reference material is full mixes). Kick-bass metrics that need separate kick and bass
signals (spectral overlap, coherence) are not computed from a single mix; they arrive with the sidechain input in phase 2.

| Group | Metrics |
|---|---|
| Clarity | band levels 20-40/40-60/60-90/90-120/120-180/180-250 Hz (re 20-250 Hz), tilt 30-300 Hz, sub-to-bass, mud vs bass, mud vs mids, resonance prominence, harmonic definition (harmonics 2-5 vs f0), pitched share, mono fold-down loss, low-band correlation (5th percentile), side-to-mid |
| Dynamics | infrasonic energy, low-band crest, low-band dynamic range, note-to-note consistency (pitch trend removed), pitch-level trend, transient-to-sustain, low-band PLR (unweighted: K-weighting would gut this band) |
| Translation | bass presence through 8 playback models (phone, laptop, Bluetooth, earbuds, headphones, car, club, ported monitors) with ISO 226 equal-loudness weighting, missing-fundamental support (harmonics after the model), low-volume shift (80 vs 40 phon), cross-system spread |
| Gates | true peak (master stage), clipping runs and inter-sample overs, DC offset |

Paths: true peak / clipping / DC at 48 kHz; spectra and low band on a 6 kHz decimated copy (16384-point Welch = 0.37 Hz
bins, so 1/12 octave resolves at 25 Hz); pitch with the shared `bass-common/PitchTracker.h`. Everything is resampled
to 48 kHz first. Deterministic: the same file gives bit-identical numbers.

## Scoring

Per metric: d = distance outside the band in sigmas; score = 100 exp(-d²/2). Category: weighted power mean (p = 0.5;
weight × confidence; metrics under 0.3 confidence excluded). Composite: Clarity 0.4, Dynamics 0.3, Translation 0.3,
renormalised over categories with data. Gates cap the composite (clipping or true peak > 0 dBTP: 59; low-band
correlation below -0.5: 64; DC > 1 %: 69); the uncapped value is always shown.

## Recommendations

Rules fire at d >= 1 (one sigma outside the band, not at the band edge, because with an interquartile band a quarter
of the references themselves sit outside it on any metric).

- **Simulated** (EQ bells and shelves from clustering the curve deviation, high-pass, mono below X Hz): the fix is
  applied to a copy of the audio, which is re-analysed and re-scored. The predicted metric changes and composite gain
  are measured. Moves worth less than one point (of the uncapped score, under a gate) are dropped. All simulated fixes
  are also applied together, in order (clipping, stereo, high-pass, EQ, dynamics, saturation), for a combined
  prediction.
- **Estimated** (compression, transient shaping, saturation): the targeted metrics are moved to their band edge and
  re-scored. These are labelled as estimates.
- Ranked by predicted gain × confidence / effort. Under a gate, the uncapped gain also counts.
- `--compare` marks each earlier recommendation fixed / improved / unchanged / worsened (0.25 sigma threshold).

## Build and test

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DORBITPAN_BUILD_PLUGIN=OFF
cmake --build build --target lee lee_tests
ctest --test-dir build -R lee
```

`lee_tests` checks DSP conformance (Welch power, resampling, true peak, ISO 226, WAV I/O). It then builds a profile
from 10 clean synthetic mixes and judges mixes with one known defect each: a +6 dB bell at 220 Hz, anti-phase low
end, 12 Hz rumble, clipping, uneven note levels, and bass 6 dB too loud. Each defect must flag its metric, the right
fix must rank and help, and a clean held-out mix must score A with no recommendations.

## Limits (so far)

- Playback models are starting estimates, not measured responses.
- Notes come from a monophonic tracker. 808 glides and heavy distortion lower confidence rather than giving wrong
  numbers.
- The synthetic tests prove the plumbing. Whether the grades track what you hear is phase 4.
