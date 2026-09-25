// wind_pipe — wp_tuning.cpp
#include "wp_tuning.h"

#include "wp_config.h"

namespace wp {
namespace {

// a band's q divides its coefficients: at zero and below the filter is not one
constexpr float kMinQ = 1e-6f;

inline float real(float x, float fallback) { return x == x ? x : fallback; }       // NaN
inline float lim(float x, float lo, float hi, float fallback) { return x == x ? clampf(x, lo, hi) : fallback; }
inline float atLeast(float x, float lo, float fallback) { return x == x ? (x < lo ? lo : x) : fallback; }

}  // namespace

Tuning clamped(const Tuning& in, float fs) {
    const Tuning d;                                       // the defaults: what a NaN falls back to
    const float nyquist = fs * 0.5f;
    Tuning t;

    t.glottisRest       = real(in.glottisRest, d.glottisRest);
    t.aspirationGain    = real(in.aspirationGain, d.aspirationGain);
    // a band below zero or past half the sample rate is an unstable filter
    t.aspirationHz      = lim(in.aspirationHz, 0.0f, nyquist, d.aspirationHz);
    t.aspirationQ       = atLeast(in.aspirationQ, kMinQ, d.aspirationQ);
    t.noiseVoicedFloor  = real(in.noiseVoicedFloor, d.noiseVoicedFloor);
    t.noiseVoicedPulse  = real(in.noiseVoicedPulse, d.noiseVoicedPulse);
    t.noiseUnvoiced     = real(in.noiseUnvoiced, d.noiseUnvoiced);

    // an end that reflects more than it receives, or a wall that adds, and the
    // pipe gains energy every round trip. either sign is a pipe; past 1 is not.
    t.glottalReflection = lim(in.glottalReflection, -1.0f, 1.0f, d.glottalReflection);
    t.lipReflection     = lim(in.lipReflection, -1.0f, 1.0f, d.lipReflection);
    t.damp              = lim(in.damp, -1.0f, 1.0f, d.damp);
    t.throatD           = real(in.throatD, d.throatD);
    t.throatUpperD      = real(in.throatUpperD, d.throatUpperD);
    t.velumRest         = real(in.velumRest, d.velumRest);

    t.turbulenceGain    = real(in.turbulenceGain, d.turbulenceGain);
    t.fricationHz       = lim(in.fricationHz, 0.0f, nyquist, d.fricationHz);
    t.fricationQ        = atLeast(in.fricationQ, kMinQ, d.fricationQ);
    t.voicedFrication   = real(in.voicedFrication, d.voicedFrication);
    t.burstGain         = real(in.burstGain, d.burstGain);
    // below zero a click grows instead of dying
    t.burstHalfLife     = atLeast(in.burstHalfLife, 0.0f, d.burstHalfLife);
    t.diction           = real(in.diction, d.diction);

    t.rateBreathUp      = real(in.rateBreathUp, d.rateBreathUp);
    t.rateBreathDown    = real(in.rateBreathDown, d.rateBreathDown);
    t.rateGlottis       = real(in.rateGlottis, d.rateGlottis);
    t.rateTenseness     = real(in.rateTenseness, d.rateTenseness);
    t.rateTongue        = real(in.rateTongue, d.rateTongue);
    t.rateLips          = real(in.rateLips, d.rateLips);
    t.rateVelum         = real(in.rateVelum, d.rateVelum);

    t.vibFadeRate       = real(in.vibFadeRate, d.vibFadeRate);
    t.wobbleSlowHz      = real(in.wobbleSlowHz, d.wobbleSlowHz);
    t.wobbleFastHz      = real(in.wobbleFastHz, d.wobbleFastHz);
    t.wobbleSlowCents   = real(in.wobbleSlowCents, d.wobbleSlowCents);
    t.wobbleFastCents   = real(in.wobbleFastCents, d.wobbleFastCents);

    // below zero the jet's speed is the root of a negative pressure
    t.breathPressure    = atLeast(in.breathPressure, 0.0f, d.breathPressure);
    t.jetLength         = real(in.jetLength, d.jetLength);
    t.jetGain           = real(in.jetGain, d.jetGain);
    return t;
}

}  // namespace wp
