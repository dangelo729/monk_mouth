// ----------------------------------------------------------------------------
// wind_pipe example 03 — SingWords
//
// sing() a line: words on notes. (Teensy 4.1 + the SGTL5000 audio shield.)
//
// a wp::Syllable is { phones, midi, ms }:
//   phones  ARPABET, space-separated; stress digits are accepted and ignored.
//           "_" is a rest, "" is a melisma (the vowel carries on, only the
//           pitch changes).
//   midi    the note.
//   ms      the time from this syllable's beat to the next one's.
//
// the words are compiled into a timed list of the very same targets example
// 02 sets by hand — tongue, lips, velum, two constrictions, glottis, breath —
// and played into the same table. the last consonant of each onset releases
// ON the beat; the consonants before it borrow time from the previous note.
//
// one score holds 256 events, roughly a dozen syllables, so a song goes to
// the voice a line at a time: sing() a line, wait for busy() to fall, sing
// the next. sing() returns false on a bad phoneme or an overflow, and then
// it has disturbed nothing.
// ----------------------------------------------------------------------------

#include <Audio.h>
#include <WindPipe.h>

AudioSynthWindPipe   voice;
AudioMixer4          mixer;
AudioOutputI2S       i2s;
AudioConnection      patchCord1(voice, 0, mixer, 0);
AudioConnection      patchCord2(mixer, 0, i2s, 0);
AudioConnection      patchCord3(mixer, 0, i2s, 1);
AudioControlSGTL5000 sgtl5000;

// notes, an octave down for a 17.5 cm tract
// (nC3, not C3: A3 and friends are the Arduino core's analog pins)
const float nC3 = 48, nD3 = 50, nE3 = 52, nF3 = 53, nG3 = 55, nA3 = 57;
const uint16_t Q = 450, H = 900;            // quarter and half, in ms

// "twinkle, twinkle, little star"
const wp::Syllable line1[] = {
    {"T W IH NG", nC3, Q}, {"K AH L", nC3, Q}, {"T W IH NG", nG3, Q}, {"K AH L", nG3, Q},
    {"L IH", nA3, Q}, {"T AH L", nA3, Q}, {"S T AA R", nG3, H}, {"_", 0, 250},
};
// "how I wonder what you are"
const wp::Syllable line2[] = {
    {"HH AW", nF3, Q}, {"AY", nF3, Q}, {"W AH N", nE3, Q}, {"D ER", nE3, Q},
    {"W AH T", nD3, Q}, {"Y UW", nD3, Q}, {"AA R", nC3, H}, {"_", 0, 250},
};
// "up above the world so high"
const wp::Syllable line3[] = {
    {"AH P", nG3, Q}, {"AH", nG3, Q}, {"B AH V", nF3, Q}, {"DH AH", nF3, Q},
    {"W ER L D", nE3, Q}, {"S OW", nE3, Q}, {"HH AY", nD3, H}, {"_", 0, 250},
};
// "like a diamond in the sky" — the last note is a melisma: one vowel, two pitches
const wp::Syllable line4[] = {
    {"L AY K", nG3, Q}, {"AH", nG3, Q}, {"D AY", nF3, Q}, {"M AH N D", nF3, Q},
    {"IH N", nE3, Q}, {"DH AH", nE3, Q}, {"S K AY", nD3, Q}, {"", nC3, H}, {"_", 0, 600},
};

struct Line { const wp::Syllable* syls; int count; };
#define LINE(x) { x, (int)(sizeof(x) / sizeof(x[0])) }
const Line kSong[] = { LINE(line1), LINE(line2), LINE(line3), LINE(line4), LINE(line1), LINE(line2) };
const int  kLines = (int)(sizeof(kSong) / sizeof(kSong[0]));

int next = 0;

void setup() {
    Serial.begin(115200);

    AudioMemory(12);
    sgtl5000.enable();
    sgtl5000.volume(0.5f);
    mixer.gain(0, 0.8f);

    // the score brings its own breath (at the last level anyone asked for,
    // 0.8 until someone does) and its own pitch. character is still yours:
    voice.setVibrato(5.5f, 30.0f, 0.3f);
    voice.setGlide(40.0f);                  // pitch changes on the beat, at this rate
}

void loop() {
    if (voice.busy()) return;               // the line is still being sung

    if (next == kLines) {                   // the song is over: breathe, then again
        delay(2000);
        next = 0;
    }
    if (!voice.sing(kSong[next].syls, kSong[next].count)) {
        Serial.print(F("line "));
        Serial.print(next);
        Serial.println(F(": bad phoneme, or too many events for one score"));
    }
    ++next;
}
