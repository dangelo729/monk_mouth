// monk_mouth — mm_config.h
// constants, clamp, approach, xorshift32. what is fixed lives here: the
// engine's shape, the anatomy the vowel table was fitted in, the guards that
// keep the simulation correct, and the defaults of the live setters. whatever
// is a matter of who is singing is a tunable, in mm_tuning.h.
//
// src/core/ includes nothing but the C++ standard library.
#pragma once

#include <cmath>
#include <cstdint>

namespace mm {

// ---- engine shape ----------------------------------------------------------
constexpr int   kBlock           = 128;     // control block, samples (2.9 ms)
constexpr float kInvBlock        = 1.0f / 128.0f;
constexpr float kDefaultFs       = 44100.0f;
// a voice takes any size between these two, whole or not, and neither is
// acoustic: the nose junction joins two sections, and the arrays have to be
// some size. 16 to 26 (12.7 to 20.6 cm) are human throats.
constexpr int   kMinSections     = 2;
constexpr int   kMaxSections     = 64;      // 50.8 cm
constexpr int   kDefaultSections = 22;      // 17.5 cm, adult male
constexpr int   kMaxNose         = 41;      // round(64 * 28/44)

// ---- the tube -----------------------------------------------------------------
// the end reflections and the wall damping are tunables (mm_tuning.h).
// a section with no area holds no air, but in area-weighted waves whatever was
// inside when it shut stays trapped between two total reflections and leaks
// out (doubled) as a 50 ms thump. sections narrower than kNarrowArea lose
// more per sample than the tuning's damp, down to kDampClosed when shut: the
// ghost drains in ~10 ms.
constexpr float kDampClosed        = 0.98f;
constexpr float kNarrowArea        = 0.01f;    // d = 0.1: below every fricative
// a reflection held to 0.999 is a slit of diameter 0.034, and one section of
// slit passes the voice's low harmonics at -24 dB: not a closure. 1 - 1e-6
// is; the area floor keeps the junction's division safe.
constexpr float kMaxReflection     = 0.999999f;
constexpr float kMinArea           = 1e-6f;
constexpr float kOutputGain        = 0.25f;    // tube output -> [-1, 1]
constexpr float kSilenceEnergy     = 1e-9f;    // the silence gate

// ---- anatomy: the reference's landmarks, as fractions of tract length -------
// the reference is pink trombone, extras/reference/pinktrombone.html.
constexpr float kTongueXMin = 12.0f / 44.0f;   // 0.273
constexpr float kTongueXMax = 29.0f / 44.0f;   // 0.659
constexpr float kTongueDMin = 2.05f;           // the tongue at its highest
constexpr float kTongueDMax = 3.5f;
constexpr float kLipDMax    = 1.5f;
constexpr float kLipRoundD  = 0.45f;           // every vowel's lips at lipRound 1: UW's, the roundest
constexpr float kLipPlace   = 41.5f / 44.0f;   // centre of the lip region
// the reference shuts the velum to 0.01. a velar closure sits downstream of the
// nose junction, and through 0.01 a voiced G leaks to within 38 dB of its vowel —
// short of the 40 dB a stop owes. 0.006 is 9 dB tighter.
constexpr float kVelumShut  = 0.006f;
constexpr float kVelumOpen  = 0.4f;
constexpr float kNasalArea  = 0.05f;           // nose area at/above this: no bursts
constexpr float kClosureAim = -0.3f;           // closures aim below zero
constexpr float kBreathAim  = -0.05f;          // so does a breath of 0
constexpr float kSlotClearMargin = 0.5f;       // clear aims this far above the base shape

// ---- the whistle: a jet at the lips ---------------------------------------------
// the airflow is physical, so these are the physics' numbers, not choices.
constexpr float kAirDensity      = 1.2f;       // kg/m³: air
constexpr float kSoundSpeed      = 350.0f;     // m/s: the tube's own, 0.794 cm sections at 44.1 kHz
constexpr float kJetConvection   = 0.4f;       // a nudge rides the jet at this fraction of its speed (fletcher)
constexpr float kJetLeakHz       = 100.0f;     // the lips' air drifting slower than every whistle is no nudge: removed under this
constexpr float kJetLowpassHz    = 3000.0f;    // a jet amplifies no nudge much shorter than it is wide: over the whistle band, it fades
constexpr float kGlottisAreaShut = 0.03f;      // cm²: the folds' mean open area, voiced
constexpr float kGlottisAreaOpen = 0.6f;       // cm²: the folds abducted
constexpr float kJetMinArea      = 1e-3f;      // cm²: d = 0.032, the turbulence window's shut edge
constexpr int   kMaxJetDelay     = 64;         // samples, a power of two: 1.45 ms, jets down to ~17 m/s at 1 cm
constexpr float kWaveUnit        = 0.75f;      // Pa·m²: what one tube unit stands for; measured so a whistle sits near a sung vowel
constexpr float kDiameterCm      = 1.0f;       // one diameter unit is one centimetre
// a slot cut into the lip sections below the pucker is a slit against the
// teeth, and a slit makes no jet: measured, the narrowest lip section over the
// pucker is 0.1 in F, 0.2 in TH, 0.39 .. 0.78 in S (N 22 .. 48), 0.95 in a
// slot behind a pucker and 1 in a bare pucker. none at or under the first,
// all at or over the second.
constexpr float kJetSlitCut      = 0.8f;
constexpr float kJetSlitClear    = 0.93f;

// ---- dynamics: rates per second ------------------------------------------------
// the body's own rates are tunables (mm_tuning.h). these two are the free
// constrictions' rates when no consonant brings its own, before the tuning's
// diction scales them.
constexpr float kRateSlotClose   = 70.0f;
constexpr float kRateSlotRelease = 45.0f;

constexpr float kBurstLife       = 0.2f;    // seconds
constexpr int   kBurstPool       = 4;
constexpr float kPressureRateVoiced    = 20.0f;
constexpr float kPressureRateVoiceless = 80.0f;
constexpr float kPressureRateFall      = 100.0f;

// ---- pitch ----------------------------------------------------------------------
constexpr float kDefaultGlide     = 30.0f;  // per second, in log frequency
constexpr float kDefaultVibRate   = 5.5f;   // Hz
constexpr float kDefaultVibCents  = 25.0f;
constexpr float kDefaultVibOnset  = 0.25f;  // seconds
constexpr float kDefaultWobble    = 0.3f;
constexpr float kMinF0            = 20.0f;

// ---- defaults at construction ------------------------------------------------------
constexpr float kDefaultTenseness = 0.6f;
constexpr float kDefaultMidi      = 48.0f;
constexpr float kScoreBreath      = 0.8f;   // sing()/say() level when none was ever set

// ---- helpers ------------------------------------------------------------------------
constexpr float kPi = 3.14159265358979f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep01(float x) {
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// the one smoothing law: x += (target - x) * (1 - exp(-rate * dt)).
// the coefficient is computed per block, never per sample.
inline float approachCoef(float ratePerSec, float dt) {
    if (ratePerSec <= 0.0f) return 1.0f;               // 0 = jump
    return 1.0f - std::exp(-ratePerSec * dt);
}
inline float approach(float x, float target, float coef) { return x + (target - x) * coef; }

// one seeded xorshift32 per voice: same calls, same samples, bit for bit.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    inline uint32_t next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    inline float bipolar() {                           // [-1, 1)
        return (float)(int32_t)next() * (1.0f / 2147483648.0f);
    }
};

}  // namespace mm
