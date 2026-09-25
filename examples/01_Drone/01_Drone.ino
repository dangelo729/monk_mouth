// ----------------------------------------------------------------------------
// wind_pipe example 01 — Drone
//
// a held vowel on I2S (Teensy 4.1 + the SGTL5000 audio shield).
//
// nothing here says what the sound is. the sketch poses a mouth — tongue and
// lips — gives it breath and a note, and the vowel is the consequence. every
// few seconds the pose moves on (AA -> IY -> UW) and the body eases there at
// its own rate, so the drone glides between vowels on one unbroken breath.
// ----------------------------------------------------------------------------

#include <Audio.h>
#include <WindPipe.h>

// construction order = signal-flow order: voice -> mixer -> output
AudioSynthWindPipe   voice;                 // 22 sections = 17.5 cm, an adult male tract
AudioMixer4          mixer;
AudioOutputI2S       i2s;
AudioConnection      patchCord1(voice, 0, mixer, 0);
AudioConnection      patchCord2(mixer, 0, i2s, 0);    // L
AudioConnection      patchCord3(mixer, 0, i2s, 1);    // R
AudioControlSGTL5000 sgtl5000;

// pose the mouth for a vowel out of the library's own table. the table holds
// raw anatomy units; the setters take 0..1, so map one onto the other.
void poseVowel(uint8_t phone) {
    const wp::VowelPose& v = wp::vowel(phone);
    const float pos    = (v.tongueX - wp::kTongueXMin) / (wp::kTongueXMax - wp::kTongueXMin);
    const float height = (wp::kTongueDMax - v.tongueD) / (wp::kTongueDMax - wp::kTongueDMin);
    voice.setTongue(pos, height);           // pos 0 = back, 1 = front; height 1 = highest
    voice.setLips(v.lipD / wp::kLipDMax);   // 1 = wide open, 0 = closed
}

const uint8_t kVowels[] = { wp::ph::AA, wp::ph::IY, wp::ph::UW };
uint32_t lastMoveMs = 0;
int      at = 0;

void setup() {
    Serial.begin(115200);

    AudioMemory(12);
    sgtl5000.enable();
    sgtl5000.volume(0.5f);
    mixer.gain(0, 0.8f);

    voice.setVibrato(5.5f, 25.0f, 0.4f);    // rate Hz, depth in cents, onset delay s
    voice.setWobble(0.3f);                  // what keeps a held note alive
    poseVowel(kVowels[0]);
    voice.noteOn(45.0f, 0.8f);              // A2: pitch + breath + vibrato onset
}

void loop() {
    const uint32_t now = millis();
    if (now - lastMoveMs >= 3000u) {
        lastMoveMs = now;
        at = (at + 1) % 3;
        poseVowel(kVowels[at]);             // a target, not an event: the tongue eases there
        Serial.print(wp::phoneName(kVowels[at]));
        Serial.print("   cpu max % ");
        Serial.println((float)AudioProcessorUsageMax(), 2);
        AudioProcessorUsageMaxReset();
    }
}
