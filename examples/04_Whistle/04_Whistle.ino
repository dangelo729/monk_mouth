// monk_mouth example 04 — Whistle
// whistle(midi, breath) poses the tongue and a pucker for the note and blows.
// a tune, a breath between its notes. a note outside whistleRange() folds
// into it by octaves.

#include <Audio.h>
#include <MonkMouth.h>

AudioSynthMonkMouth  voice;
AudioOutputI2S       i2s;
AudioConnection      left(voice, 0, i2s, 0), right(voice, 0, i2s, 1);
AudioControlSGTL5000 codec;

const float kTune[] = {84, 84, 91, 91, 93, 93, 91};   // C6 C6 G6 G6 A6 A6 G6

void setup() {
    Serial.begin(115200);
    AudioMemory(8);
    codec.enable();
    codec.volume(0.5f);
    float lo, hi;
    voice.whistleRange(lo, hi);
    Serial.printf("whistles midi %.0f to %.0f without folding\n", lo, hi);
}

void loop() {
    for (float note : kTune) {
        voice.whistle(note, 0.8f);
        delay(350);
        voice.noteOff();                    // the breath stops; the pucker stays
        delay(100);
    }
    delay(1000);
}
