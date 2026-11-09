// wind_pipe — wp_pitch.h  (plan §6)
// the pitch axis: a target note eased in log frequency, plus vibrato and
// wobble in cents. one f0 per block; the glottis latches it per period.
#pragma once

#include "wp_config.h"
#include "wp_tuning.h"

namespace wp {

class PitchAxis {
public:
    void init(float fs, float midi = kDefaultMidi, const Tuning& t = Tuning());
    void retune(const Tuning& t);                         // block-rate: the vibrato fade and both drifts

    void setTarget(float midi)       { target_ = clampf(midi, 0.0f, 127.0f); spoken_ = false; spokenGlide_ = 0.0f; }
    // a spoken note: no vibrato, and it glides at its own rate (<= 0: the glide), until a note that is not
    void speakTarget(float midi, float glidePerSec) {
        target_ = clampf(midi, 0.0f, 127.0f);
        spoken_ = true;
        spokenGlide_ = glidePerSec > 0.0f ? approachCoef(glidePerSec, dt_) : 0.0f;
    }
    void setGlide(float perSec);                          // 0 = jump
    void setVibrato(float rateHz, float cents)  { vibRate_ = rateHz; vibCents_ = cents; }
    void setVibratoOnset(float sec)  { vibOnset_ = sec; }
    void setWobble(float amount01)   { wobble_ = amount01; }               // 1 is the tuning's full drift; past it is more
    void noteOn()                    { sinceOnset_ = 0.0f; vibDepth_ = 0.0f; }

    // once per block. silent = no breath: the first note out of silence jumps.
    float tick(Rng& rng, bool silent);

    float midi() const    { return midi_; }              // eased note, before vibrato and wobble
    float target() const  { return target_; }
    float vibrato() const { return vib_; }               // the vibrato's swing this block, in cents
    float f0() const      { return f0_; }

private:
    float dt_ = kBlock / kDefaultFs;
    float target_ = kDefaultMidi, midi_ = kDefaultMidi, f0_ = 130.81f;
    float glideCoef_ = 1.0f;
    bool  spoken_ = false;                                // speakTarget: the vibrato fades out, and out of silence is gone
    float spokenGlide_ = 0.0f;                            // its glide coefficient; 0 = the glide
    float vibRate_ = kDefaultVibRate, vibCents_ = kDefaultVibCents, vibOnset_ = kDefaultVibOnset;
    float vibPhase_ = 0.0f, vibDepth_ = 0.0f, vibFadeCoef_ = 1.0f, sinceOnset_ = 0.0f, vib_ = 0.0f;
    float wobble_ = kDefaultWobble;
    float slow_ = 0.0f, fast_ = 0.0f, slowCoef_ = 0.0f, fastCoef_ = 0.0f, slowNorm_ = 1.0f, fastNorm_ = 1.0f;
    float slowCents_ = 0.0f, fastCents_ = 0.0f;           // each drift's depth at wobble = 1
};

}  // namespace wp
