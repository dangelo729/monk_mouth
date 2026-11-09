// wind_pipe — wp_voice.h  (plan §7; whistle plan §3, §4, §6)
// the voice: the whole public surface. you move a mouth; the sound is the
// consequence.
//
// one block, top to bottom — this is all the engine does:
//
//   targets (live setters | due score events)
//      -> pitch axis: glide, vibrato, wobble            -> f0 for this block
//      -> body: every channel approaches its target     -> eased pose
//      -> anatomy: pose -> diameters + velum            -> areas -> reflections
//      -> watchers: closures, releases, pressure        -> burst + noise gains
//      -> airflow: folds, tract, lips in series         -> the jet's flow + transit
//      -> 128 x { glottis + aspiration -> tube (+nose) + turbulence + bursts
//                 + the jet at the lips, fed by the lips' own flow }
//      -> halfband lowpass -> gain -> clamp
//      -> state published for readback
//
// every call sets a target. a target set between blocks lands at the top of
// the next block — one latency, for everything. live setters and a running
// score write the same targets; the last writer wins, per channel, and that
// is the whole arbitration rule. (last in the caller's own order: a setter
// called before sing() loses to the score's first events, one called after
// wins, even when all of it lands in a single block.)
//
// whistle(midi, breath) is a convenience like noteOn: one call that writes a
// whistle pose from the table (wp_vocab.h) through the same targets — folds
// open, velum shut, tongue, pucker, a slot or none, breath — so the last
// writer wins here too. a note outside the table's range folds into it by
// octaves; a voice of another size looks its note up shifted by its size.
// it leaves the folds open: a bare noteOn after it whispers, a score commit,
// stop() or setGlottis(0) voices again. words and whistles are exclusive:
// whistle() ends any words in flight.
//
// the tuning (wp_tuning.h) — who is singing — goes the same way, whole: set
// between blocks, it lands at the top of the next one, ahead of that block's
// targets, so a setTuning and a noteOn in one frame aim at the new rates.
// so does the size (setSections), after the tuning: any number of sections,
// not only a whole one (wp_tube.h's Layout). the pose stands, the pipe takes
// the new length under whatever is sounding, and every formant moves with
// the ratio of the lengths — a size moved a little at a time glides. a
// whistle() looks its note up at the size last asked for.
//
// threading: none here. on the Teensy the wrapper masks interrupts around
// latch() and state(); a host app with an audio thread wraps its calls in its
// own mutex.
#pragma once

#include "wp_body.h"
#include "wp_config.h"
#include "wp_glottis.h"
#include "wp_jet.h"
#include "wp_noise.h"
#include "wp_pitch.h"
#include "wp_score.h"
#include "wp_tube.h"
#include "wp_tuning.h"
#include "wp_vocab.h"

namespace wp {

// the eased body, as of the end of the last block. API units throughout.
struct BodyState {
    float breath = 0, glottis = 0, tenseness = 0;
    float tonguePos = 0, tongueHeight = 0, lips = 1, velum = 0;
    float slotPlace[2] = {0, 0}, slotDegree[2] = {0, 0};
    bool  slotActive[2] = {false, false};
    float midi = 0, f0 = 0;             // the eased note; the f0 actually sung
    float vibrato = 0;                  // the vibrato's swing in f0, in cents: its depth, speed, onset and bloom
    float voicing = 0;                  // gain on the glottal pulse
    float hiss = 0;                     // strongest turbulence, 0..1
    float pressure = 0;                 // behind a closure
    bool  closed = false;               // some source is shut
    bool  busy = false;                 // a score is playing
    uint8_t phoneme = ph::none;         // the score's current phoneme
    float whistle = 0;                  // the jet's swing over its cap, 0..1: near 1 locked
    float whistleHz = 0;                // the whistle's pitch; held while whistle is down
};

class Voice {
public:
    // size is in sections, whole or not: 22 = 17.5 cm (adult male), 19 ~ 15 cm, 16 ~ 12.7 cm, up to 26.
    explicit Voice(float sections = kDefaultSections, uint32_t seed = 1, float sampleRate = kDefaultFs);

    // lungs + larynx
    void setBreath(float p01);              // the loudness axis; 0 = silent
    void setPitch(float midi);
    void setGlottis(float open01);          // 0 voiced .. 1 open (h, whisper, voiceless)
    void setTenseness(float t01);           // breathy .. pressed
    // tract
    void setTongue(float pos01, float height01);    // pos 0 = back, 1 = front; height 1 = highest
    void setLips(float aperture01);                 // 0 closes
    void setVelum(float open01);
    void setConstriction(int slot, float place01, float degree);   // degree <= 0 closes
    void clearConstriction(int slot);
    // character
    void setGlide(float perSec);            // 0 = jump
    void setVibrato(float rateHz, float cents, float onsetSec);
    void setWobble(float amount01);
    // who is singing: every tunable at once, held inside its hard limits
    void   setTuning(const Tuning& t);
    Tuning tuning() const { return tuning_; }       // the last one asked for, as clamped
    void   setSections(float sections);             // the size, live: held to kMinSections .. kMaxSections
    // conveniences
    void noteOn(float midi, float vel01);   // pitch + breath + vibrato onset
    void noteOff();                         // breath to 0
    void whistle(float midi, float breath); // a whistle pose for this note from the table, and breath; ends any words in flight
    void whistleRange(float& lo, float& hi) const;  // the notes whistle() reaches without folding
    // words (§9). false = bad phoneme or overflow, and nothing was disturbed.
    bool syllable(const char* phones);      // now, at the current note
    bool sing(const Syllable* syls, int n);
    bool sing(const Syllable* syls, int n, float breath);    // at this breath; the voice's own level stays
    bool say(const char* phones);
    // spoken, not sung: lengths, accents and loudness from the stress digits,
    // a pitch that keeps moving, no vibrato while it plays
    bool speak(const char* phones);                    // Speech() defaults
    bool speak(const char* phones, const Speech& s);
    void stop();
    bool busy() const { return player_.busy(); }
    // readback
    BodyState state() const { return state_; }      // eased pose, f0, vibrato, voicing, hiss, closed, phoneme, whistle
    int  diameters(float* out, int max) const;
    // the engine
    void render(float* out, int n);         // host and wrapper both call this

    // ---- for the platform wrapper --------------------------------------------------------
    // latch() moves the pending targets, and a pending tuning, across to the
    // audio side. render() does it itself at the top of every block unless the
    // wrapper takes it over (to put its interrupt mask around exactly this copy
    // and nothing else).
    void latch();
    void setAutoLatch(bool on) { autoLatch_ = on; }

    float sections() const   { return sections_; }               // the last size asked for, as held
    float sampleRate() const { return fs_; }
    void  setHalfband(bool on) { useHalfband_ = on; }            // keep or drop by ear
    const Body& body() const { return body_; }                   // tests and tools
    const Tube& tube() const { return tube_; }
    const Jet&  jet() const  { return jet_; }

private:
    struct Table { uint32_t dirty = 0; float a[kChCount] = {}, b[kChCount] = {}; uint32_t seq[kChCount] = {}; };

    void write(uint8_t ch, float a, float b = 0.0f);
    void apply(uint8_t ch, float a, float b, float rate);
    void applyLive(bool onlyBefore, uint32_t seq);
    void control();
    void retune(const Tuning& t);
    void resize(float sections);
    void endBlock();
    void renderRun(float* out, int n);
    void publish();

    float fs_;
    Rng   rng_;
    Tube  tube_;
    Glottis glottis_;
    Jet   jet_;
    Noise noise_;
    Body  body_;
    PitchAxis pitch_;
    Halfband halfband_;
    ScorePlayer player_;

    Table pending_, latched_;
    uint32_t seq_ = 0;                      // caller side: orders writes and commits
    Tuning tuning_, tuningLatched_;         // caller side (the score compiler reads diction); audio side
    bool  tuningPending_ = false;           // set by the caller, cleared by latch()
    bool  retuneDue_ = false;               // audio side: a latched tuning waits for the block top
    float sections_ = kDefaultSections, sectionsLatched_ = kDefaultSections;   // caller side; audio side
    bool  sectionsPending_ = false;         // set by the caller, cleared by latch()
    bool  resizeDue_ = false;               // audio side: a latched size waits for the block top
    bool  autoLatch_ = true;
    bool  useHalfband_ = true;
    float scoreBreath_ = kScoreBreath;      // the last level anyone asked for
    float midi_ = kDefaultMidi;             // caller side: the last note anyone asked for
    float whistleShift_ = 0.0f;             // 12 log2(N / kDefaultSections): a note's table midi, less the note

    int   blockPos_ = 0;
    bool  silent_ = true;
    uint8_t phoneme_ = ph::none;

    // per-sample ramps toward this block's gains
    float voicing_ = 0, dVoicing_ = 0, voicingT_ = 0;
    float asp_ = 0, dAsp_ = 0, aspT_ = 0;
    float out_ = kOutputGain, dOut_ = 0, outT_ = kOutputGain;     // the output level: kOutputGain * the tuning's gain
    float turb_[3] = {0, 0, 0}, dTurb_[3] = {0, 0, 0}, turbT_[3] = {0, 0, 0};
    bool  turbOn_[3] = {false, false, false};
    int   turbA_[3] = {1, 1, 1}, turbB_[3] = {1, 1, 1};
    float turbFrac_[3] = {0, 0, 0};
    float voicedness_ = 0;
    float noiseFloor_ = 0, noisePulse_ = 0, noiseUnvoiced_ = 0;    // the tuning's, for the noise modulator

    BodyState state_;
    int   stateSections_ = kDefaultSections;
    float stateDiameters_[kMaxSections];
};

}  // namespace wp
