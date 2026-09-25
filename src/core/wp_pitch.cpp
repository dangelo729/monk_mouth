// wind_pipe — wp_pitch.cpp  (plan §6)
#include "wp_pitch.h"

namespace wp {

void PitchAxis::init(float fs, float midi, const Tuning& t) {
    dt_ = (float)kBlock / fs;
    target_ = midi_ = midi;
    setGlide(kDefaultGlide);
    vibPhase_ = vibDepth_ = sinceOnset_ = 0.0f;
    slow_ = fast_ = 0.0f;
    f0_ = 440.0f * std::exp2((midi_ - 69.0f) / 12.0f);
    retune(t);
}

void PitchAxis::retune(const Tuning& t) {
    vibFadeCoef_ = approachCoef(t.vibFadeRate, dt_);

    // two one-pole-smoothed random walks. smoothing white noise this hard
    // leaves almost no variance, so each is scaled back up to unit deviation:
    // var(out) = var(in) * c / (2 - c), and uniform [-1, 1) has var 1/3.
    const float twoPi = 6.28318530717959f;
    slowCoef_ = approachCoef(twoPi * t.wobbleSlowHz, dt_);
    fastCoef_ = approachCoef(twoPi * t.wobbleFastHz, dt_);
    // a drift too slow for a float coefficient never moves: nothing to scale.
    slowNorm_ = slowCoef_ > 0.0f ? std::sqrt(3.0f * (2.0f - slowCoef_) / slowCoef_) : 0.0f;
    fastNorm_ = fastCoef_ > 0.0f ? std::sqrt(3.0f * (2.0f - fastCoef_) / fastCoef_) : 0.0f;
    slowCents_ = t.wobbleSlowCents;
    fastCents_ = t.wobbleFastCents;
}

void PitchAxis::setGlide(float perSec) { glideCoef_ = approachCoef(perSec, dt_); }

float PitchAxis::tick(Rng& rng, bool silent) {
    // the note: eased in log frequency, which is linear in midi
    if (silent) midi_ = target_;
    else midi_ = approach(midi_, target_, glideCoef_);

    // vibrato: a sine whose depth fades in once the onset delay has passed
    sinceOnset_ += dt_;
    vibDepth_ = approach(vibDepth_, sinceOnset_ >= vibOnset_ ? 1.0f : 0.0f, vibFadeCoef_);
    vibPhase_ += vibRate_ * dt_;
    vibPhase_ -= std::floor(vibPhase_);
    float cents = vibCents_ * vibDepth_ * std::sin(6.28318530717959f * vibPhase_);

    // wobble: what keeps a held note alive. always drawn, so the stream of
    // random numbers — and with it every sample — does not depend on the amount.
    slow_ = approach(slow_, rng.bipolar(), slowCoef_);
    fast_ = approach(fast_, rng.bipolar(), fastCoef_);
    cents += wobble_ * (slowCents_ * slowNorm_ * slow_ + fastCents_ * fastNorm_ * fast_);

    f0_ = 440.0f * std::exp2((midi_ - 69.0f + cents * 0.01f) / 12.0f);
    if (f0_ < kMinF0) f0_ = kMinF0;
    return f0_;
}

}  // namespace wp
