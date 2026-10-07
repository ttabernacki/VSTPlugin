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
  and it lets go when the note is over. A muted note-off decays far faster than a natural ring-out, so the detector is
  soft-limited and the end of notes is not pumped.
- **Match loudness** (on by default): contrast moves energy toward the note, which by itself would just make the low
  band louder (+4.6 dB at +100 % on the test note). With Match on, the low band is trimmed back to within about ±1.5 dB, so you
  judge the definition and not the level. Switch it off if you want the raw boost.
- **Kick** (0–100 %) and the **sidechain input**: route the kick track to the plug-in's sidechain in Ableton (put
  Low-End Definition on the bass, pick the kick track as the sidechain source). Where the kick masks the bass, the bass is
  ducked per frequency (up to 12 dB at 100 %), but the **note's own harmonics are protected** (the pitch tracker knows where
  they are), so the duck falls on the mud and on the kick's range, not on the bass note itself. It starts a few ms before the kick
  and lets go over about 45 ms. With no sidechain connected, or at 0 %, nothing changes.
- **Kick mode**: *Simple* ducks each frequency in proportion to the kick's share of the energy there. *Masking* asks which bass
  components actually cover the kick up and ducks just enough to uncover it (up to 12 dB at Kick 100 %): the spectra are spread through
  auditory filters (roex, ERB widths), a tonal bass masks only about 14 dB down (5 dB if it is noisy), a kick that is only
  a few dB below the bass counts as audible, and the sub is weighted down because the ear (and AirPods) hear it less. In practice
  Masking does **much less** than Simple when the kick is as loud as the bass, and acts mainly when the bass swamps the kick.
  It is a textbook masking model, not Gullfoss's (which is unpublished), and I could only test it on synthetic signals, so it is
  there to be A/B'd by ear. Default: Simple.
- **Auto polarity**: a running correlation of kick and bass (gathered only while both play) says whether they partly cancel.
  The footer shows the summed level against the difference (positive = they add, negative = they fight) and the lag at which
  the bass would line up best. With Auto polarity on, the whole bass polarity is flipped (30 ms crossfade, with hysteresis)
  when the sum is more than 1 dB worse than the difference. The reading is only meaningful if the bass starts with the same
  phase against the kick each time (sampled or well-gated bass); with free-running oscillators it averages out to nothing.
- **Range** (60–300 Hz): crossover. Complementary split (high = input − low), so with all three controls at 0 the
  output is the bit-exact input, delayed.

The panel shows a **definition meter** (share of the low band's energy that sits on the note's fundamental, input vs
output, plus a 5-second history) and the punch/sustain gain.

## How it works
- The audio is held in a **look-ahead delay of 91 ms** (reported to the host, so plug-in delay compensation applies;
  not for live monitoring). That is what lets it know the note, and where the attack really starts, before you hear them.
- Pitch: YIN on a decimated copy of the low band. Near a note change the window that starts at the current sample and
  the one that ends there disagree; an energy onset between them says which one is pure, so the bell is on the right note
  from the first sample of the note.
- Kick: a 72 ms short-time spectrum (87.5 % overlap, sqrt-Hann) of the low band of the bass and of the kick, on a 5 kHz
  decimated copy. Per bin, the share of energy that is the kick sets how far that bin is ducked; bins within about one bin of
  the note's harmonics 1-5 are protected. The difference signal is overlap-added inside the look-ahead (this is what grew
  the delay from 71 to 91 ms), interpolated back to full rate and added to the output.
- Low band: a Linkwitz-Riley split, read advanced by the filter's group delay so that gain changes line up with the audio in time.
- Everything is sample-by-sample with control updates every 16 samples: the output does not depend on the host's block size.

## What it does not do (yet)
- It does not apply a delay for kick/bass alignment (only reports the lag, and optionally flips polarity). The frequency resolution of
  the spectral stage is about 14 Hz per bin, so a bass fundamental and a kick that sit within a bin of each other (say 41 and 50 Hz)
  cannot be separated spectrally; the duck helps where they differ in frequency or in time.
- No mono-sub control.
- Monophonic material only: on chords the pitch tracker will pick one voice or give up (then the harmonics are not protected
  and the duck is plain spectral ducking).
- Meant for bass tracks. A kick drum *in the main input* is treated as a note.
- Verified on synthetic bass and kick, not on real recordings. Treat the numbers below as "the algorithm does what it claims on
  clean material", not as a promise about your mix.

## Verified (`lowend-definition/tests`, synthetic bass)
- All controls at 0: bit-exact delay. A 3 kHz tone is untouched with every control up (0.006 dB).
- Pitch: 48/48 notes tracked over >80 % of their length, for 550 ms-spaced and 200 ms-spaced notes; white noise
  stays unpitched (no contrast). Also checked on a saturated 808 with a pitch drop, a bass whose 2nd harmonic is
  louder than its fundamental, and a heavily driven bass: 98-100 % of note time tracked on the right octave, no octave errors.
- Contrast +100 % on a held note with mud between its harmonics: the fundamental gains 5.8 dB and the mud loses 3.0 dB relative to the
  2nd harmonic; -100 % does the reverse. Definition 74 % to 92 %. A pure sine gets 0.00 dB of boost.
- Punch ±100 %: attack vs body +6.7 / -6.0 dB. Sustain ±100 %: body +2.8 / -1.9 dB with the attack unchanged.
- The gain moves at most 1.9 dB per ms (punch + sustain + contrast all up); what the plug-in adds never jumps; at default
  settings the output peak rises 1.5 to 2.9 dB on those test basses (mostly the punch pulse).
- CPU: about 1.5 % of one core (stereo, 48 kHz).
- Output identical for block sizes 1 to 4096; works at 44.1-192 kHz; survives NaN, DC, silence.
- Kick (synthetic, a 64 Hz mud on the bass against a 64 Hz kick burst): the mud is ducked 5.7 dB during the hit, the note's
  fundamental moves -0.3 dB, and between hits the mud is untouched (-0.0 dB). Bit-exact delay at Kick 0 % and with no sidechain.
  Through the real processor with a stereo sidechain bus: mud -6.2 dB, fundamental -0.7 dB. The duck starts about 40 ms before the
  hit and is down by about 25 dB (re the bass) 200 ms after it.
- Polarity: an opposite-phase bass is detected (-19 dB sum vs difference), flipped, and kick+bass then sum 18 dB louder; an
  in-phase bass is left alone; with Auto polarity off it only reports.
- Output identical for block sizes 1 to 4096 with a sidechain.
- Masking mode (synthetic): a 62 Hz component 30+ dB over a 62 Hz kick is ducked 6.3 dB (Simple: 0 dB); a faint component
  that does not cover a loud kick is left alone (-0.2 dB; Simple ducks it 5.8 dB); a kick only a few dB under the bass is not
  masked, so nothing happens (-0.6 dB). The note's own fundamental moves -1.4 dB in the first case. Bit-exact at Kick 0 %,
  identical across block sizes. To keep the note safe, harmonics 1-5 are protected within about 8 Hz and fade out over the next 7 Hz
  (the window cannot resolve finer than about 14 Hz), which also trims the Simple-mode duck (mud under a hit: -5.7 dB, was -8.7 dB).
