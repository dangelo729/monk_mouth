// wind_pipe — wp_voice.h  (plan §7)
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
//      -> 128 x { glottis + aspiration -> tube (+nose) + turbulence + bursts }
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
// the tuning (wp_tuning.h) — who is singing — goes the same way, whole: set
// between blocks, it lands at the top of the next one, ahead of that block's
// targets, so a setTuning and a noteOn in one frame aim at the new rates.
//
// threading: none here. on the Teensy the wrapper masks interrupts around
// latch() and state(); a host app with an audio thread wraps its calls in its
// own mutex.
#pragma once

#include "wp_body.h"
#include "wp_config.h"
#include "wp_glottis.h"
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
    float voicing = 0;                  // gain on the glottal pulse
    float hiss = 0;                     // strongest turbulence, 0..1
    float pressure = 0;                 // behind a closure
    bool  closed = false;               // some source is shut
    bool  busy = false;                 // a score is playing
    uint8_t phoneme = ph::none;         // the score's current phoneme
};

class Voice {
public:
    // size is a section count: 22 = 17.5 cm (adult male), 19 ~ 15 cm, 16 ~ 12.7 cm, up to 26.
    explicit Voice(int sections = kDefaultSections, uint32_t seed = 1, float sampleRate = kDefaultFs);

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
    // conveniences
    void noteOn(float midi, float vel01);   // pitch + breath + vibrato onset
    void noteOff();                         // breath to 0
    // words (§9). false = bad phoneme or overflow, and nothing was disturbed.
    bool syllable(const char* phones);      // now, at the current note
    bool sing(const Syllable* syls, int n);
    bool sing(const Syllable* syls, int n, float breath);    // at this breath; the voice's own level stays
    bool say(const char* phones);
    void stop();
    bool busy() const { return player_.busy(); }
    // readback
    BodyState state() const { return state_; }      // eased pose, f0, voicing, hiss, closed, phoneme
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

    int   sections() const   { return tube_.sections(); }
    float sampleRate() const { return fs_; }
    void  setHalfband(bool on) { useHalfband_ = on; }            // keep or drop by ear
    const Body& body() const { return body_; }                   // tests and tools
    const Tube& tube() const { return tube_; }

private:
    struct Table { uint32_t dirty = 0; float a[kChCount] = {}, b[kChCount] = {}; uint32_t seq[kChCount] = {}; };

    void write(uint8_t ch, float a, float b = 0.0f);
    void apply(uint8_t ch, float a, float b, float rate);
    void applyLive(bool onlyBefore, uint32_t seq);
    void control();
    void retune(const Tuning& t);
    void endBlock();
    void renderRun(float* out, int n);
    void publish();

    float fs_;
    Rng   rng_;
    Tube  tube_;
    Glottis glottis_;
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
    bool  autoLatch_ = true;
    bool  useHalfband_ = true;
    float scoreBreath_ = kScoreBreath;      // the last level anyone asked for
    float midi_ = kDefaultMidi;             // caller side: the last note anyone asked for

    int   blockPos_ = 0;
    bool  silent_ = true;
    uint8_t phoneme_ = ph::none;

    // per-sample ramps toward this block's gains
    float voicing_ = 0, dVoicing_ = 0, voicingT_ = 0;
    float asp_ = 0, dAsp_ = 0, aspT_ = 0;
    float turb_[3] = {0, 0, 0}, dTurb_[3] = {0, 0, 0}, turbT_[3] = {0, 0, 0};
    bool  turbOn_[3] = {false, false, false};
    int   turbA_[3] = {1, 1, 1}, turbB_[3] = {1, 1, 1};
    float turbFrac_[3] = {0, 0, 0};
    float voicedness_ = 0;
    float noiseFloor_ = 0, noisePulse_ = 0, noiseUnvoiced_ = 0;    // the tuning's, for the noise modulator

    BodyState state_;
    float stateDiameters_[kMaxSections];
};

}  // namespace wp
