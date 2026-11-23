// monk_mouth — mm_tuning.h
// the tunables. a tunable changes who is singing, not what is being sung.
//
// every field here passes three tests. the outside world gives it no right
// answer: it is not a measurement, anatomy, or a published model. nothing
// else depends on it being what it is: not the fitted vowel table, a test
// gate, or an array size. and two different values would both be a good
// voice, just different voices. what fails them stays a constant, in
// mm_config.h, mm_vocab.cpp and mm_score.h.
//
// a voice is handed one of these whole (Voice::setTuning) and it lands at the
// top of the next block, like every other target. the field initializers are
// the defaults: a voice nobody tunes is exactly this one.
//
// most fields have no limit: a value that sounds broken is the caller's to
// find. clamped() holds only the few where a value past the line does not
// sound bad but ends the voice — a noise band that is no longer a filter, a
// pipe that gains energy, a click that grows — and turns a NaN or an
// infinity, which would end it too, back into the field's default.
#pragma once

namespace mm {

struct Tuning {
    // ---- larynx ---------------------------------------------------------------------
    float glottisRest       = 0.0f;     // how open the folds sit when voiced: standing breathiness
    float growl             = 0.0f;     // every other pulse this much weaker: an octave under the note, heard as growl
    // pink trombone's noise gains are 0.2 and 0.66. measured against an open
    // vowel they put /s/ 19 dB down and /h/ 30 dB down; speech has them near 12
    // and 18. these defaults were set by that measurement (mmtool trace).
    float aspirationGain    = 0.8f;     // the air in the voice, and how loud /h/ is
    float aspirationHz      = 500.0f;   // the breath's colour: low is chesty, high is airy
    float aspirationQ       = 0.5f;     // how narrow that colour is
    float noiseVoicedFloor  = 0.1f;     // breath noise always there under the voice
    float noiseVoicedPulse  = 0.2f;     // breath noise that pulses with each glottal cycle
    float noiseUnvoiced     = 0.3f;     // noise level once voicing is gone

    // ---- pipe -----------------------------------------------------------------------
    float glottalReflection = 0.75f;    // formant sharpness, throat end: low is lossy
    float lipReflection     = -0.85f;   // formant sharpness, mouth end: toward -1 rings
    float damp              = 0.998f;   // wall loss per section; pink trombone's 0.999 per 3.97 mm, squared
    float throatD           = 0.6f;     // the tube just above the larynx: narrow is twang
    float throatUpperD      = 1.1f;     // the step up into the pharynx
    float velumRest         = 0.0f;     // how open the nose sits when shut: standing nasality
    float lipRound          = 0.0f;     // how the lips hold a vowel: toward 1 every one pursed, toward -1 opened
    float gain              = 1.0f;     // the pipe's output level, ahead of the clamp: 1 as built

    // ---- consonants -----------------------------------------------------------------
    float turbulenceGain    = 1.5f;     // fricative loudness
    float fricationHz       = 1000.0f;  // hiss colour; raise first against lispy sibilants
    float fricationQ        = 0.5f;     // how narrow that colour is
    float voicedFrication   = 0.4f;     // how much a voiced fricative hisses next to a voiceless one
    float burstGain         = 0.3f;     // transient amplitude = this * pressure
    float burstHalfLife     = 0.005f;   // seconds a stop's click rings
    float diction           = 1.0f;     // consonant gesture speed: above 1 crisp, below 1 lazy

    // ---- motion: rates per second -----------------------------------------------------
    float rateBreathUp      = 60.0f;    // note attack
    float rateBreathDown    = 12.0f;    // note release
    float rateGlottis       = 50.0f;    // voicing on and off
    float rateTenseness     = 10.0f;
    float rateTongue        = 14.0f;    // vowel to vowel: low is slurred, high is snappy
    float rateLips          = 20.0f;
    float rateVelum         = 18.0f;

    // ---- pitch drift ------------------------------------------------------------------
    float vibFadeRate       = 6.0f;     // per second: vibrato depth fading in past its onset
    float wobbleSlowHz      = 0.5f;     // the slow drift's speed
    float wobbleFastHz      = 4.0f;     // the flutter's speed; tens of Hz is per-period jitter
    float wobbleSlowCents   = 30.0f;    // the slow drift's depth at wobble = 1
    float wobbleFastCents   = 10.0f;    // the flutter's depth at wobble = 1

    // ---- lips: the whistle ----------------------------------------------------------
    float breathPressure    = 1000.0f;  // Pa, lung pressure at breath 1: the jet's speed. only the jet reads it
    float jetLength         = 0.35f;    // cm, the pucker's depth: the jet's transit, which pitches lock at which breath
    float jetGain           = 0.1f;     // ms, how hard the jet amplifies a nudge: the lock threshold and the drive
};

// the few fields that have a limit, held inside it; a NaN or an infinity falls
// back to the field's default. fs bounds the two noise bands at half the sample rate.
Tuning clamped(const Tuning& t, float fs);

}  // namespace mm
