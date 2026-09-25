// wind_pipe — wp_glottis.h  (plan §2)
// the Liljencrants-Fant pulse with one knob, ported from Pink Trombone's
// Glottis.setupWaveform. the reference evaluates exp and sin per sample; here
// the same waveform runs as recurrences reseeded from the closed form every
// period, so error cannot accumulate and the per-sample path calls no libm.
//
//   open phase    E0 * e^(alpha t) * sin(omega t)  -> a damped rotation of (s, c)
//   return phase  (shift - u) / Delta              -> u decays geometrically
//
// the rotation is the coupled form, written as increments (s += a*s + b*c):
// the textbook direct form x[n+1] = c1 x[n] - c2 x[n-1] has its poles next to
// z = 1 at singing pitches and loses 1e-3 in float32 by 60 Hz; this form holds
// 1e-6 for the same handful of multiplies.
//
// one repair to the reference: its alpha comes from two area approximations,
// so its period carries a 3-9 % DC residual. the exact mean of the period is
// a closed form (LfShape::dc, one more cos per period) and is subtracted, so
// the source integrates to zero by construction.
//
// the closed form stays in the tree as the test oracle.
#pragma once

#include "wp_config.h"

namespace wp {

struct LfShape { float alpha, E0, epsilon, shift, Delta, Te, omega, dc; };

LfShape lfSetup(float tenseness);                  // Glottis.setupWaveform
float   lfClosedForm(const LfShape& s, float t);   // normalizedLFWaveform, t in [0, 1)

class Glottis {
public:
    void init(float fs);
    void reset();

    // per block. both latch at the next period boundary.
    void setTargets(float f0Hz, float tenseness) { f0Next_ = f0Hz; tenseNext_ = tenseness; }

    inline float step() {                          // one sample of the bare pulse
        float out;
        if (openLeft_ > 0) {
            --openLeft_;
            out = s_ - shape_.dc;
            const float ns = s_ + (ca_ * s_ + cb_ * c_);
            const float nc = c_ + (ca_ * c_ - cb_ * s_);
            s_ = ns; c_ = nc;
        } else {
            out = (shape_.shift - u_) * invDelta_ - shape_.dc;
            u_ += u_ * ud_;
        }
        t_ += dt_;
        if (t_ >= 1.0f) { t_ -= 1.0f; beginPeriod(); }   // keeps the remainder: pitch is exact
        return out;
    }

    float phase() const { return t_; }             // normalized time in the period
    float f0() const    { return f0_; }
    const LfShape& shape() const { return shape_; }

private:
    void beginPeriod();

    float fs_ = kDefaultFs;
    float f0_ = 110.0f, f0Next_ = 110.0f;
    float tense_ = kDefaultTenseness, tenseNext_ = kDefaultTenseness;
    LfShape shape_{};
    float t_ = 0.0f, dt_ = 0.0f;
    int   openLeft_ = 0;
    float s_ = 0.0f, c_ = 0.0f, ca_ = 0.0f, cb_ = 0.0f;
    float u_ = 0.0f, ud_ = 0.0f, invDelta_ = 1.0f;
};

}  // namespace wp
