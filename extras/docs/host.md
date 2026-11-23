# The host rig

Everything but the Teensy wrapper compiles with clang and the C++ standard
library. The rig lives in `extras/host`; the root `Makefile` forwards to it, so
these work from either place:

```
make test        # every test in extras/host/tests: tube, glottis, body, voice, vocabulary, score, tuning, jet, whistle, umbrella
make tool        # extras/host/build/mmtool
make corpus      # every phoneme in three vowel contexts, and the sung lines in
                 # extras/host/corpus/, as WAVs in extras/host/build/corpus/
```

The `mmtool` lines below are run from `extras/host` (`./build/mmtool ...`).

```
mmtool vowel AA --midi 48 --sec 2 -o aa.wav      ("rest" = Pink Trombone's rest shape)
mmtool pose --tongue .3 .8 --lips .9 -o pose.wav
mmtool formants --tongue .3 .8 --lips .9         F1..F4 of a static shape (or: formants AA)
mmtool fit                                       grid-search every vowel pose; prints the table
mmtool syllable "B AA" -o ba.wav
mmtool sing corpus/twinkle.txt -o song.wav       one "phones midi ms" per line; "-" melisma, "_" rest
mmtool say "HH AH0 L OW1 / W ER1 L D" -o hello.wav
mmtool speak "S T AA1 P / DH AE2 T" --midi 50 --range 5 -o stop.wav    spoken; also --speed S --breath B
mmtool trace "AA S AA"                           10 ms frames: level, periodicity, centroid, body state
                                                 (--speak: the line through speak(), with its options)
mmtool throat [step]                             sweep the throat: the map of where the vowel gates hold
mmtool whistle --tongue .9 .6 --lips .3 -o w.wav a live whistle pose: lock, level, whistleHz against the lip modes
mmtool whistle 84 -o w.wav                       a note through whistle(): the same report
mmtool whistle fit                               grid-search the whistle table; prints the block
   any render:  --png spec.png                   a spectrogram (0 to 8 kHz, ticks every 100 ms)
   options:     --size N --seed S --midi M --breath B --tense T --no-halfband --no-wobble
                --tune NAME=VALUE                any field of mm::Tuning, repeatable (formants and fit take it too)
```

`formants` feeds a unit impulse through a static tube and peak-picks the FFT:
no source, no noise, no external tools. `fit` searches 7245 tubes with the
vowel orderings built into the search, and prints the block that is pasted
into `src/core/mm_vocab.cpp`. The fitted vowels are all within 5 % of
Peterson & Barney (adult male), and their orderings hold at every tested size,
14 to 48.

`trace` and `--png` show what the body did and what came out, frame by frame.
