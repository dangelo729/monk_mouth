# Monk Mouth

A singing voice for the Teensy Audio Library that you play by moving a mouth.
You move a tongue, lips and lungs, and the sound is whatever comes out of the
tube they shape. It can hold a note, sing words, talk and whistle.

Hear it at https://dangelo.engineering (drag the slider on Monk Mouth there).

## Install

- Arduino IDE: download this repository as a ZIP, then Sketch > Include Library > Add .ZIP Library.
- PlatformIO: `lib_deps = https://github.com/dangelo729/monk_mouth.git`

It needs a Teensy 4.x and the Teensy Audio Library, part of Teensyduino and of PlatformIO's Teensy platform.

`#include <MonkMouth.h>` gives the class `AudioSynthMonkMouth` and the namespace `mm`.

## Use

```cpp
#include <Audio.h>
#include <MonkMouth.h>

AudioSynthMonkMouth  voice;
AudioOutputI2S       i2s;
AudioConnection      left(voice, 0, i2s, 0), right(voice, 0, i2s, 1);
AudioControlSGTL5000 codec;

const uint8_t kVowels[] = {mm::ph::AA, mm::ph::IY, mm::ph::UW};

void setup() {
    AudioMemory(8);
    codec.enable();
    codec.volume(0.5f);
    voice.setVibrato(5.5f, 25.0f, 0.4f);    // rate in Hz, depth in cents, onset in s
    voice.setVowel(kVowels[0]);
    voice.noteOn(45, 0.8f);                 // A2, at breath 0.8
}

void loop() {
    static int at = 0;
    delay(3000);
    at = (at + 1) % 3;
    voice.setVowel(kVowels[at]);            // the tongue and lips ease there
}
```

The main calls:

- `noteOn(midi, breath)`, `noteOff()`: start and stop a note.
- `setTongue(pos, height)`, `setLips`, `setBreath`, `setPitch`, `setGlottis`, `setVelum`:
  move one part, 0 to 1 (pitch in MIDI notes). Each eases there at its own rate.
- `setVowel(mm::ph::AA)`: the tongue and lips for a vowel.
- `syllable("B AA")`: a syllable now, on the note that is sounding.
- `sing(line, n)`: words on notes, each `{"HH AH", 48, 300}` (phones, note, ms); `busy()` while it plays.
- `speak("HH AH0 L OW1")`: words spoken, timed and pitched by their stress.
- `whistle(midi, breath)`: whistle a note.
- `setTuning(t)`: who is singing, every tunable in one `mm::Tuning`.
- `setSections(n)`: the size. 22 is an adult male tract (17.5 cm); fewer is shorter.

Each call lands at the start of the next 128-sample block. For more voices, mix
several, each with its own seed: `AudioSynthMonkMouth voice2(22, 2);`.

## Examples

- `01_Drone`: a held note moving through vowels (the sketch above).
- `02_Puppet`: play the mouth by hand from the serial monitor.
- `03_SingWords`: sing a song a line at a time.
- `04_Whistle`: whistle a tune.
- `99_Benchmark`: four voices at their heaviest, with the CPU load printed.

## On a computer

Everything but the Teensy wrapper builds with clang alone. `make test` runs
the tests; `make tool` builds `extras/host/build/mmtool`, which renders WAVs
and measures the voice: `mmtool say "HH AH0 L OW1" -o hello.wav`.

## More

- [The whole API](extras/docs/api.md)
- [How it works](extras/docs/how-it-works.md)
- [Tuning](extras/docs/tuning.md)
- [Whistling](extras/docs/whistling.md)
- [Known limits](extras/docs/limits.md)
- [The host rig and mmtool](extras/docs/host.md)

## Credits and license

The tube and the glottis are a float32 C++ port of
[Pink Trombone](https://dood.al/pinktrombone/) by Neil Thapen (2017, MIT),
kept verbatim at `extras/reference/pinktrombone.html`. Vowel targets are from
Peterson & Barney (1952). `NOTICE` names every source. MIT, see `LICENSE`.
