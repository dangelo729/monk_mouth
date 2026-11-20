# How it works

The sound is air moving through a tube shaped like a vocal tract: a
Kelly-Lochbaum waveguide with a nose branch, driven by a Liljencrants-Fant
glottal pulse, both adapted from Pink Trombone. Nothing is told what a sound
is. A small body (lungs, larynx, tongue, lips, velum and two free
constrictions) moves toward its targets, and vowels, glides, nasals, stops,
bursts and hiss come out of where the tube is wide, narrow, closed or opening.
Words are a timed list of the same targets a hand would set.

One block, top to bottom:

```
tuning, when one was set               -> every module's rates, gains, losses
size, when one was set                 -> the tube's layout under the same pose
targets (live setters | due score events)
   -> pitch axis: glide, vibrato, wobble            -> f0 for this block
   -> body: every channel approaches its target     -> eased pose
   -> anatomy: pose -> diameters + velum            -> areas -> reflections
   -> watchers: closures, releases, pressure        -> burst + noise gains
   -> airflow: folds, tract, lips in series         -> the jet's flow + transit
   -> 128 x { glottis + aspiration -> tube (+nose) + turbulence + bursts
              + the jet at the lips, fed by the lips' own flow }
   -> halfband lowpass -> gain -> clamp
   -> state published for readback
```

One smoothing law moves everything: `x += (target - x) * (1 - exp(-rate * dt))`.
Closures (and a breath of 0) aim below zero and clamp, so they arrive in finite
time. The per-sample path calls no libm. One seeded xorshift32 per voice: the
same calls produce the same samples, bit for bit, at any render buffer size.
No heap.

| | |
|---|---|
| `src/MonkMouth.h` | the header to include |
| `src/AudioSynthMonkMouth.{h,cpp}` | the Teensy wrapper, the only platform file |
| `src/core/mm_config.h` | constants, clamp, approach, xorshift32: what is fixed |
| `src/core/mm_tuning.{h,cpp}` | `mm::Tuning`: every tunable and its default; the few limits |
| `src/core/mm_tube.{h,cpp}` | Kelly-Lochbaum tube and nose, halfband |
| `src/core/mm_glottis.{h,cpp}` | the LF pulse as recurrences, with its closed form |
| `src/core/mm_noise.{h,cpp}` | aspiration and frication bands, modulator, turbulence window |
| `src/core/mm_body.{h,cpp}` | anatomy; dynamics, releases, pressure; the airflow through the lips |
| `src/core/mm_jet.{h,cpp}` | the jet at the lips: transit line, saturation, whistle telemetry |
| `src/core/mm_pitch.{h,cpp}` | glide, vibrato, wobble |
| `src/core/mm_vocab.{h,cpp}` | the 39 phonemes: vowel poses, consonant table; the whistle table |
| `src/core/mm_score.{h,cpp}` | parser, compiler, player; word timing |
| `src/core/mm_voice.{h,cpp}` | `mm::Voice`, the whole engine |
| `extras/host/` | Makefile, tests, `mmtool`, WAV and PNG writers, `corpus/` |
| `extras/reference/pinktrombone.html` | Pink Trombone, verbatim, with its license |

`src/core/` includes nothing but the C++ standard library.

On the Teensy, `AudioSynthMonkMouth` owns one `mm::Voice`. Each setter writes
the voice's pending targets inside a short interrupt mask; `update()` masks
interrupts only to copy them to the audio side, then renders unmasked. The
word calls compile on the caller's side and hand the score over by a flag, so
the audio interrupt sees either no new score or a whole one. Flush-to-zero is
set for the audio interrupt at construction.
