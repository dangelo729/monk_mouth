# wind_pipe

An articulatory singing voice for the Teensy Audio Library (Teensy 4.1).

The sound is made by simulating air moving through a tube shaped like a vocal
tract. You never tell it what a sound is. You move a mouth, and the sound is
the consequence.

- **A body, not a score player.** The whole control surface is pose targets on
  a small body — lungs, larynx, tongue, lips, velum, two free constrictions.
  Every part moves toward its target at its own rate. Vowels, glides, nasals,
  stops, bursts and hiss all fall out of where the tube is wide, narrow,
  closed, or opening.
- **Playable and scriptable through one mechanism.** Live play sets targets.
  Words are a timed list of the same target-sets. There is no second path.
- **Able to say words, loosely.** ARPABET in, sung on notes. Recognizable in a
  song you know is the bar, not dictation.
- **Able to whistle.** Open the folds, pucker, breathe: a jet at the lips locks
  onto the front cavity's ring. No switch; the pressure drop decides.
- **Host-first.** Everything but one wrapper file compiles on a Mac with clang
  and nothing else, renders WAVs, and passes tests.

`wind_pipe` is a working name (library `WindPipe`, namespace `wp`, class
`AudioSynthWindPipe`); renaming is a search and replace.

## Status

| | |
|---|---|
| engine, body, vocabulary, words, tunables, whistle | done; 75 host tests green (`make test`) |
| Teensy wrapper + 4 examples | written; **compile clean for Teensy 4.1** (PlatformIO, `-Wall -Wextra`) |
| hardware go / no-go (`99_Benchmark`: 4 voices ≤ 25 % CPU) | **not yet run** — needs a board |
| by-ear passes | **not yet done** — levels were set by measurement against speech norms (see *Tuning*) |

Footprint of the 4-voice benchmark: 39 KB flash, 64 KB RAM1 (≈ 13.5 KB per
voice, most of it the two 256-event score buffers).

## On a Teensy

```cpp
#include <Audio.h>
#include <WindPipe.h>

AudioSynthWindPipe   voice;                 // (sections = 22, seed = 1)
AudioOutputI2S       i2s;
AudioConnection      c1(voice, 0, i2s, 0), c2(voice, 0, i2s, 1);
AudioControlSGTL5000 codec;

void setup() {
    AudioMemory(8);
    codec.enable(); codec.volume(0.5f);
    voice.setTongue(0.1f, 0.75f);           // pos 0 = back .. 1 = front, height 1 = highest
    voice.noteOn(48, 0.8f);                 // "ah"
}

void loop() {
    static const wp::Syllable line[] = {
        {"HH AH", 48, 300}, {"L OW", 52, 600}, {"_", 0, 250}, {"W ER L D", 55, 1000}};
    if (!voice.busy()) { voice.sing(line, 4); delay(4000); }
}
```

Polyphony is N instances into a stock `AudioMixer4`; give each voice its own
seed. Examples: `01_Drone` (a held vowel), `02_Puppet` (serial commands →
targets: play the body by hand), `03_SingWords` (`sing()` a line),
`99_Benchmark` (the CPU gate).

## The whole public surface

```cpp
// lungs + larynx
void setBreath(float p01);              // the loudness axis; 0 = silent, past 1 is only louder
void setPitch(float midi);
void setGlottis(float open01);          // 0 voiced .. 1 open (h, whisper, voiceless)
void setTenseness(float t01);           // breathy .. pressed
// tract
void setTongue(float pos01, float height01);
void setLips(float aperture01);         // 0 closes
void setVelum(float open01);
void setConstriction(int slot, float place01, float degree);   // degree <= 0 closes
void clearConstriction(int slot);
// character
void setGlide(float perSec);            // 0 = jump
void setVibrato(float rateHz, float cents, float onsetSec);
void setWobble(float amount01);
// who is singing: every tunable at once (see Tuning)
void   setTuning(const wp::Tuning& t);
Tuning tuning() const;                  // the last one asked for, as held
// conveniences
void noteOn(float midi, float vel01);   // pitch + breath + vibrato onset
void noteOff();                         // breath to 0
void whistle(float midi, float breath); // a whistle pose for this note from the table, and breath; ends any words in flight
void whistleRange(float& lo, float& hi) const;   // the notes whistle() reaches without folding (wp::Voice only)
// words
bool syllable(const char* phones);      // now, at the current note
bool sing(const Syllable* syls, int n); // {phones, midi, ms}
bool sing(const Syllable* syls, int n, float breath);   // at this breath; the voice's own level stays
bool say(const char* phones);           // "HH AH0 L OW1 / W ER1 L D"
void stop();   bool busy() const;
// readback
BodyState state() const;                // eased pose, f0, voicing, hiss, closed, phoneme, whistle, whistleHz
int  diameters(float* out, int max) const;
// the engine (wp::Voice only)
void render(float* out, int n);
```

`setGlottis(0)` and `setVelum(0)` mean *this voice's rest* — shut, unless its
tuning rests them open (`glottisRest`, `velumRest`).

Every call sets a target; it lands at the top of the next 128-sample block —
one latency, for everything. Live setters and a running score write the same
targets, and **the last writer wins, per channel**, in the caller's own order:
a setter called before `sing()` loses to the score's first events, one called
after wins, even if all of it lands in a single block.

Places are fractions of tract length (glottis 0, lips 1): labial .93, dental
.88, alveolar .82, postalveolar .72, velar .50. Degrees are tube diameters:
`<= 0` closes (and will burst when it lets go), `0.03 – 0.4` hisses, above
that it only colours the vowel.

**Words.** Phones are ARPABET, space-separated, stress digits accepted. `"_"`
is a rest, `""` a melisma (only the pitch changes). In `say()`, consonants
between vowels join the next syllable except at a `/` word end. The last onset
consonant *releases on the beat*; the cluster before it borrows the previous
note's tail; the vowel's pose is sent when the onset starts, so the tongue is
already in place behind the closure. `sing()`, `say()` and `syllable()` return
`false` on a bad phoneme or an overflow and then have disturbed nothing. One
score holds 256 events (a CV syllable costs 5, a heavy one ~15), so a song
goes in a line at a time: `sing()`, wait for `busy()` to fall, `sing()` again.
Call them from the main loop, not an interrupt (≈ 10 KB of stack).

**Voice size** is a section count given at construction: 22 = 17.5 cm (adult
male), 19 ≈ 15 cm, 16 ≈ 12.7 cm; human throats run 16 to 26. Any size from 2
to 64 is taken, and neither end is acoustic — the nose junction joins two
sections, and the arrays have to be some size (`kMaxSections`; cost is linear
in it). Poses mean the same thing at every size; formants scale with 1/N. The
vowel orderings and the pose scaling are tested from 14 to 48, where the
analysis's 120 – 5000 Hz window still sees every formant.

## The host rig

The rig lives in `extras/host`; the root `Makefile` forwards to it, so these
work from either place.

```
make test        # 75 tests: tube, glottis, body, voice, vocabulary, score, tuning, jet, whistle
make tool        # extras/host/build/wptool
make corpus      # every phoneme in three vowel contexts + the sung lines in
                 # extras/host/corpus/ -> extras/host/build/corpus/*.wav
```

The `wptool` lines below are run from `extras/host` (`./build/wptool ...`).

```
wptool vowel AA --midi 48 --sec 2 -o aa.wav      ("rest" = the reference's rest shape)
wptool pose --tongue .3 .8 --lips .9 -o pose.wav
wptool formants --tongue .3 .8 --lips .9         F1..F4 of a static shape (or: formants AA)
wptool fit                                       grid-search every vowel pose; prints the table
wptool syllable "B AA" -o ba.wav
wptool sing corpus/twinkle.txt -o song.wav       one "phones midi ms" per line; "-" melisma, "_" rest
wptool say "HH AH0 L OW1 / W ER1 L D" -o hello.wav
wptool trace "AA S AA"                           10 ms frames: level, periodicity, centroid, body state
wptool throat [step]                             sweep the throat: the map of where the vowel gates hold
wptool whistle --tongue .9 .6 --lips .3 -o w.wav a live whistle pose: lock, level, whistleHz against the lip modes
wptool whistle 84 -o w.wav                       a note through whistle(): the same report
wptool whistle fit                               grid-search the whistle table; prints the block (see Whistling)
   any render:  --png spec.png                   a spectrogram (0–8 kHz, ticks every 100 ms)
   options:     --size N --seed S --midi M --breath B --tense T --no-halfband --no-wobble
                --tune NAME=VALUE                any field of wp::Tuning, repeatable (formants and fit take it too)
```

`formants` feeds a unit impulse through a static tube and peak-picks the FFT:
no source, no noise, no external tools. `fit` searches 7245 tubes in about a
second, with the vowel orderings built into the search, and prints the block
that is pasted into `src/core/wp_vocab.cpp`. Fitted residuals are all within
5 % of Peterson & Barney (adult male); the orderings hold at every voice size.

`trace` and `--png` are the rig's eyes: what the body did and what came out,
for when ears are not to hand.

## How it works

One block, top to bottom — this is all the engine does:

```
tuning, when one was set               -> every module's rates, gains, losses
targets (live setters | due score events)
   -> pitch axis: glide, vibrato, wobble            -> f0 for this block
   -> body: every channel approaches its target     -> eased pose
   -> anatomy: pose -> 22 diameters + velum         -> areas -> reflections
   -> watchers: closures, releases, pressure        -> burst + noise gains
   -> airflow: folds, tract, lips in series         -> the jet's flow + transit
   -> 128 x { glottis + aspiration -> tube (+nose) + turbulence + bursts
              + the jet at the lips, fed by the lips' own flow }
   -> halfband lowpass -> gain -> clamp
   -> state published for readback
```

| | |
|---|---|
| `src/WindPipe.h` | umbrella |
| `src/AudioSynthWindPipe.{h,cpp}` | the Teensy wrapper, the only platform file |
| `src/core/wp_config.h` | constants, clamp, approach, xorshift32 — what is fixed |
| `src/core/wp_tuning.{h,cpp}` | `wp::Tuning`: **every tunable** and its default; the few limits |
| `src/core/wp_tube.{h,cpp}` | Kelly-Lochbaum tube + nose, halfband |
| `src/core/wp_glottis.{h,cpp}` | LF pulse as recurrences, closed form as oracle |
| `src/core/wp_noise.{h,cpp}` | aspiration + frication bands, modulator, turbulence window |
| `src/core/wp_body.{h,cpp}` | anatomy; dynamics, releases, pressure; the airflow through the lips |
| `src/core/wp_jet.{h,cpp}` | the jet at the lips: transit line, saturation, whistle telemetry |
| `src/core/wp_pitch.{h,cpp}` | glide, vibrato, wobble |
| `src/core/wp_vocab.{h,cpp}` | the 39 phonemes: vowel poses, consonant table; the whistle table |
| `src/core/wp_score.{h,cpp}` | parser, compiler, player; timing constants |
| `src/core/wp_voice.{h,cpp}` | the public surface |
| `extras/host/` | Makefile, tests, `wptool`, WAV/PNG writers, `corpus/` |
| `extras/reference/pinktrombone.html` | the port bible, verbatim, with its license |

`src/core/` includes nothing but the C++ standard library. One smoothing law
moves everything: `x += (target - x) * (1 - exp(-rate * dt))`. Closures (and a
breath of 0) aim below zero and clamp, so they arrive in finite time. The
per-sample path calls no libm. One seeded xorshift32 per voice: the same calls
produce the same samples, bit for bit, at any render buffer size. No heap.

## Tuning

**A tunable changes who is singing, not what is being sung.** Every number
that passes that rule is a field of `wp::Tuning` (`src/core/wp_tuning.h`),
defaulted; a voice is handed one whole:

```cpp
wp::Tuning t;                   // the defaults: the voice nobody tuned, bit for bit
t.glottisRest   = 0.3f;         // a breathy singer
t.lipReflection = -0.93f;       // ringing formants
t.diction       = 1.5f;         // crisp consonants
voice.setTuning(t);             // lands whole at the top of the next block
```

| | fields |
|---|---|
| larynx | `glottisRest` `aspirationGain` `aspirationHz` `aspirationQ` `noiseVoicedFloor` `noiseVoicedPulse` `noiseUnvoiced` |
| pipe | `glottalReflection` `lipReflection` `damp` `throatD` `throatUpperD` `velumRest` |
| consonants | `turbulenceGain` `fricationHz` `fricationQ` `voicedFrication` `burstGain` `burstHalfLife` `diction` |
| motion | `rateBreathUp` `rateBreathDown` `rateGlottis` `rateTenseness` `rateTongue` `rateLips` `rateVelum` |
| pitch drift | `vibFadeRate` `wobbleSlowHz` `wobbleFastHz` `wobbleSlowCents` `wobbleFastCents` |
| lips: the whistle | `breathPressure` (Pa at breath 1, default 1000) `jetLength` (cm, the pucker's depth, 0.35) `jetGain` (**ms**, 0.1) |

**Almost nothing is limited.** A value that sounds broken is yours to find: a
breath of 3, a throat of 0.2, a rate of a million, a velum resting wide open
(which, by the body's own rule for a nasal voice, stops every burst and hiss)
all run. `setTuning` (`clamped()`) holds only the fields where a value past
the line does not sound bad but ends the voice — a full-scale blast, then NaN
until the voice is rebuilt:

| field | held to | past it |
|---|---|---|
| `aspirationHz` `fricationHz` | 0 … fs/2 | the band is an unstable filter |
| `aspirationQ` `fricationQ` | ≥ 1e-6 | its coefficients divide by zero |
| `glottalReflection` `lipReflection` `damp` | −1 … 1, either sign | the pipe gains energy every round trip |
| `burstHalfLife` | ≥ 0 | a click grows instead of dying |
| `breathPressure` | ≥ 0 | the jet's speed is the root of a negative pressure |

A NaN in any field falls back to its default, and `setTenseness` floors at 0
(the voicing gain is a root of it). A rate of 0 or less is a jump. Even with
all of these at their limits at once the voice is loud, finite, and sings
again when handed the defaults. Three fields are behaviours of their own: `glottisRest` and `velumRest` make "0"
mean this voice's rest, so a breathy or nasal singer stays one through every
score; `diction` scales every consonant's close and release rate, and the
score compiler's leads follow, so a crisper gesture still lands on the beat.
The throat moves formants and the vowel table was fitted against one throat,
so `wptool throat` is the guide: it maps which throats keep both vowel gates
at every human size. It is a map, not a limit.

What the rule leaves fixed stays in three places: `wp_config.h` (the engine's
shape, the anatomy, the guards), `wp_vocab.cpp` (poses, consonants),
`wp_score.h` (word timing). Re-run `wptool fit` after touching the anatomy or
the tube. First levers, by symptom:

- *lispy sibilants* — raise `fricationHz`; move S/SH places apart.
- *hiss / aspiration too loud or soft* — `turbulenceGain`, `aspirationGain`.
- *mushy consonants* — `diction`; table `holdMs`.
- *sluggish vowels* — `rateTongue`.
- *no voice bar in B D G* — raise `kVelumShut` (it is what leaks).
- *a whistle buzzy or bright* — lower `jetGain`; *will not start* — raise it or
  `breathPressure`; *locks on the wrong mode, or only at some pitches* —
  `jetLength`; *too much hiss under the tone* — `turbulenceGain`. Re-run
  `wptool whistle fit` after any of them.

## Whistling

A whistle is the other source, at the other end of the same pipe. The air
through the folds, the tract and the lips is Bernoulli through three orifices
in series: voiced, the folds are the narrowest and the jet at the lips is slow
and inert; folds open and lips puckered, the pressure drops at the lips and the
jet is fast. The lips' own air, moving with the front cavity's ring, nudges the
jet; the nudge rides it for a transit and arrives as more or less air, and when
the transit matches the ring the loop locks. Pitch is the cavity's, set by the
tongue, the pucker and a slot behind it; loudness is the jet's.

Two ways to whistle:

```cpp
// a pose, by hand
voice.setGlottis(1);  voice.setVelum(0);
voice.setTongue(0.9f, 0.6f);  voice.setLips(0.3f);
voice.setBreath(0.8f);                      // ~1250 Hz at N = 22

// a note, through the table
voice.whistle(84, 0.8f);                    // C6, at breath 0.8
voice.whistle(89, 0.8f);                    // the tongue eases: a slide
voice.noteOff();                            // ends it as it ends a note
```

`whistle(midi, breath)` is one call that writes targets like any other — pitch
(the asked note, read back as `midi`), folds open, velum shut, tongue, pucker,
slot 0 or none, slot 1 cleared, breath. The last writer still wins, per
channel. `whistle()` ends any words in flight: a `sing()` not yet started is
dropped, one playing is abandoned, and the phoneme readback clears. The table (`wp_vocab.cpp`) has one pose
per semitone from midi 77 to 95 (F5 to B6, 698 to 1976 Hz), each within 21
cents at N = 22 and breath 0.8; a fraction of a semitone interpolates the two
neighbouring rows.

- **The fold.** A note outside the range folds into it by octaves, then
  clamps: a sung C3 (48) whistles C6 (84), as a whistler would.
  `whistleRange(lo, hi)` says where the table sits for this voice.
- **The size shift.** The table was fitted at N = 22. A voice of N sections
  looks its note up at `midi + 12 log2(N / 22)`: every mode scales with 1/N, so
  the pose that whistles that higher note at 22 whistles this one at N. The
  jet's transit does not scale (see *Known limits*).
- **Pitch rises with breath.** A faster jet is a shorter transit. From breath
  0.5 to 1.0 a row rises 34 to 144 cents, most at the bottom of the range. Rows
  77, 94 and 95 do not lock at breath 0.5; everything locks at 0.8.
- **The folds stay open.** `whistle` opens them, and a bare `noteOn` after it
  does not close them: a whispered note is legal. A score commit does (it rests
  the glottis and clears both slots), and so do `stop()` and `setGlottis(0)`.
- **Words and whistles are exclusive.** A pucker has no room for consonants,
  and a slot at the lips is a slit, which breaks the jet: `whistle()` ends any
  words in flight, and a `sing()` after it voices again.

The table comes from `wptool whistle fit` (about 10 s on a laptop): a grid of
57960 poses (tongue 23 × 21, pucker lipD 0.2 to 0.9 in 11 steps, no slot or
one at 0.75 to 0.90 × three degrees), screened for a lip mode in the whistle
band, rendered at breath 0.8 and measured the way `wptool whistle` measures:
a lock is periodicity above 0.95 and `whistle` above 0.5 after 300 ms, and
the pitch is the measured `whistleHz`, not the lip mode. Every lock is also
rendered at breath 0.5 and 1.0 and at N = 16 and 26. A Viterbi pass picks one
pose per semitone, trading cents, the lock's distance from its own lip mode, a
lock lost at 0.5 or 1.0 and the error at the other sizes against pose jumps
and slot flips between neighbours. It prints the block for `wp_vocab.cpp`,
each row with its measured Hz, cents and drift. Re-run it after touching the
anatomy, the tube, the jet or the whistle tunables.

## Where this departs from the plan, and why

Each of these is commented at the site.

1. **LF recurrence form.** The plan's direct form `x[n+1] = c1 x[n] - c2 x[n-1]`
   loses 2e-3 in float32 at 60 Hz (poles next to z = 1), missing the plan's own
   1e-4 tolerance. The open phase runs as a coupled (rotation) recurrence in
   increment form instead: 7e-7, same cost class, still reseeded every period.
2. **LF DC.** Pink Trombone's `setupWaveform` uses two area approximations, so
   its period carries a 3–9 % DC residual. The port is line for line; the exact
   per-period mean (a closed form, one extra `cos` per period) is subtracted.
3. **Reflection clamp 0.999999, not 0.999.** 0.999 is a slit of diameter 0.034
   and passes the voice's low harmonics at −24 dB — the closure tests could not
   pass. The area floor already keeps the junction's division safe.
4. **Narrow sections lose more.** In area-weighted waves, whatever was inside a
   section when it shut stays trapped and leaks out as a 50 ms thump. Sections
   narrower than d = 0.1 get extra damping (to 0.98 when shut); same per-sample
   cost. Closures now reach their floor in ~10 ms.
5. **Shut velum 0.006, not 0.01.** Through 0.01 a voiced G leaked to within
   38 dB of its vowel, short of the 40 dB gate.
6. **Turbulence needs somewhere for the air to go.** A source hisses only while
   the other two are open and the air is not leaving through the nose — the
   same rule that keeps nasals from bursting. Without it a held S kept hissing
   behind a T closure, and nasal releases had an "s" blip.
7. **Noise gains 0.8 / 1.5, not the reference's 0.2 / 0.66.** Measured against
   an open vowel the reference puts /s/ 19 dB down and /h/ 30 dB down; speech
   has them near 12 and 18. Set by `wptool trace`, not yet by ear. (They are
   `Tuning`'s defaults.)
8. **Three-port areas.** The reference reads the junction's left/right areas
   one section off (`A[noseStart]`, `A[noseStart+1]` for a junction between
   `noseStart-1` and `noseStart`); here it uses the sections it actually joins,
   so a shut velum collapses exactly to the plain two-port.
9. **The fit enforces the orderings.** The plan's own targets put UW's F2 (870)
   above AO's (840), against its hard ordering gate; the fit splits them
   (833 / 876, both within 5 %).
10. **"Last writer wins" is exact**, via a caller-side sequence number (it was
    the one place a race on the Teensy could change what you hear).

The whistle departs from its own plan (`wind_pipe-whistle.md`) here, each
measured:

11. **The jet is nudged by the lips' air velocity, not its displacement**
    (whistle plan §1, §3): the displacement's 1/f tilt locked every pose at
    80 to 200 Hz, far under its lip mode.
12. **The drive enters as a pressure difference across the last section**
    (`Tube::injectFlow`, a dipole), not a volume source (§3): the open end
    shorts a volume source and it barely rang the front cavity.
13. **The drive opposes the nudge** (§3): it locks on the lip mode at three
    times as many poses. The sign is folded into `Jet::setGain`.
14. **The nudge is lowpassed at 3 kHz** (`kJetLowpassHz`, §3): without it the
    lip channel's own 7 to 9 kHz mode locked first, and F and TH whistled.
15. **Pulsing folds carry no jet** (§2): `jetFlow` is gated by the voicing
    gate's complement; without it a sung UW whistled 17 dB under its vowel.
16. **The slit rule** (§2): a slot cut deeper than the pucker inside the lip
    sections breaks the jet, so F, TH and S never whistle; a slot behind the
    lips shortens the front cavity.
17. **`jetGain` is in milliseconds** (default 0.1), not a dimensionless ~20
    (§5): the gain on a velocity is a time.
18. **`whistle` is weighted by how well the period holds** (§4): a swing forced
    by hiss or a voice is not a whistle; without the weight a sung line read
    up to 0.5.
19. **The fit costs robustness, not only cents** (§7.2): a lock lost at breath
    0.5 or 1.0 and the error at N = 16 and 26 join the cost. Without them most
    rows sat at their threshold, locked only near 0.8 and failed at N = 16.

## Known limits

- **Sibilants are approximations.** S sits near 6 kHz and SH near 2 kHz, which
  keeps them apart, but a 1D tube has no teeth: expect a slight lisp.
- **L and R are approximations** — a constriction above the hiss window (R with
  narrowed lips). No lateral channel, no retroflexion, no low F3.
- **TH/DH** are a weak hiss near the teeth and are easily heard as F/V.
- **Voiced stops have almost no voice bar**: the tube really closes.
- **The voice is dark.** The LF pulse at the default tenseness has a steep
  tilt; raise `setTenseness` for a brighter, more pressed sound.
- **The LF pulse aliases at high pitch.** Fine to ~500 Hz; above that the
  known fix (2× oversampling of the source alone) is not implemented.
- **Words are loose.** No text→phoneme, General American only, no stress
  handling beyond accepting the digits, no per-syllable dynamics. Live
  `syllable()` cannot anticipate: the vowel lands after the onset.
- **Voice size is quantized** in steps of ~4.5 % (one section); nothing between.
- **One score is 256 events** — a line at a time.
- **The tongue body cannot close or hiss**; only the two slots and the lips can.
- **A 1D tube has no teeth.** The slit rule stands in: a consonant slot at
  the lips never whistles, a pucker does.
- **The jet's transit does not scale with size.** It is physical (a length
  over a speed), while every mode scales with 1/N. At N = 16 and 26 the table's
  rows land within 136 cents of their note rather than 21, and the jet's reach
  in hertz stays put: at 16 the top two rows (past 2.5 kHz) do not lock or lock
  on another mode, at 26 the bottom row (under 600 Hz) does not lock. The pose
  whistle's lock threshold moves too: breath 0.3 at 22, 0.5 at 16, 0.2 at 26.
- **The whistle's pitch follows breath**, up to 1.4 semitones from 0.5 to 1.0
  at the bottom of the range. No ear corrects it (whistle plan §11).
- **The whistle range is 18 semitones**, F5 to B6 at N = 22: nothing in the
  pose grid locks above about 2 kHz or below about 700 Hz on the front cavity.
- Not a self-oscillating fold model, not a 2D/3D tract.

## Credits

The tube and the glottis are a float32 C++ port of
[Pink Trombone](https://dood.al/pinktrombone/) by Neil Thapen (2017, MIT),
vendored verbatim at `extras/reference/pinktrombone.html`; see `NOTICE`. Vowel
targets are from Peterson & Barney (1952). The whistle's jet drive follows
Fletcher and Rossing, *The Physics of Musical Instruments* (the flue jet, its
0.4 convection, the tanh saturation); McIntyre, Schumacher and Woodhouse
(1983), *On the oscillations of musical instruments* (a resonator coupled to a
delayed nonlinearity); and Cook's STK `Flute` (the digital form and its loop
filter). The front cavity as the whistle's resonator is from Shadle (1983),
*Experiments on the acoustics of whistling*, and Wilson et al. (1971),
*Experiments on the fluid mechanics of whistling*.

MIT — see `LICENSE`.
