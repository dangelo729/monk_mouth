// ----------------------------------------------------------------------------
// wind_pipe example 99 — Benchmark
//
// the hardware go / no-go (plan: stage 2, and the acceptance gate):
//
//   four voices, all voiced with the velum open and a fricative held,
//   <= 25 % AudioProcessorUsageMax, steady for a minute, and flat as notes
//   ring out.
//
// that pose is the worst case the engine has: the glottal pulse and the
// aspiration band are running, the nose ladder is coupled in, a turbulence
// source is injecting into the tube, and vibrato + wobble keep the pitch —
// and so the per-period glottis setup — moving.
//
// three phases, a line per second, forever:
//
//   ONE    15 s   one voice in the worst case, the other three silent
//                 (a silent voice skips its tube: near zero)
//   FOUR   60 s   all four in the worst case          <- the gate: max <= 25 %
//   RING   15 s   every note off. the tubes ring down into the silence gate.
//                 CPU must FALL here. if it RISES, denormals are getting
//                 through (FPDSCR flush-to-zero, AudioSynthWindPipe.cpp).
//
// columns: phase, second, total % now, total % max this second, max this
// phase, each voice's max this second, audio memory max.
//
// if FOUR misses 25 %, the plan's levers, in order: skip the nose ladder
// while the velum is shut and the nose has rung out; interpolate k every
// other sample; drop the halfband (send 'h' to toggle it here and see what
// it buys).
//
// Teensy 4.1 + the SGTL5000 audio shield. build at 600 MHz, "Faster".
// ----------------------------------------------------------------------------

#include <Audio.h>
#include <WindPipe.h>

// four sizes of singer, four seeds: no two voices share a sample
AudioSynthWindPipe   voice0(22, 1);
AudioSynthWindPipe   voice1(22, 2);
AudioSynthWindPipe   voice2(19, 3);
AudioSynthWindPipe   voice3(26, 4);         // the longest tube is the dearest
AudioMixer4          mixer;
AudioOutputI2S       i2s;
AudioConnection      patchCord1(voice0, 0, mixer, 0);
AudioConnection      patchCord2(voice1, 0, mixer, 1);
AudioConnection      patchCord3(voice2, 0, mixer, 2);
AudioConnection      patchCord4(voice3, 0, mixer, 3);
AudioConnection      patchCord5(mixer, 0, i2s, 0);
AudioConnection      patchCord6(mixer, 0, i2s, 1);
AudioControlSGTL5000 sgtl5000;

AudioSynthWindPipe* const kVoices[4] = { &voice0, &voice1, &voice2, &voice3 };
const float kNotes[4] = { 45.0f, 52.0f, 57.0f, 40.0f };

enum Phase { ONE, FOUR, RING };
const char* const kPhaseName[] = { "ONE ", "FOUR", "RING" };
const uint32_t    kPhaseSec[]  = { 15, 60, 15 };

Phase    phase = ONE;
uint32_t second = 0, lastPrintMs = 0;
float    phaseMax = 0.0f;
bool     halfband = true;

// all voiced, velum open, a fricative held
void worstCase(AudioSynthWindPipe& v, float midi) {
    v.setVibrato(6.0f, 40.0f, 0.0f);
    v.setWobble(1.0f);
    v.setTenseness(0.7f);
    // velum part open: the nose is coupled in, and air still goes through the
    // mouth. (fully open, the engine vents the hiss through the nose — the rule
    // that keeps nasals from bursting — and the injection path would sit idle.)
    v.setVelum(0.3f);
    v.setConstriction(0, 0.82f, 0.15f);     // /z/: mid hiss window, turbulence injecting
    v.noteOn(midi, 1.0f);
}

void enter(Phase p) {
    phase = p;
    second = 0;
    phaseMax = 0.0f;
    switch (p) {
        case ONE:
            worstCase(voice0, kNotes[0]);
            break;
        case FOUR:
            for (int i = 0; i < 4; ++i) worstCase(*kVoices[i], kNotes[i]);
            break;
        case RING:
            for (int i = 0; i < 4; ++i) {
                kVoices[i]->noteOff();
                kVoices[i]->clearConstriction(0);
                kVoices[i]->setVelum(0.0f);
            }
            break;
    }
}

void setup() {
    Serial.begin(115200);

    AudioMemory(24);
    sgtl5000.enable();
    sgtl5000.volume(0.4f);
    for (int i = 0; i < 4; ++i) mixer.gain(i, 0.25f);

    while (!Serial && millis() < 3000) {}
    Serial.println(F("wind_pipe benchmark — gate: FOUR max <= 25 %, RING falls"));
    Serial.print(F("F_CPU_ACTUAL ")); Serial.println(F_CPU_ACTUAL);
    enter(ONE);
    lastPrintMs = millis();
    AudioProcessorUsageMaxReset();
}

void loop() {
    if (Serial.available() && Serial.read() == 'h') {
        halfband = !halfband;
        for (int i = 0; i < 4; ++i) kVoices[i]->setHalfband(halfband);
        Serial.println(halfband ? F("halfband on") : F("halfband off"));
    }

    const uint32_t now = millis();
    if (now - lastPrintMs < 1000u) return;
    lastPrintMs = now;
    ++second;

    // already percent on Teensy 4 (CYCLE_COUNTER_APPROX_PERCENT, AudioStream.h)
    const float total = (float)AudioProcessorUsage();
    const float totalMax = (float)AudioProcessorUsageMax();
    if (second > 1 && totalMax > phaseMax) phaseMax = totalMax;     // skip the second the phase changed in

    Serial.print(kPhaseName[phase]);
    Serial.print(' ');
    if (second < 10) Serial.print(' ');
    Serial.print(second);
    Serial.print(F("  total ")); Serial.print(total, 2);
    Serial.print(F("  max "));   Serial.print(totalMax, 2);
    Serial.print(F("  phase max ")); Serial.print(phaseMax, 2);
    Serial.print(F("  voices"));
    for (int i = 0; i < 4; ++i) {
        Serial.print(' ');
        Serial.print((float)kVoices[i]->processorUsageMax(), 2);
        kVoices[i]->processorUsageMaxReset();
    }
    Serial.print(F("  mem ")); Serial.println(AudioMemoryUsageMax());
    AudioProcessorUsageMaxReset();

    if (second >= kPhaseSec[phase]) {
        if (phase == FOUR) {
            Serial.print(F("== FOUR voices, worst case: max "));
            Serial.print(phaseMax, 2);
            Serial.println(phaseMax <= 25.0f ? F(" %  PASS (<= 25)") : F(" %  MISS (> 25)"));
        } else if (phase == ONE) {
            Serial.print(F("== ONE voice, worst case: max "));
            Serial.print(phaseMax, 2);
            Serial.println(F(" %"));
        } else {
            Serial.print(F("== RING: last second "));
            Serial.print(totalMax, 2);
            Serial.println(F(" %  (must have fallen, not risen)"));
        }
        enter(phase == ONE ? FOUR : phase == FOUR ? RING : ONE);
    }
}
