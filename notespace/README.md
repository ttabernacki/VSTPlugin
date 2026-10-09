# Note Space

A low-end processor that works in the note's own coordinates. Every sample of a bass is split into

- **the partials**: harmonics 1 to 8 of the note being played, each followed as a slowly changing amplitude and phase, and
- **the residual**: everything that is not the note (room boom, mud between the harmonics, string and pick noise, rumble, bleed).

The controls act on that split, so the plug-in can do things a frequency-domain tool cannot: turn the mud down without touching
the note even where they overlap in frequency, give every note the same timbre, or replace a wobbly fundamental with a steady one.

With every control at neutral the output is the input, bit for bit (delayed). Where the split cannot be trusted (no clear pitch,
a note change inside the analysis window) the processing fades out and the input passes through.

## Controls
- **Contrast** (-100 to +100 %): the residual below **Range**, down to -40 dB (+) or up to +18 dB (-). The note's partials are not
  touched. The first ~40 ms of each note are left alone, because a pick or finger attack is mostly residual.
- **Tone lock** (0-100 %): each harmonic's share of the note is pulled toward its long-term average (±24 dB at most), so a note that
  a room or cab makes boomy or thin comes out like the others. It locks timbre, so it also flattens deliberate tone changes.
- **Fundamental** (±18 dB): the fundamental alone.
- **Repair** (0-100 %): the fundamental is replaced by its slow part (its complex envelope through two smoothing poles). The blend
  reaches 100 % at the halfway point; above that the smoothing stretches from 60 to 200 ms.
  Beating against a detuned component, room-mode wobble and chorus all rotate against the note and average out; what is left is a
  steady sine locked to the note's phase. It is kept off the first ~140 ms of each note, where it would only lag the attack.
- **Translate** (0-200 %): harmonics 2, 3 and 4 are lifted to at least 6, 9 and 12 dB below the fundamental at 100 %, and generated
  (phase-locked to the fundamental) if the bass has none, so the note keeps its pitch on small speakers and earbuds. Above 100 %
  the floor rises by up to 9 dB (+3/0/-3 dB re the fundamental at 200 %).
- **Range** (50-1000 Hz): Contrast acts on the residual below this.

The ranges are deliberately extreme, for finding the sweet spot by ear; expect to use much less.

## How it works
1. Pitch: YIN on a decimated copy (as in Low-End Definition), read at the centre of its window. The note's phase theta(t) is the
   integral of the pitch (glides followed within a few ms, jumps taken at once).
2. Each harmonic h is heterodyned down to DC (the input times e^{-j h theta}) and averaged over exactly two periods, twice (a
   triangle over four periods). An average over whole periods cancels every other harmonic exactly, and anything between harmonics
   falls in the stopband, so each partial comes out on its own, even at 30 Hz, with no FFT and no frequency-resolution limit.
   The averages run on running sums, so the cost does not grow with the period.
3. Partials = the sum of each envelope times e^{j h theta}; residual = input minus partials. Output = input + (processed partials -
   partials) + (residual gain - 1) x residual below Range, with the residual low-pass read ahead by its group delay so the cut lands
   in phase.

Latency: **about 115 ms** (pitch window, the four-period average at 30 Hz, and the residual filter's alignment), reported to the host.
CPU: about 3.5 % of one core for stereo at 48 kHz (single-core figure on a desktop CPU).

## No zipper noise
Every control value (voicing, residual gain, per-harmonic gains, attack and repair guards) glides sample by sample between control ticks
instead of stepping every 16 samples, which would put a click train at 3 kHz on top of the bass. What the plug-in adds above 1 kHz is
measured in the tests, against the dry signal's own energy above 1 kHz: Fundamental +6 dB -34 dB (was -14 dB before the fix), Repair 100 %
-38 dB (was -17), Contrast 100 % -79, Tone lock -45, Translate 100 % -48.

## Limits
- Monophonic bass only. Chords, or two notes ringing together, are not split (processing fades out or follows one note).
- Fundamentals from about 31 Hz up. Below that nothing is processed.
- Around a note change (about four periods either side, plus a few ms) the split is not trusted and the input passes through.
- The partials follow amplitude and pitch changes slower than about four periods: a very fast slap or a deep fast vibrato leaves some
  of the note in the residual, where Contrast would treat it as residual.
- Verified on synthetic bass only. This is a prototype: listen to the split in the demo (partials alone, residual alone) and on your own
  material before trusting it.

## Verified (`notespace/tests`, synthetic signals)
- Neutral: bit-exact delay, mono and stereo, at every block size.
- The split, for notes at 31, 41, 62, 98 and 147 Hz: the note's harmonics left in the residual 35-39 dB down; a steady tone between the
  harmonics ends up in the residual (within 1.3 dB) and 17-26 dB down in the partials.
- Contrast +100 %: mud between the harmonics -18.7 dB (the residual gain is -40 dB; what is left is the part of the mud the split
  could not separate from the note), the note's harmonics within 0.07 dB; -100 %: mud +17.2 dB. A residual tone at
  800 Hz with Range 300 Hz is left alone. Unpitched rumble passes through untouched with every control at maximum.
- Tone lock 100 %: notes whose 2nd harmonic varies ±6 dB: spread 6.0 dB -> 1.7 dB.
- Fundamental +6 dB: +6.00 dB, the 2nd harmonic unchanged. Repair 100 %: a fundamental beating 3 Hz against a detuned component
  wobbles 6.2 dB before, 0.3 dB after. Translate 100 % on a pure sine: harmonics 2/3/4 at -6.0/-9.0/-12.0 dB, steady.
- A plucked line with everything on: what is added never jumps; the overall level stays put.
- 44.1-192 kHz, NaN, fuzzing with random settings: finite.
