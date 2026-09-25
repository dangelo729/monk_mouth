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
- **Host-first.** Everything but one wrapper file compiles on a Mac with clang
  and nothing else, renders WAVs, and passes tests.

`wind_pipe` is a working name (library `WindPipe`, namespace `wp`, class
`AudioSynthWindPipe`); renaming is a search and replace.

## Status

| | |
|---|---|
| engine, body, vocabulary, words, tunables | done; 62 host tests green (`make test`) |
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
// words
bool syllable(const char* phones);      // now, at the current note
bool sing(const Syllable* syls, int n); // {phones, midi, ms}
bool sing(const Syllable* syls, int n, float breath);   // at this breath; the voice's own level stays
bool say(const char* phones);           // "HH AH0 L OW1 / W ER1 L D"
void stop();   bool busy() const;
// readback
BodyState state() const;                // eased pose, f0, voicing, hiss, closed, phoneme
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
make test        # 62 tests: tube, glottis, body, voice, vocabulary, score, tuning
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
   -> 128 x { glottis + aspiration -> tube (+nose) + turbulence + bursts }
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
| `src/core/wp_body.{h,cpp}` | anatomy; dynamics, releases, pressure |
| `src/core/wp_pitch.{h,cpp}` | glide, vibrato, wobble |
| `src/core/wp_vocab.{h,cpp}` | the 39 phonemes: vowel poses, consonant table |
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
- Not a self-oscillating fold model, not a 2D/3D tract.

## Credits

The tube and the glottis are a float32 C++ port of
[Pink Trombone](https://dood.al/pinktrombone/) by Neil Thapen (2017, MIT),
vendored verbatim at `extras/reference/pinktrombone.html`; see `NOTICE`. Vowel
targets are from Peterson & Barney (1952).

MIT — see `LICENSE`.
