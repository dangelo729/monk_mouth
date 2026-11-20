// ----------------------------------------------------------------------------
// monk_mouth example 02 — Puppet
//
// serial commands -> targets: play the body by hand, and babble.
// (Teensy 4.1 + the SGTL5000 audio shield; serial monitor at any baud, line
// ending = newline.)
//
// the body has no events. every command below sets a target; each part moves
// toward its target at its own rate, and the sound is whatever the tube does
// on the way. one command per line, letter first, numbers space-separated:
//
//   b <0..1>                 breath — the loudness axis       setBreath
//   p <midi>                 pitch                            setPitch
//   g <0..1>                 glottis: 0 voiced .. 1 open      setGlottis
//   t <0..1>                 tenseness: breathy .. pressed    setTenseness
//   T <pos> <height>         tongue: 0 back .. 1 front,       setTongue
//                                    0 low .. 1 high
//   l <0..1>                 lips: 0 closed .. 1 open         setLips
//   v <0..1>                 velum: 0 shut .. 1 open          setVelum
//   c <slot> <place> <deg>   constriction, slot 0 or 1,       setConstriction
//                            place 0 glottis .. 1 lips,
//                            deg <= 0 closes
//   x <slot>                 let the constriction go          clearConstriction
//   G <perSec>               pitch glide rate, 0 = jump       setGlide
//   V <hz> <cents> <onset>   vibrato                          setVibrato
//   w <0..1>                 wobble                           setWobble
//   n <midi> [vel]           note on (vel defaults to 0.8)    noteOn
//   o                        note off                         noteOff
//   s <phones>               one syllable, now, this note     syllable
//   S <phones>               say, at the current pitch        say
//   !                        stop the words, close down       stop
//   ? [ms]                   print the body once, or every <ms> (0 = off)
//   d                        print the tract's diameters, glottis to lips
//   h                        this list
//
// things to try, a line at a time:
//   n 45            a note on the rest shape: "uh"
//   T 0.9 0.95      the tongue goes high and front: "ee"
//   T 0.1 0.8       low and back: "ah"
//   l 0             lips shut: the sound stops, pressure builds (watch ?)
//   l 1             lips open: "b". the burst is the consequence.
//   c 0 0.82 0      tongue tip to the ridge, shut ...
//   x 0             ... and let go: "d"
//   c 0 0.82 0.15   the same place, not quite shut: "z"; then  g 1  makes it "s"
//   v 1             velum open; now  l 0  hums instead of stopping: "m"
//   s B AA          the same things, scripted:  s S T R IY T   S HH AH L OW / W ER L D
// ----------------------------------------------------------------------------

#include <Audio.h>
#include <MonkMouth.h>

AudioSynthMonkMouth   voice;
AudioMixer4          mixer;
AudioOutputI2S       i2s;
AudioConnection      patchCord1(voice, 0, mixer, 0);
AudioConnection      patchCord2(mixer, 0, i2s, 0);
AudioConnection      patchCord3(mixer, 0, i2s, 1);
AudioControlSGTL5000 sgtl5000;

char     line[200];
size_t   lineLen = 0;
uint32_t stateEveryMs = 0, lastStateMs = 0;

// ---- parsing ---------------------------------------------------------------------

// reads one number and advances s past it. false if there was none.
bool num(char*& s, float& out) {
    char* end = s;
    out = strtof(s, &end);
    if (end == s) return false;
    s = end;
    return true;
}

const char* trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    return s;
}

// ---- printing ----------------------------------------------------------------------

void printHelp() {
    Serial.println(F("b breath | p midi | g glottis | t tense | T pos height | l lips | v velum"));
    Serial.println(F("c slot place deg | x slot | G glide | V hz cents onset | w wobble"));
    Serial.println(F("n midi [vel] | o | s PHONES | S PHONES | ! stop | ? [ms] | d | h"));
}

void printState() {
    const mm::BodyState s = voice.state();
    Serial.print(F("breath "));   Serial.print(s.breath, 2);
    Serial.print(F("  glottis ")); Serial.print(s.glottis, 2);
    Serial.print(F("  tense "));   Serial.print(s.tenseness, 2);
    Serial.print(F("  tongue "));  Serial.print(s.tonguePos, 2); Serial.print(' '); Serial.print(s.tongueHeight, 2);
    Serial.print(F("  lips "));    Serial.print(s.lips, 2);
    Serial.print(F("  velum "));   Serial.print(s.velum, 2);
    for (int j = 0; j < 2; ++j) {
        if (!s.slotActive[j]) continue;
        Serial.print(F("  slot")); Serial.print(j); Serial.print(' ');
        Serial.print(s.slotPlace[j], 2); Serial.print('@'); Serial.print(s.slotDegree[j], 2);
    }
    Serial.print(F("  | midi "));  Serial.print(s.midi, 2);
    Serial.print(F("  f0 "));      Serial.print(s.f0, 1);
    Serial.print(F("  voicing ")); Serial.print(s.voicing, 2);
    Serial.print(F("  hiss "));    Serial.print(s.hiss, 2);
    Serial.print(F("  pressure ")); Serial.print(s.pressure, 2);
    if (s.closed) Serial.print(F("  CLOSED"));
    if (s.busy)   Serial.print(F("  busy"));
    if (s.phoneme) { Serial.print(F("  /")); Serial.print(mm::phoneName(s.phoneme)); Serial.print('/'); }
    Serial.println();
}

void printDiameters() {
    float d[mm::kMaxSections];
    const int n = voice.diameters(d, mm::kMaxSections);
    for (int i = 0; i < n; ++i) { Serial.print(d[i], 2); Serial.print(' '); }
    Serial.println();
}

// ---- one command ---------------------------------------------------------------------

void handle(char* text) {
    char* s = text;
    while (*s == ' ' || *s == '\t') ++s;
    if (!*s) return;
    const char cmd = *s++;
    float a = 0, b = 0, c = 0;
    bool ok = true;

    switch (cmd) {
        case 'b': ok = num(s, a); if (ok) voice.setBreath(a); break;
        case 'p': ok = num(s, a); if (ok) voice.setPitch(a); break;
        case 'g': ok = num(s, a); if (ok) voice.setGlottis(a); break;
        case 't': ok = num(s, a); if (ok) voice.setTenseness(a); break;
        case 'T': ok = num(s, a) && num(s, b); if (ok) voice.setTongue(a, b); break;
        case 'l': ok = num(s, a); if (ok) voice.setLips(a); break;
        case 'v': ok = num(s, a); if (ok) voice.setVelum(a); break;
        case 'c': ok = num(s, a) && num(s, b) && num(s, c); if (ok) voice.setConstriction((int)a, b, c); break;
        case 'x': ok = num(s, a); if (ok) voice.clearConstriction((int)a); break;
        case 'G': ok = num(s, a); if (ok) voice.setGlide(a); break;
        case 'V': ok = num(s, a) && num(s, b) && num(s, c); if (ok) voice.setVibrato(a, b, c); break;
        case 'w': ok = num(s, a); if (ok) voice.setWobble(a); break;
        case 'n': ok = num(s, a); if (ok) voice.noteOn(a, num(s, b) ? b : 0.8f); break;
        case 'o': voice.noteOff(); break;
        case 's': ok = voice.syllable(trim(s)); break;      // false: bad phoneme, nothing disturbed
        case 'S': ok = voice.say(trim(s)); break;
        case '!': voice.stop(); break;
        case '?':
            if (num(s, a)) stateEveryMs = a > 0 ? (uint32_t)a : 0u;
            else printState();
            break;
        case 'd': printDiameters(); break;
        case 'h': printHelp(); break;
        default:  ok = false; break;
    }
    if (!ok) { Serial.print(F("?? ")); Serial.println(text); }
}

// ---- sketch -----------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    AudioMemory(12);
    sgtl5000.enable();
    sgtl5000.volume(0.5f);
    mixer.gain(0, 0.8f);

    while (!Serial && millis() < 3000) {}
    Serial.println(F("monk_mouth puppet — h for help"));
}

void loop() {
    while (Serial.available()) {
        const char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            line[lineLen] = 0;
            handle(line);
            lineLen = 0;
        } else if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = ch;
        }
    }
    const uint32_t now = millis();
    if (stateEveryMs && now - lastStateMs >= stateEveryMs) {
        lastStateMs = now;
        printState();
    }
}
