// wind_pipe — AudioSynthWindPipe.cpp  (plan §10)
// the header self-guards on #if defined(ARDUINO), so on the host this
// translation unit is intentionally empty.
#include "AudioSynthWindPipe.h"

#if defined(ARDUINO)

// the architectural address of the Floating-Point Default Status Control
// Register. the Teensy core defines it; keep a fallback so the wrapper never
// silently loses its denormal protection.
#ifndef SCB_FPDSCR
#define SCB_FPDSCR (*(volatile uint32_t*)0xE000EF3Cu)
#endif

namespace {

constexpr uint32_t kFlushToZero = 1u << 24;       // FPSCR / FPDSCR bit FZ

// everything masked, for a few dozen bytes of copy. PRIMASK is saved and
// restored rather than blindly re-enabled: a setter called from inside
// someone else's critical section must not end it.
class IrqMask {
public:
    IrqMask() {
        __asm__ volatile("mrs %0, primask\n" : "=r" (primask_) ::);
        __disable_irq();
    }
    ~IrqMask() { if (primask_ == 0) __enable_irq(); }
private:
    uint32_t primask_;
};

}  // namespace

// loop bounds below use AUDIO_BLOCK_SAMPLES. the core's control block is
// wp::kBlock = 128, the stock value — but nothing breaks if a build changes
// AUDIO_BLOCK_SAMPLES: wp::Voice::render runs control once per 128 samples
// internally whatever size it is called with, and latch() merges into the
// audio side's table, so one latch per update() is right at any block size.
static_assert(AUDIO_BLOCK_SAMPLES > 0, "AUDIO_BLOCK_SAMPLES must be positive");

// init-only, so it lives in flash. FZ makes denormals flush to zero. setting
// it in the thread's FPSCR alone would NOT cover update(): on ARMv7-M a new
// floating-point context in an exception is loaded from FPDSCR. verify on
// hardware (plan: stage 2) — CPU must not rise as a note rings out.
FLASHMEM void AudioSynthWindPipe::initFlushToZero() {
    static bool done = false;
    if (done) return;
    done = true;
    SCB_FPDSCR |= kFlushToZero;                                       // every interrupt context from here on
    __builtin_arm_set_fpscr(__builtin_arm_get_fpscr() | kFlushToZero); // and the current one (setup / loop)
}

AudioSynthWindPipe::AudioSynthWindPipe(float sections, uint32_t seed)
    : AudioStream(0, NULL), voice_(sections, seed, AUDIO_SAMPLE_RATE_EXACT) {
    initFlushToZero();
    voice_.setAutoLatch(false);                   // update() owns the latch, inside its mask
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) scratch_[i] = 0.0f;
}

// ---- setters: every call sets a target, atomically with respect to update() -----------------------

void AudioSynthWindPipe::setBreath(float p01)        { IrqMask m; voice_.setBreath(p01); }
void AudioSynthWindPipe::setPitch(float midi)        { IrqMask m; voice_.setPitch(midi); }
void AudioSynthWindPipe::setGlottis(float open01)    { IrqMask m; voice_.setGlottis(open01); }
void AudioSynthWindPipe::setTenseness(float t01)     { IrqMask m; voice_.setTenseness(t01); }
void AudioSynthWindPipe::setTongue(float pos01, float height01) { IrqMask m; voice_.setTongue(pos01, height01); }
void AudioSynthWindPipe::setLips(float aperture01)   { IrqMask m; voice_.setLips(aperture01); }
void AudioSynthWindPipe::setVelum(float open01)      { IrqMask m; voice_.setVelum(open01); }
void AudioSynthWindPipe::setConstriction(int slot, float place01, float degree) {
    IrqMask m; voice_.setConstriction(slot, place01, degree);
}
void AudioSynthWindPipe::clearConstriction(int slot) { IrqMask m; voice_.clearConstriction(slot); }
void AudioSynthWindPipe::setGlide(float perSec)      { IrqMask m; voice_.setGlide(perSec); }
void AudioSynthWindPipe::setVibrato(float rateHz, float cents, float onsetSec) {
    IrqMask m; voice_.setVibrato(rateHz, cents, onsetSec);
}
void AudioSynthWindPipe::setWobble(float amount01)   { IrqMask m; voice_.setWobble(amount01); }
void AudioSynthWindPipe::setTuning(const wp::Tuning& t) { IrqMask m; voice_.setTuning(t); }
wp::Tuning AudioSynthWindPipe::tuning() const        { return voice_.tuning(); }   // caller side only: no mask
void AudioSynthWindPipe::setSections(float sections) { IrqMask m; voice_.setSections(sections); }
void AudioSynthWindPipe::noteOn(float midi, float vel01) { IrqMask m; voice_.noteOn(midi, vel01); }
void AudioSynthWindPipe::noteOff()                   { IrqMask m; voice_.noteOff(); }
void AudioSynthWindPipe::whistle(float midi, float breath) { IrqMask m; voice_.whistle(midi, breath); }
void AudioSynthWindPipe::setHalfband(bool on)        { voice_.setHalfband(on); }   // one byte: atomic as it is

// ---- words: compiled here, caller-side and unmasked; committed by flag -------------------------------
// ScorePlayer::commit withdraws its pending flag, copies into the buffer the
// audio side is not playing, then raises the flag; beginBlock() flips buffers
// only while the flag is up. update() can preempt any instant of that and
// sees either no commit or a complete one — so no mask, and the long part
// (the compile) never delays an audio block.

bool AudioSynthWindPipe::syllable(const char* phones)            { return voice_.syllable(phones); }
bool AudioSynthWindPipe::sing(const wp::Syllable* syls, int n)   { return voice_.sing(syls, n); }
bool AudioSynthWindPipe::sing(const wp::Syllable* syls, int n, float breath) { return voice_.sing(syls, n, breath); }
bool AudioSynthWindPipe::say(const char* phones)                 { return voice_.say(phones); }
bool AudioSynthWindPipe::speak(const char* phones)               { return voice_.speak(phones); }
bool AudioSynthWindPipe::speak(const char* phones, const wp::Speech& s) { return voice_.speak(phones, s); }
void AudioSynthWindPipe::stop()                                  { IrqMask m; voice_.stop(); }
bool AudioSynthWindPipe::busy() const                            { return voice_.busy(); }

// ---- readback: the same copy, the other way --------------------------------------------------------------

wp::BodyState AudioSynthWindPipe::state() const {
    IrqMask m;
    return voice_.state();
}

int AudioSynthWindPipe::diameters(float* out, int max) const {
    IrqMask m;
    return voice_.diameters(out, max);
}

// ---- the audio interrupt ---------------------------------------------------------------------------------------

void AudioSynthWindPipe::update(void) {
    {
        IrqMask m;                                // ~100 bytes (~240 with a tuning), sub-microsecond
        voice_.latch();
    }
    voice_.render(scratch_, AUDIO_BLOCK_SAMPLES);

    audio_block_t* block = allocate();
    if (!block) return;                           // rendered anyway: scores keep time
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
        float x = scratch_[i];
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        block->data[i] = (int16_t)(x * 32767.0f);
    }
    transmit(block);
    release(block);
}

#endif  // defined(ARDUINO)
