# Bass Note Leveler

Evens out the level of individual notes on a **monophonic bass line** without a compressor's pumping.
Room, cab and instrument resonances make some notes boom and others disappear; this plug-in learns how
loud each pitch comes out, then puts a narrow bell filter on the played fundamental, from the first
sample of the note, to move it back toward the common level.

## Use
1. Put it on the bass track. Turn **LEARN** on and play the whole part once. (Audio passes through
   unchanged while learning; the bars show the level it measured for every pitch, the dashed line is the
   common target.)
2. Turn LEARN off. Each pitch is now corrected toward the target.
3. **Strength** = how far to correct. **Rider** adds a live per-note correction on top (this fixes
   variation *within* one pitch, e.g. soft and hard notes, which a per-pitch table cannot).
4. **Mode:** *Balance* = fundamental relative to its own harmonics (keeps playing dynamics, fixes
   resonances); *Level* = absolute fundamental level (flattens everything). **+ 2nd harmonic** puts a second
   bell an octave up, for notes where the 2nd harmonic carries the pitch.
5. **Max boost / Max cut / Speed** limit the correction and set how fast the gain moves inside a note.
The learned table is saved with the project. *Clear table* forgets it.

## How it works
- The input is low-passed, decimated and analysed (YIN pitch tracker, energy-onset detector, note
  segmentation). Each note's fundamental and 2nd-4th harmonics are measured on a window of exactly K periods,
  so nothing leaks between them.
- The audio runs through a look-ahead delay so the note's pitch and correction are known **before** its first
  sample reaches the output. **Latency is about 132 ms and is reported to the host** (plug-in delay
  compensation applies); it is not for live monitoring.
- Hammer-ons and pull-offs (pitch jumps without a new attack) start a new note; slides and bends stay one note
  and the bell follows the pitch.

## Verified (synthetic bass with injected resonances; `bass-leveler/tests`)
- 48/48 notes found and pitched correctly, no phantom notes; fast 8th notes, hammer-ons (8/8) and a slide
  (one note) handled.
- Resonance spread between pitches: **-79%** in Balance mode (3.7 dB to 0.8 dB), -61% in Level mode.
  Rider cuts note-to-note spread inside one pitch from 2.2 dB to 0.6 dB.
- Transparent on already-even bass (max change 0.02 dB); with nothing learned the output is the input delayed
  by exactly the reported latency.
- Output is bit-identical for any host block size (1 to 4096), 44.1-192 kHz, NaN/inf input is harmless.
- State round trip: a reloaded project corrects exactly like the original.
- Steinberg VST3 validator 47/47 and pluginval strictness 10 on the Linux build; CI runs pluginval on Windows
  and macOS too.

## Limits (be aware)
- **Never run in a DAW and never tried on real bass recordings.** All numbers above are from synthetic bass.
  Real playing (ghost notes, noise, string buzz, heavy distortion) will be harder than the tests.
- Monophonic lines only. Chords, heavy distortion and fundamentals below about 28 Hz are not tracked.
  Audio that is not recognised passes through unchanged.
- A per-pitch table fixes resonances, not playing dynamics; that is what Rider is for.
- The display is read-only (no per-pitch manual override yet).
