# Tuning

A tunable changes who is singing, not what is being sung. Every number that
passes that rule is a field of `mm::Tuning` (`src/core/mm_tuning.h`), with a
default; a voice is handed one whole:

```cpp
mm::Tuning t;                   // the defaults: the voice nobody tuned, bit for bit
t.glottisRest   = 0.3f;         // a breathy singer
t.lipReflection = -0.93f;       // ringing formants
t.diction       = 1.5f;         // crisp consonants
voice.setTuning(t);             // lands whole at the top of the next block
```

| | fields |
|---|---|
| larynx | `glottisRest` `growl` `aspirationGain` `aspirationHz` `aspirationQ` `noiseVoicedFloor` `noiseVoicedPulse` `noiseUnvoiced` |
| pipe | `glottalReflection` `lipReflection` `damp` `throatD` `throatUpperD` `velumRest` `lipRound` `gain` |
| consonants | `turbulenceGain` `fricationHz` `fricationQ` `voicedFrication` `burstGain` `burstHalfLife` `diction` |
| motion | `rateBreathUp` `rateBreathDown` `rateGlottis` `rateTenseness` `rateTongue` `rateLips` `rateVelum` |
| pitch drift | `vibFadeRate` `wobbleSlowHz` `wobbleFastHz` `wobbleSlowCents` `wobbleFastCents` |
| lips: the whistle | `breathPressure` (Pa at breath 1, default 1000) `jetLength` (cm, the pucker's depth, 0.35) `jetGain` (ms, 0.1) |

Each field's comment in `mm_tuning.h` says what it does.

## Limits

Almost nothing is limited. A value that sounds broken is yours to find: a
breath of 3, a throat of 0.2, a rate of a million, a velum resting wide open
(which, by the body's own rule for a nasal voice, stops every burst and hiss)
all run. `setTuning` (`clamped()`) holds only the fields where a value past
the line does not sound bad but ends the voice, with a full-scale blast and
then NaN until the voice is rebuilt:

| field | held to | past it |
|---|---|---|
| `aspirationHz` `fricationHz` | 0 to fs/2 | the band is an unstable filter |
| `aspirationQ` `fricationQ` | at least 1e-6 | its coefficients divide by zero |
| `glottalReflection` `lipReflection` `damp` | -1 to 1, either sign | the pipe gains energy every round trip |
| `burstHalfLife` | at least 0 | a click grows instead of dying |
| `breathPressure` | at least 0 | the jet's speed is the root of a negative pressure |

A NaN in any field falls back to its default, and `setTenseness` floors at 0
(the voicing gain is a root of it). A rate of 0 or less is a jump. With all of
these at their limits at once the voice stays finite, and sings again when
handed the defaults.

## Fields with behaviours of their own

- `glottisRest` and `velumRest` make "0" mean this voice's rest, so a breathy
  or nasal singer stays one through every score.
- `lipRound` is how the singer holds the lips on every vowel: toward 1 each
  one pursed to UW's, toward -1 each one opened as wide as the lips go, a
  straight line on a log scale and on past either end. It reshapes the lips of
  a sung vowel and of `setVowel`; a consonant's own lips, a whistle's and
  `setLips` are left as asked. A retune re-holds the vowel in the mouth.
- `diction` scales every consonant's close and release rate, and the score
  compiler's leads follow, so a crisper gesture still lands on the beat.
- `growl` makes every other glottal pulse `1 - growl` as strong, so an octave
  under the note joins the voice, and at 1 the note itself drops an octave.
- `gain` is the pipe's output level, ahead of the output clamp, 1 as built; a
  new one ramps across the block it lands on, so a held note never steps.

The throat moves formants and the vowel table was fitted against one throat,
so `mmtool throat` is the guide: it maps which throats keep both vowel gates
at every human size. It is a map, not a limit.

## What stays fixed

What the rule leaves fixed stays in three places: `src/core/mm_config.h` (the
engine's shape, the anatomy, the guards), `src/core/mm_vocab.cpp` (poses,
consonants) and `src/core/mm_score.h` (word timing). Re-run `mmtool fit` after
touching the anatomy or the tube.

## First levers

- Lispy sibilants: raise `fricationHz`; move the S and SH places apart.
- Hiss or aspiration too loud or soft: `turbulenceGain`, `aspirationGain`.
- Mushy consonants: `diction`; the consonant table's `holdMs`.
- Sluggish vowels: `rateTongue`.
- No voice bar in B D G: raise `kVelumShut`, which is what leaks.
- A whistle buzzy or bright: lower `jetGain`. One that will not start: raise
  it, or `breathPressure`. One that locks on the wrong mode, or only at some
  pitches: `jetLength`. Too much hiss under the tone: `turbulenceGain`. Re-run
  `mmtool whistle fit` after any of them.
