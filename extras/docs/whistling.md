# Whistling

A whistle is the other source, at the other end of the same pipe. The air
through the folds, the tract and the lips is Bernoulli flow through three
orifices in series: voiced, the folds are the narrowest and the jet at the
lips is slow and inert; folds open and lips puckered, the pressure drops at
the lips and the jet is fast. The lips' own air, moving with the front
cavity's ring, nudges the jet; the nudge rides it for a transit and arrives as
more or less air, and when the transit matches the ring the loop locks. Pitch
is the cavity's, set by the tongue, the pucker and a slot behind it; loudness
is the jet's.

Two ways to whistle:

```cpp
// a pose, by hand
voice.setGlottis(1);  voice.setVelum(0);
voice.setTongue(0.9f, 0.6f);  voice.setLips(0.3f);
voice.setBreath(0.8f);                      // about 1250 Hz at N = 22

// a note, through the table
voice.whistle(84, 0.8f);                    // C6, at breath 0.8
voice.whistle(89, 0.8f);                    // the tongue eases: a slide
voice.noteOff();                            // ends it as it ends a note
```

`whistle(midi, breath)` is one call that writes targets like any other: pitch
(the asked note, read back as `midi`), folds open, velum shut, tongue, pucker,
slot 0 or none, slot 1 cleared, breath. The last writer still wins, per
channel. `whistle()` ends any words in flight: a `sing()` not yet started is
dropped, one playing is abandoned, and the phoneme readback clears.

The table (`src/core/mm_vocab.cpp`) has one pose per semitone from midi 77 to
95 (F5 to B6, 698 to 1976 Hz), each within 21 cents of its note at N = 22 and
breath 0.8; a fraction of a semitone interpolates the two neighbouring rows.

- The fold. A note outside the range folds into it by octaves, then clamps: a
  sung C3 (48) whistles C6 (84), as a whistler would. `whistleRange(lo, hi)`
  says where the table sits for this voice.
- The size shift. The table was fitted at N = 22. A voice of N sections looks
  its note up at `midi + 12 log2(N / 22)`: every mode scales with 1/N, so the
  pose that whistles that higher note at 22 whistles this one at N. The jet's
  transit does not scale (see [limits](limits.md)).
- Pitch rises with breath. A faster jet is a shorter transit. From breath 0.5
  to 1.0 a row rises 34 to 143 cents, most at the bottom of the range. Rows 77,
  94 and 95 do not lock at breath 0.5; every row locks at 0.8 and 1.0.
- The folds stay open. `whistle` opens them, and a bare `noteOn` after it does
  not close them: a whispered note is legal. A score commit does (it rests the
  glottis and clears both slots), and so do `stop()` and `setGlottis(0)`.
- Words and whistles are exclusive. A pucker has no room for consonants, and a
  slot at the lips is a slit, which breaks the jet: `whistle()` ends any words
  in flight, and a `sing()` after it voices again.

The table comes from `mmtool whistle fit`: a grid of 57960 poses (tongue 23 x
21, pucker lipD 0.2 to 0.9 in 11 steps, no slot or one at 0.75 to 0.90 x three
degrees), screened for a lip mode in the whistle band, rendered at breath 0.8
and measured the way `mmtool whistle` measures: a lock is periodicity above
0.95 and `whistle` above 0.5 after 300 ms, and the pitch is the measured
`whistleHz`, not the lip mode. Every lock is also rendered at breath 0.5 and
1.0 and at N = 16 and 26. A Viterbi pass picks one pose per semitone, trading
cents, the lock's distance from its own lip mode, a lock lost at 0.5 or 1.0
and the error at the other sizes against pose jumps and slot flips between
neighbours. It prints the block for `mm_vocab.cpp`, each row with its measured
Hz, cents and drift. Re-run it after touching the anatomy, the tube, the jet
or the whistle tunables.
