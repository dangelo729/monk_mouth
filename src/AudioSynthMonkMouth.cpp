// monk_mouth — AudioSynthMonkMouth.cpp
// the header self-guards on #if defined(ARDUINO), so on the host this
// translation unit is intentionally empty.
#include "AudioSynthMonkMouth.h"

#if defined(ARDUINO)

// the architectural address of the Floating-Point Default Status Control
// Register. the Teensy core defines it; keep a fallback so the wrapper never
// silently loses its denormal protection.
#ifndef SCB_FPDSCR
#define SCB_FPDSCR (*(volatile uint32_t*)0xE000EF3Cu)
#endif

namespace {

constexpr uint32_t kFlushToZero = 1u << 24;       // FPSCR / FPDSCR bit FZ

// everything masked, for a short copy. PRIMASK is saved and
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
// mm::kBlock = 128, the stock value — but nothing breaks if a build changes
// AUDIO_BLOCK_SAMPLES: mm::Voice::render runs control once per 128 samples
// internally whatever size it is called with, and latch() merges into the
// audio side's table, so one latch per update() is right at any block size.
static_assert(AUDIO_BLOCK_SAMPLES > 0, "AUDIO_BLOCK_SAMPLES must be positive");

// init-only, so it lives in flash. FZ makes denormals flush to zero. setting
// it in the thread's FPSCR alone would not cover update(): on ARMv7-M a new
// floating-point context in an exception is loaded from FPDSCR. 99_Benchmark's
// last phase watches for it: cpu should fall as the notes ring out, not rise.
FLASHMEM void AudioSynthMonkMouth::initFlushToZero() {
    static bool done = false;
    if (done) return;
    done = true;
    SCB_FPDSCR |= kFlushToZero;                                       // every interrupt context from here on
    __builtin_arm_set_fpscr(__builtin_arm_get_fpscr() | kFlushToZero); // and the current one (setup / loop)
}

AudioSynthMonkMouth::AudioSynthMonkMouth(float sections, uint32_t seed)
    : AudioStream(0, NULL), voice_(sections, seed, AUDIO_SAMPLE_RATE_EXACT) {
    initFlushToZero();
    voice_.setAutoLatch(false);                   // update() owns the latch, inside its mask
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) scratch_[i] = 0.0f;
}

// ---- setters: every call sets a target, atomically with respect to update() -----------------------

void AudioSynthMonkMouth::setBreath(float p01)        { IrqMask m; voice_.setBreath(p01); }
void AudioSynthMonkMouth::setPitch(float midi)        { IrqMask m; voice_.setPitch(midi); }
void AudioSynthMonkMouth::setGlottis(float open01)    { IrqMask m; voice_.setGlottis(open01); }
void AudioSynthMonkMouth::setTenseness(float t01)     { IrqMask m; voice_.setTenseness(t01); }
void AudioSynthMonkMouth::setTongue(float pos01, float height01) { IrqMask m; voice_.setTongue(pos01, height01); }
void AudioSynthMonkMouth::setLips(float aperture01)   { IrqMask m; voice_.setLips(aperture01); }
void AudioSynthMonkMouth::setVowel(uint8_t phone)     { IrqMask m; voice_.setVowel(phone); }
void AudioSynthMonkMouth::setVelum(float open01)      { IrqMask m; voice_.setVelum(open01); }
void AudioSynthMonkMouth::setConstriction(int slot, float place01, float degree) {
    IrqMask m; voice_.setConstriction(slot, place01, degree);
}
void AudioSynthMonkMouth::clearConstriction(int slot) { IrqMask m; voice_.clearConstriction(slot); }
void AudioSynthMonkMouth::setGlide(float perSec)      { IrqMask m; voice_.setGlide(perSec); }
void AudioSynthMonkMouth::setVibrato(float rateHz, float cents, float onsetSec) {
    IrqMask m; voice_.setVibrato(rateHz, cents, onsetSec);
}
void AudioSynthMonkMouth::setWobble(float amount01)   { IrqMask m; voice_.setWobble(amount01); }
void AudioSynthMonkMouth::setTuning(const mm::Tuning& t) { IrqMask m; voice_.setTuning(t); }
mm::Tuning AudioSynthMonkMouth::tuning() const        { return voice_.tuning(); }   // caller side only: no mask
void AudioSynthMonkMouth::setSections(float sections) { IrqMask m; voice_.setSections(sections); }
void AudioSynthMonkMouth::noteOn(float midi, float vel01) { IrqMask m; voice_.noteOn(midi, vel01); }
void AudioSynthMonkMouth::noteOff()                   { IrqMask m; voice_.noteOff(); }
void AudioSynthMonkMouth::whistle(float midi, float breath) { IrqMask m; voice_.whistle(midi, breath); }
void AudioSynthMonkMouth::whistleRange(float& lo, float& hi) const { voice_.whistleRange(lo, hi); }   // caller side only: no mask
void AudioSynthMonkMouth::setHalfband(bool on)        { voice_.setHalfband(on); }   // one byte: atomic as it is

// ---- words: compiled here, caller-side and unmasked; committed by flag -------------------------------
// ScorePlayer::commit withdraws its pending flag, copies into the buffer the
// audio side is not playing, then raises the flag; beginBlock() flips buffers
// only while the flag is up. update() can preempt any instant of that and
// sees either no commit or a complete one — so no mask, and the long part
// (the compile) never delays an audio block.

bool AudioSynthMonkMouth::syllable(const char* phones)            { return voice_.syllable(phones); }
bool AudioSynthMonkMouth::sing(const mm::Syllable* syls, int n)   { return voice_.sing(syls, n); }
bool AudioSynthMonkMouth::sing(const mm::Syllable* syls, int n, float breath) { return voice_.sing(syls, n, breath); }
bool AudioSynthMonkMouth::say(const char* phones)                 { return voice_.say(phones); }
bool AudioSynthMonkMouth::speak(const char* phones)               { return voice_.speak(phones); }
bool AudioSynthMonkMouth::speak(const char* phones, const mm::Speech& s) { return voice_.speak(phones, s); }
void AudioSynthMonkMouth::stop()                                  { IrqMask m; voice_.stop(); }
bool AudioSynthMonkMouth::busy() const                            { return voice_.busy(); }

// ---- readback: the same copy, the other way --------------------------------------------------------------

mm::BodyState AudioSynthMonkMouth::state() const {
    IrqMask m;
    return voice_.state();
}

int AudioSynthMonkMouth::diameters(float* out, int max) const {
    IrqMask m;
    return voice_.diameters(out, max);
}

// ---- the audio interrupt ---------------------------------------------------------------------------------------

void AudioSynthMonkMouth::update(void) {
    {
        IrqMask m;                                // at most 208 bytes (360 with a tuning), sub-microsecond
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
