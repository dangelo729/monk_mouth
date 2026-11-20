// monk_mouth example 01 — Drone
// one voice holds a note on a Teensy 4.x with the audio shield. every three
// seconds the mouth moves to the next vowel, and the sound follows it.

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
