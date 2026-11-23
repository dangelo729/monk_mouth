# The API

`AudioSynthMonkMouth` (the Teensy class) and `mm::Voice` (the engine, which
runs anywhere) have the same calls. On the Teensy every setter is safe to call
from the main loop while audio runs.

```cpp
AudioSynthMonkMouth voice(float sections = 22, uint32_t seed = 1);
mm::Voice           voice(float sections = 22, uint32_t seed = 1, float sampleRate = 44100);

// lungs and larynx
void setBreath(float p01);              // the loudness axis; 0 = silent, past 1 is only louder
void setPitch(float midi);
void setGlottis(float open01);          // 0 voiced .. 1 open (h, whisper, voiceless)
void setTenseness(float t01);           // breathy .. pressed
// tract
void setTongue(float pos01, float height01);   // pos 0 = back, 1 = front; height 1 = highest
void setLips(float aperture01);                // 0 closes
void setVowel(uint8_t phone);                  // tongue and lips for a vowel: mm::ph::AA .. mm::ph::UW
void setVelum(float open01);
void setConstriction(int slot, float place01, float degree);   // slot 0 or 1; degree <= 0 closes
void clearConstriction(int slot);
// character
void setGlide(float perSec);            // how fast the pitch moves to a new note; 0 = jump
void setVibrato(float rateHz, float cents, float onsetSec);
void setWobble(float amount01);         // slow drift and flutter in the pitch
// who is singing
void       setTuning(const mm::Tuning& t);     // every tunable at once (tuning.md)
mm::Tuning tuning() const;                     // the last one set, as held to its limits
void       setSections(float sections);        // the size, live, whole or not
float      sections() const;                   // the last size set
// conveniences
void noteOn(float midi, float vel01);   // pitch + breath + vibrato onset, together
void noteOff();                         // breath to 0
void whistle(float midi, float breath); // a whistle pose for this note, and breath (whistling.md)
void whistleRange(float& lo, float& hi) const;  // the notes whistle() reaches without folding
// words
bool syllable(const char* phones);      // now, on the note that is sounding
bool sing(const mm::Syllable* syls, int n);                 // {phones, midi, ms}
bool sing(const mm::Syllable* syls, int n, float breath);   // at this breath; the voice's own level stays
bool say(const char* phones);           // "HH AH0 L OW1 / W ER1 L D" on the current note
bool speak(const char* phones);         // spoken, not sung: "S T AA1 P / DH AE2 T"
bool speak(const char* phones, const mm::Speech& s);        // {midi, range, speed, breath}
void stop();                            // end the words and close everything down
bool busy() const;                      // words are playing
// readback, as of the end of the last block
mm::BodyState state() const;            // eased pose, f0, vibrato, voicing, hiss, closed, phoneme, whistle, whistleHz
int  diameters(float* out, int max) const;     // the tract's diameters, glottis to lips
void setHalfband(bool on);              // the output lowpass, on by default
// mm::Voice only
void render(float* out, int n);         // any number of samples, in [-1, 1]
// and latch(), setAutoLatch() for a platform wrapper; sampleRate(), body(), tube(), jet() for tools
```

## Targets

Every call sets a target, and the body moves each part toward its target at
that part's own rate. A target set between blocks lands at the top of the next
128-sample block: one latency, for everything. Live setters and a running
score write the same targets, and the last writer wins, per channel, in the
caller's own order: a setter called before `sing()` loses to the score's first
events, one called after wins, even if all of it lands in a single block.

A number that is not finite (a NaN, an infinity) is ignored: the call sets
nothing (a `Tuning` field takes its default), and `sing()` or `speak()`
returns `false`.

`setGlottis(0)` and `setVelum(0)` mean this voice's rest: shut, unless its
tuning rests them open (`glottisRest`, `velumRest`).

`setVowel(phone)` poses the tongue and lips as the vowel table has them, the
lips held as the voice holds a sung vowel's (the tuning's `lipRound`). A
diphthong poses its first half; a phone that is not a vowel sets nothing.

Places are fractions of tract length (glottis 0, lips 1): labial .93, dental
.88, alveolar .82, postalveolar .72, velar .50. Degrees are tube diameters:
`<= 0` closes (and bursts when it lets go, unless the nose is open), 0.03 to
0.4 hisses, above that it only colours the vowel.

On the Teensy, call the word calls from the main loop, not an interrupt: they
compile the words where they are called and use about 11 KB of stack.

## Words

Phones are ARPABET, space-separated, stress digits accepted. `"_"` is a rest,
`""` a melisma (only the pitch changes). In `say()`, consonants between vowels
join the next syllable except at a `/` word end. The last onset consonant
releases on the beat; the cluster before it borrows the previous note's tail;
the vowel's pose is sent when the onset starts, so the tongue is already in
place behind the closure.

`sing()`, `say()`, `speak()` and `syllable()` return `false` on a bad phoneme
or an overflow, and then have disturbed nothing. One score holds 256 events:
about 50 syllables like `"B AA"`, or a dozen like `"S T R EH NG TH S"`. A song
goes in a line at a time: `sing()`, wait for `busy()` to fall, `sing()` again.

```cpp
const mm::Syllable line[] = {{"HH AH", 48, 300}, {"L OW", 52, 600}, {"_", 0, 250}, {"W ER L D", 55, 1000}};
if (!voice.busy()) voice.sing(line, 4);
```

Out of silence (a line's start, or after a rest) the mouth is ready before the
air: the vowel's pose moves quickly, the breath comes once the first consonant
has arrived (a closure shut, a hiss's channel narrowed), and a voiceless first
consonant has the glottis open from the start, so nothing voices ahead of it;
a voiced stop starts voicing behind its closure. Into silence (the end, or a
rest) a voiceless consonant keeps the glottis open until the breath has gone,
so no voice comes back after its release. Before a vowel a stop voices again
`kVotMs` after it lets go. `syllable()` plays into a note that is already
sounding and keeps its own timing.

## Speaking

`speak()` takes phones as `say()` does and says them as a plain declarative
speaker would, where `say()` sings them on one note:

```cpp
mm::Speech s;                   // midi 0 (the held note), range 4, speed 1, breath 0 (the voice's level)
s.midi  = 50;                   // the baseline
s.range = 5;                    // semitones an accent lifts
voice.speak("S T AA1 P / DH AE2 T", s);
```

- Lengths from stress. A vowel with primary stress (`1`, or no digit) is
  120 ms, secondary (`2`) 90, unstressed (`0`) 55; a diphthong 1.4 times that,
  the line's last syllable 1.5 times. Consonants keep their table timings;
  `"_"` is a 200 ms pause. `speed` divides all of it (held to 0.25 to 4) and
  runs every consonant's gesture that much faster.
- A pitch that keeps moving. `midi` is the baseline (`<= 0`: the voice's held
  note, `setPitch`), falling 1.5 semitones across the line. Each primary
  syllable lifts toward baseline + `range` as its vowel starts and falls back;
  after the last accent the pitch falls to the baseline less `range` / 2 by
  the end of the last vowel. Every move glides at the line's own rates: the
  voice's `setGlide` is neither used nor changed.
- No vibrato while a line is spoken; the wobble stays. The voice's vibrato is
  not changed: the next `noteOn()`, `sing()` or `setPitch()` has it as it was
  set.
- Loudness from stress. `breath` is a stressed syllable's level (`<= 0`: the
  voice's own, as `say()` uses); secondary syllables take 0.85 of it,
  unstressed 0.7. The line ends by falling away.

`busy()` and `stop()` work on a spoken line as on any score. The numbers are
the `kSpeak*` constants in `src/core/mm_score.h`, beside the rest of the word
timing.

## Voice size

The size is in sections of 0.79 cm: 22 = 17.5 cm (adult male), 19 = 15 cm,
16 = 12.7 cm; human throats run about 16 to 26. Any size from 2 to 64 is
taken, whole or not, and neither end is acoustic: the nose junction joins two
sections, and the arrays have to be some size (`kMaxSections`; the cost is
linear in it). Poses mean the same thing at every size, and formants scale
with 1/N. The vowel orderings and the pose scaling are tested from 14 to 48,
where the analysis's 120 to 5000 Hz window still sees every formant.

A size between whole ones is laid out whole sections first (`Layout` in
`src/core/mm_tube.h`); what is left lives at the three open ends (the glottis,
the lips, the nostrils) as a piece of a section, a round trip read between
samples, as wide as the shape is there and joined to its end section by a
junction that grows in with the piece. The nose joins and runs at the
reference's ratios, sliding between one whole size's layout and the next. A
whole size lays out exactly as a tube built at it, and a piece that grows
whole is exactly the section that appears there, so every part of the tube
moves continuously with the size. A section at a region's edge (throat,
tongue, lips) is in both regions by its share, 22's regions drawn to scale, so
formants follow 1/N closely, not exactly.

`setSections` changes the size live, like a tuning: it lands at the top of
the next block. The pose stands and whatever is sounding carries on: a held
note, a word, a whistle. A size moved a little at a time glides; a jump lands
at once, the waves already in the pipe stretched over the new length.

A voice is 16,284 bytes (`sizeof(mm::Voice)`), most of it the two 256-event
score buffers. For more than one voice, use one `AudioSynthMonkMouth` per voice
into an `AudioMixer4`, each with its own seed.
