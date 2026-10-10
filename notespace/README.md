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
  touched. The first 40 ms of each note are left alone (then it comes in over the next 40), because a pick or finger attack is
  mostly residual. At -100 % the residual is "the bass with its own harmonics notched out", 18 dB up: on a clean bass that sounds
  like a comb or formant filter following the pitch. That is what the setting is, not a fault; it is meant for finding the edge.
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
- **Range** (50-1000 Hz): Contrast, and the kick duck, act on the residual below this.
- **Punch** (±100 %): attacks of the low band (the note plus the residual below Range) up to +10 dB or down to -10 dB, a smooth
  pulse (3 ms rise, 28 ms fall) placed where the attack really is although the detector fires a few ms late.
- **Sustain** (±100 %): the body's ring-out lengthened (up to +12 dB, only while the note is really decaying, not on a mute or a
  tremolo; the gain moves with a 15 ms smoother, so it does not ride the beating between a note and the mud next to it) or
  shortened. Both come from the shared `bass-common/EnvelopeShaper.h`.
- **Kick** (0-100 %, needs the optional sidechain input): ducks up to 24 dB while the sidechain's kick plays. Once a note is
  tracked only the residual below Range is ducked (the mud and boom around the note go, the note keeps its pitch). On a note's
  first 80 ms (fading out over the next 30), where a kick usually lands and needs room most, and on notes that are not
  tracked, everything below Range is ducked, as a plain sidechain duck would. The kick is read 5 ms ahead, so the duck is already there at the hit;
  it lets go in about 80 ms. A sidechain that carries the bass itself (at any level: a post-fader send is a scaled copy), or a kick below -70 dBFS,
  ducks nothing. (Ducking only
  the residual everywhere did nothing at all on a kick that lands on the note's attack: 0.0 dB under the hit, now -11.9 dB.)

The ranges are deliberately extreme, for finding the sweet spot by ear; expect to use much less.

## How it works
1. Pitch: YIN on a decimated copy (`bass-common/PitchTracker.h`, shared with the other bass plug-ins), read at the centre of its window. The note's phase theta(t) is the
   integral of the pitch (glides followed within a few ms, jumps taken at once).
2. Each harmonic h is heterodyned down to DC (the input times e^{-j h theta}) and averaged over exactly two periods, twice (a
   triangle over four periods). An average over whole periods cancels every other harmonic exactly, and anything between harmonics
   falls in the stopband, so each partial comes out on its own, even at 30 Hz, with no FFT and no frequency-resolution limit.
   The averages run on running sums, so the cost does not grow with the period.
3. At attacks. The four-period average straddles an attack (half old note, half new) and the centred pitch window only hears the
   new note half a window after it starts, so on their own they bring the processing in 40-90 ms late (a "flabby bloom"). The
   look-ahead already holds the new note, so at each attack (placed where the level really starts to rise, not where the
   detector fired) the note is measured with its own pitch, read from the first pitch window after the attack, over two-period
   boxes that start at the attack. The note before it is measured over its last two periods, if it is still sounding and steady.
   The two cross over 5 ms at the attack. The usual measurement takes over once its window is clear of the attack. Result: full
   effect 0-16 ms after the attack (was 42-88 ms), no added latency.

   Fast, short notes (funk 16ths, dead notes, rests): an attack counts when the level rises 10 dB from the quietest point of the
   last 40 ms after it had fallen to a quarter of its peak, so a note that follows a short gap is caught even when the level never
   drops to silence (before, a third of the notes in a 120 bpm 16th line were missed). The note's release (its level 15 dB under
   the attack's peak) is found in the look-ahead too: a short note's pitch is read from the attack to the release only, and its
   measurement holds to the release and then fades over one period instead of stopping (that stop clicked). If an attack has no
   readable pitch (a dead note), the note before it keeps its last-two-periods measurement and fades out over one period.
4. Partials = the sum of each envelope times e^{j h theta}; residual = input minus partials. Output = input + (processed partials -
   partials) + (residual gain - 1) x residual below Range, with the residual low-pass read ahead by its group delay so the cut lands
   in phase. Punch and Sustain then apply one gain to (processed note + processed residual below Range); the high residual is never
   touched.

Latency: **about 115 ms** (pitch window, the four-period average at 30 Hz, and the residual filter's alignment), reported to the host.
CPU: about 3.5 % of one core for stereo at 48 kHz (single-core figure on a desktop CPU).

## Where this sits among the other bass plug-ins
Note Space is the flagship. Bass Note Leveler stays as a separate tool for the one thing Note Space does not do: evening out
note-to-note level. Do not chain it before Note Space: its bells change the note's harmonics, and Note Space's Tone lock would then
see (and undo) the changed balance. Put the Leveler after Note Space, or skip Tone lock.

## No zipper noise
Every control value (voicing, residual gain, per-harmonic gains, attack and repair guards) glides sample by sample between control ticks
instead of stepping every 16 samples, which would put a click train at 3 kHz on top of the bass. What the plug-in adds above 1 kHz is
measured in the tests, against the dry signal's own energy above 1 kHz: Fundamental +6 dB -34 dB (was -14 dB before the fix), Repair 100 %
-38 dB (was -17), Contrast 100 % -79, Tone lock -45, Translate 100 % -48. Sustain 100 % adds nothing above -30 dB, and the kick duck on
a plucked line stays below -25 dB. Punch is gain modulation by definition (a +10 dB pulse with a 3 ms rise): -11 to -19 dB re the dry line's own energy above 1 kHz
(which is tiny, and depends on how many attacks there are), and -54 dB or lower re the whole signal.

## Limits
- Where the split is not trusted (attacks, no clear pitch, an 808 mid-glide, a chord) the kick duck falls back to ducking everything
  below Range, note included. Punch and Sustain do not depend on pitch and always act.
- A kick above -50 dBFS ducks by the full Kick amount whatever its level relative to the bass; there is no level-dependent scaling.
- A note is only trusted if at least 35 % of its energy sits on harmonics 1-4 of the tracked pitch (noise scores 0.45 at most, 0.33 at the
  99th percentile; a note 0.65 or more). The fundamental itself may be weak or missing: on a real recording, notes at 41.6 and 46.4 Hz had 3 %
  and 1 % of their energy at the fundamental and were found only after this test replaced a fundamental-only purity gate.
- Monophonic bass only. Chords, or two notes ringing together, are not split (processing fades out or follows one note).
- Fundamentals from about 31 Hz up. Below that nothing is processed.
- A note change with an attack is handled from the attack on (above). A note change with no attack the detector can see (legato, or a
  quieter note rising out of the last one's tail) still has the slower path: the split is not trusted for about four periods either
  side and the input passes through there. (Sustain is told about those changes, so it does not carry the last note's body into the
  new one.)
- The partials follow amplitude and pitch changes slower than about four periods: a very fast slap or a deep fast vibrato leaves some
  of the note in the residual, where Contrast would treat it as residual.
- Verified on synthetic bass only. This is a prototype: listen to the split in the demo (partials alone, residual alone) and on your own
  material before trusting it.

## Verified (`notespace/tests`, synthetic signals)
- Neutral: bit-exact delay, mono and stereo, at every block size.
- Attacks (two plucked notes, the second a fifth up, at 41, 62 and 98 Hz): Fundamental +12 dB and Translate 200 % reach full effect
  0-16 ms after the attack (42-88 ms before the attack-aligned measurement). On a staccato line nothing a control adds jumps from
  one sample to the next by more than half the dry line's largest step, and with everything turned up at once what is added has no
  discontinuity (Translate turned a harmonic's phase in a single sample when it crossed its floor: fixed).
- Fast funk line (16ths at 90 and 120 bpm, rests, dead notes, notes 60-70 % of a 16th long): Fundamental +12 dB is at full effect
  over 81-83 % of each note, no note missed (50-63 % and 14-29 of 79 notes missed before), and letting a note go adds no step
  larger than 0.02 against the dry line's 0.59 (0.41 before).
- The split, for notes at 31, 41, 62, 98 and 147 Hz: the note's harmonics left in the residual 35-39 dB down; a steady tone between the
  harmonics ends up in the residual (within 1.3 dB) and 17-26 dB down in the partials.
- Contrast +100 %: mud between the harmonics -18.7 dB (the residual gain is -40 dB; what is left is the part of the mud the split
  could not separate from the note), the note's harmonics within 0.07 dB; -100 %: mud +17.2 dB. A residual tone at
  800 Hz with Range 300 Hz is left alone. Unpitched rumble passes through untouched with every control at maximum.
- Tone lock 100 %: notes whose 2nd harmonic varies ±6 dB: spread 6.0 dB -> 1.7 dB.
- Fundamental +6 dB: +6.00 dB, the 2nd harmonic unchanged. Repair 100 %: a fundamental beating 3 Hz against a detuned component
  wobbles 6.2 dB before, 0.3 dB after. Translate 100 % on a pure sine: harmonics 2/3/4 at -6.0/-9.0/-12.0 dB, steady.
- A plucked line with everything on: what is added never jumps; the overall level stays put.
- Punch +100 %: attack vs body +2 dB or more; -100 %: -1.5 dB or less. Sustain +100 %: body +1.5 dB or more with the attack within
  1.5 dB; -100 %: the reverse.
- Kick 100 % with a 64 Hz kick over a steady note and a 64 Hz mud tone: the residual under the hit -6 dB or more (-7.3 dB through the
  real processor in all three layouts: stereo, mono, mono-in/stereo-out), the note's fundamental within 0.5 dB, released between hits;
  Kick 0 or no sidechain: bit-exact delay; the bass as its own sidechain: untouched; block sizes 1 and 333 identical.
- 44.1-192 kHz, NaN, fuzzing with random settings: finite.
- Steinberg VST3 validator 47/47, pluginval strictness 10, ASan/UBSan clean.
