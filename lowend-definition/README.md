# Low-End Definition

Makes a bass note more *defined*: the fundamental stands out from the low-band mud around it, the attack can be
pushed forward or tamed, and the body can be lengthened or shortened. It only touches what is below **Range**
(60–300 Hz); everything above passes through untouched.

Where a contrast tool such as Ozone's Low End Focus works on level alone, this one **knows which note is playing**,
so it can tell the note from the stuff between the harmonics.

## Controls
- **Contrast** (−100…+100 %): the pitch-aware part. A bell sits on the current note's fundamental and an opposite bell
  between the 1st and 2nd harmonic. Positive tightens (fundamental up to +6 dB, mud between harmonics down to −4 dB),
  negative softens (the reverse). The boost backs off by itself as the note gets clean, so it cannot over-cook a pure tone.
- **Punch** (−100…+100 %): emphasise or tame the attack. A smooth gain pulse is placed *on* the detected onset
  (up to ±10 dB, strength follows how hard the level jumped).
- **Sustain** (−100…+100 %): lengthen or shorten the body. Driven by how fast the note is dying, after the first ~30 ms,
  and it lets go when the note is over.
- **Range** (60–300 Hz): crossover. Complementary split (high = input − low), so with all three controls at 0 the
  output is the bit-exact input, delayed.

The panel shows a **definition meter** (share of the low band's energy that sits on the note's fundamental, input vs
output, plus a 5-second history) and the punch/sustain gain.

## How it works
- The audio is held in a **look-ahead delay of 71 ms** (reported to the host, so plug-in delay compensation applies;
  not for live monitoring). That is what lets it know the note, and where the attack really starts, before you hear them.
- Pitch: YIN on a decimated copy of the low band. Near a note change the window that starts at the current sample and
  the one that ends there disagree; an energy onset between them says which one is pure, so the bell is on the right note
  from the first sample of the note.
- Low band: a Linkwitz-Riley split, read advanced by the filter's group delay so that gain changes line up with the audio in time.
- Everything is sample-by-sample with control updates every 16 samples: the output does not depend on the host's block size.

## What it does not do (yet)
- No kick/bass phase alignment (needs a sidechain and a way to find the kick); no mono-sub control.
- Monophonic material only: on chords the pitch tracker will pick one voice or give up (then it does nothing).
- Verified on synthetic bass, not on real recordings. Treat the numbers below as "the algorithm does what it claims on
  clean material", not as a promise about your bass.

## Verified (`lowend-definition/tests`, synthetic bass)
- All controls at 0: bit-exact delay. A 3 kHz tone is untouched with every control up (0.006 dB).
- Pitch: 48/48 notes tracked over >80 % of their length, for 550 ms-spaced and 200 ms-spaced notes; white noise
  stays unpitched (no contrast).
- Contrast +100 % on a held note with mud between its harmonics: fundamental +5.6 dB, mud −3.2 dB, 2nd harmonic −0.3 dB;
  −100 % does the reverse. Definition 74 % → 92 %. A pure sine gets 0.00 dB of boost.
- Punch ±100 %: attack vs body +6.7 / −6.0 dB. Sustain ±100 %: body +3.0 / −1.9 dB with the attack unchanged.
- The gain moves at most 1.9 dB per ms (punch + sustain + contrast all up); what the plug-in adds never jumps.
- Output identical for block sizes 1 to 4096; works at 44.1–192 kHz; survives NaN, DC, silence.
