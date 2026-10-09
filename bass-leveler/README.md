# Bass Note Leveler

Evens out the level of individual notes on a **monophonic bass line**, automatically, without a
compressor's pumping. Room, cab and instrument resonances make some notes boom and others disappear;
this plug-in measures every note, compares it with the typical level of the recent notes, and corrects it
with a narrow bell filter on that note's own fundamental, from the first sample of the note.

There is nothing to learn or set up: put it on a mono bass track and play.

## How it works
- The input is analysed (YIN pitch tracker, energy-onset detector, note segmentation) while the audio is
  held in a **look-ahead delay of about 132 ms**. By the time a note's first sample leaves the delay, the plug-in
  already knows its pitch and how loud its fundamental is, so the correction is in place before you hear it.
  The delay is reported to the host (plug-in delay compensation applies); it is not for live monitoring.
- **Reference:** the median of the last 24 notes. It is trusted after about 4 notes and keeps adapting, so
  it follows the part. Each note is moved toward it by **Amount**.
- **What is leveled:** the level of the whole note (harmonics 1-4 together), evened out across notes, dynamics included.
  The correction is still a bell on the fundamental, so the plug-in works out the fundamental gain that moves the whole note
  by the right amount: a resonance on the fundamental is corrected fully. A note whose fundamental carries under 5 % of its
  energy cannot be moved by its fundamental and is left alone (the correction fades in between 5 and 20 %).
  Measuring the fundamental alone (as before) went wrong on a real recording: notes whose energy sat on the 2nd to 4th
  harmonics looked quiet, and the healthy notes around them were cut by 8 dB.
  (An earlier *Balance* mode and a *+ 2nd harmonic* bell were removed: Note Space's **Tone lock** does the
  harmonic-balance job on the separated partials, without bells on the full signal.)

## Sounding natural (no brick wall)
- The correction curve is smooth everywhere: a small dead zone (a note within about 1 dB of typical is barely
  touched), an ease-in, then a soft ceiling. **Max boost / Max cut** are soft ceilings the curve eases toward,
  not hard limits: there is no corner where the correction suddenly stops.
- The correction is one steady gain per note (no pumping during the note, no release tail to breathe).
  It ramps in about 12 ms *before* the attack arrives (attack time is about 40% of **Speed**), and the
  previous note releases on its own bell while the new note starts on a second one, so a bell never jumps
  from one pitch to another while it has gain.
- **Amount** below 1 is a ratio: 0.5 corrects about half of each note's distance from typical.
  Ghost notes and accents keep some of their character.
  Slides and bends keep one note and the bell follows the pitch; if a slide moves more than 3 semitones from
  where the note was measured, the correction eases away.

## Verified (synthetic bass with injected resonances; `bass-leveler/tests`)
- 48/48 notes found and pitched correctly, no phantom notes; fast 8th notes, hammer-ons (8/8) and a slide
  (one note) handled.
- No learning pass: resonance spread between pitches **-75%** (Amount 1.0, 3.6 dB to 0.9 dB);
  playing dynamics are evened out too (2.2 dB to 0.8 dB note-to-note). Equally loud notes, some with weak
  fundamentals: no correction (was up to 8.8 dB).
- Natural behaviour: the correction curve is monotonic, steepest slope < 1, no slope jump larger than
  0.02 per 0.01 dB step; the bell gain moves at most 0.35 dB in any millisecond; only 1.1% of what the
  plug-in adds to the signal lies above 600 Hz (no clicks or splatter).
- Transparent: Amount 0 and audio with no pitch to track pass through as a bit-exact delay; already-even bass
  changes by < 0.01 dB. The first two notes are left alone while the reference forms.
- Output is bit-identical for any host block size (1 to 4096); 44.1 to 192 kHz; NaN/inf input is harmless.
- Steinberg VST3 validator 47/47 and pluginval strictness 10 on the Linux build; CI runs pluginval on Windows
  and macOS too.

## Limits (be aware)
- **Never run in a DAW and never tried on real bass recordings.** All numbers above are from synthetic bass.
  Real playing (ghost notes, string noise, buzz, heavy distortion) will be harder than the tests.
- Monophonic lines only. Chords, heavy distortion and fundamentals below about 28 Hz are not tracked; audio
  that is not recognised passes through unchanged.
- The reference is "what the recent notes are like": a part that mostly repeats one note will pull other notes
  toward that note's level. Lower **Amount** if that is not what you want.
- `bass_demo out.wav` (built with the other tools) renders a synthetic riff dry and then leveled, to audition
  for artifacts.
