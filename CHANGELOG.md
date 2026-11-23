# Changelog

All notable changes to MonkMouth are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.1] - 2026-11-23

### Fixed

- A number too large for a float to carry (a breath past about 3e19, a tenseness past
  about 4e25, a tuning value thrown between two huge opposites) no longer silences a
  voice for good: `render()` never puts out a NaN, and the voice sings again once the
  number is taken back.
- `MonkMouth.h` builds on an Arduino core without the Teensy core's `AudioStream.h`,
  where it gives `mm::Voice` alone.
- `library.json` no longer leaves out a folder that is not there.

### Changed

- The docs say what breath is and where it clips, what `busy()` means, a word's limits
  (64 syllables, 6 consonants either side of a vowel), the setters' defaults, that
  flush-to-zero is on for the whole sketch, and that the voice is in tune near 44.1 kHz.
  Pink Trombone is named wherever the code adapts it.

## [0.1.0] - 2026-11-23

The first release.
