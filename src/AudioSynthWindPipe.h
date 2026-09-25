// wind_pipe — AudioSynthWindPipe.h  (plan §10)
// the one file that knows the platform. one custom AudioStream, zero inputs,
// one output; it owns a wp::Voice and nothing else. polyphony is N instances
// into a stock mixer. no heap after construction.
//
// the ENTIRE contents — includes too — sit inside #if defined(ARDUINO), so
// the umbrella WindPipe.h stays safe on the host: there this header is
// nothing, and tests and tools use wp::Voice directly. there is no host twin.
//
// handoff (plan §10): no lanes, no locks, no ordering rules.
//
//   setters      each one fills the voice's pending target table inside a
//                few-instruction interrupt mask (PRIMASK saved and restored,
//                so a setter called with interrupts already off leaves them
//                off). a pair like setTongue(pos, height) can never be seen
//                half-written, and noteOn's pitch + breath + vibrato onset
//                land in the same block.
//   update()     masks interrupts ONLY around voice_.latch() — the ~100-byte
//                copy of that table to the audio side (and, when one was set,
//                the 140-byte tuning), sub-microsecond — and renders
//                unmasked. a target set from the main loop lands at the top
//                of the next block: one latency, for everything.
//   state()      copies the published BodyState back the same way.
//   words        sing / say / syllable run UNMASKED: the compile is long and
//                touches nothing the audio side reads. the commit is
//                ScorePlayer::commit's flag protocol — withdraw the pending
//                flag, fill the buffer the audio side is NOT playing, raise
//                the flag — and beginBlock() flips buffers only while the
//                flag is up. on a single core the audio interrupt can preempt
//                the loop but never the reverse, so it sees either no commit
//                or a whole one. call them from the main loop, not from an
//                interrupt: they use ~10 KB of stack.
//
// denormals: the flush-to-zero bit is set in FPDSCR at construction, because
// update() runs in interrupt context and an exception's FPSCR is loaded from
// FPDSCR, not from the thread's FPSCR. the core's silence gate makes this
// belt and braces.
#pragma once

#if defined(ARDUINO)

// core headers only — no dependency on the Audio library itself.
// AUDIO_BLOCK_SAMPLES and AUDIO_SAMPLE_RATE_EXACT live in the core's AudioStream.h.
#include <Arduino.h>
#include <AudioStream.h>

#include "core/wp_voice.h"

class AudioSynthWindPipe : public AudioStream {
public:
    // size is a section count, set at construction: 22 = 17.5 cm (adult male),
    // 19 ~ 15 cm, 16 ~ 12.7 cm, up to 26. one seeded xorshift32 per voice:
    // give each voice of a choir its own seed. statically allocate, never destroy.
    explicit AudioSynthWindPipe(int sections = wp::kDefaultSections, uint32_t seed = 1);

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
    // who is singing: every tunable at once (core/wp_tuning.h), held inside its hard limits
    void setTuning(const wp::Tuning& t);
    wp::Tuning tuning() const;              // the last one asked for, as clamped
    // conveniences
    void noteOn(float midi, float vel01);   // pitch + breath + vibrato onset, one block
    void noteOff();                         // breath to 0
    void whistle(float midi, float breath); // a whistle pose for this note from the table, and breath, one block (whistle plan §6)
    // words (plan §9). false = bad phoneme or overflow, and nothing was disturbed.
    // main loop only (see above).
    bool syllable(const char* phones);      // now, at the current note
    bool sing(const wp::Syllable* syls, int n);
    bool sing(const wp::Syllable* syls, int n, float breath);   // at this breath; the voice's own level stays
    bool say(const char* phones);
    void stop();
    bool busy() const;                      // a volatile read of a flag the audio side keeps
    // readback, as of the end of the last block
    wp::BodyState state() const;            // eased pose, f0, voicing, hiss, closed, phoneme
    int  diameters(float* out, int max) const;

    int  sections() const { return voice_.sections(); }
    void setHalfband(bool on);              // the output lowpass: keep or drop by ear (a CPU lever)

    // the audio interrupt: latch targets (masked), render one block through
    // wp::Voice (unmasked), clamp, scale to int16, transmit, release. if
    // allocate() returns NULL the block is still rendered, so scores keep time.
    // no Serial, no heap, no I2C in here.
    virtual void update(void);

private:
    static void initFlushToZero();

    wp::Voice voice_;
    float     scratch_[AUDIO_BLOCK_SAMPLES];
};

#endif  // defined(ARDUINO)
