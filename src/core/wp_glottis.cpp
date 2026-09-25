// wind_pipe — wp_glottis.cpp  (plan §2)
#include "wp_glottis.h"

namespace wp {

// Glottis.setupWaveform, line for line. normalized to time = 1, Ee = 1.
LfShape lfSetup(float tenseness) {
    float Rd = 3.0f * (1.0f - tenseness);
    if (Rd < 0.5f) Rd = 0.5f;
    if (Rd > 2.7f) Rd = 2.7f;

    const float Ra = -0.01f + 0.048f * Rd;
    const float Rk = 0.224f + 0.118f * Rd;
    const float Rg = (Rk / 4.0f) * (0.5f + 1.2f * Rk) / (0.11f * Rd - Ra * (0.5f + 1.2f * Rk));

    const float Ta = Ra;
    const float Tp = 1.0f / (2.0f * Rg);
    const float Te = Tp + Tp * Rk;

    const float epsilon = 1.0f / Ta;
    const float shift = std::exp(-epsilon * (1.0f - Te));
    const float Delta = 1.0f - shift;                     // divide by this to scale RHS

    float RHSIntegral = (1.0f / epsilon) * (shift - 1.0f) + (1.0f - Te) * shift;
    RHSIntegral = RHSIntegral / Delta;

    const float totalLowerIntegral = -(Te - Tp) / 2.0f + RHSIntegral;
    const float totalUpperIntegral = -totalLowerIntegral;

    const float pi = 3.14159265358979f;
    const float omega = pi / Tp;
    const float s = std::sin(omega * Te);
    const float y = -pi * s * totalUpperIntegral / (Tp * 2.0f);
    const float z = std::log(y);
    const float alpha = z / (Tp / 2.0f - Te);
    const float E0 = -1.0f / (s * std::exp(alpha * Te));

    // not the reference: the exact mean of one period, to be subtracted.
    // open phase: integral of E0 e^(alpha t) sin(omega t) over 0..Te, using
    // E0 e^(alpha Te) = -1/s; return phase: RHSIntegral above.
    const float openIntegral = (-alpha + omega * std::cos(omega * Te) / s + E0 * omega)
                             / (alpha * alpha + omega * omega);

    LfShape sh;
    sh.dc = openIntegral + RHSIntegral;
    sh.alpha = alpha; sh.E0 = E0; sh.epsilon = epsilon; sh.shift = shift;
    sh.Delta = Delta; sh.Te = Te; sh.omega = omega;
    return sh;
}

float lfClosedForm(const LfShape& s, float t) {
    if (t > s.Te) return (-std::exp(-s.epsilon * (t - s.Te)) + s.shift) / s.Delta - s.dc;
    return s.E0 * std::exp(s.alpha * t) * std::sin(s.omega * t) - s.dc;
}

void Glottis::init(float fs) {
    fs_ = fs;
    reset();
}

void Glottis::reset() {
    t_ = 0.0f;
    beginPeriod();
}

// once per period: latch f0 and tenseness, rebuild the shape, and reseed both
// recurrences from the closed form at this period's own sample instants.
// a baker's dozen of libm calls, all of them here.
void Glottis::beginPeriod() {
    f0_ = clampf(f0Next_, kMinF0, fs_ * 0.125f);
    tense_ = clampf(tenseNext_, 0.0f, 1.0f);
    shape_ = lfSetup(tense_);
    dt_ = f0_ / fs_;
    invDelta_ = 1.0f / shape_.Delta;

    // samples of this period that fall in the open phase: t_ + n*dt <= Te
    const float t0 = t_;
    openLeft_ = t0 <= shape_.Te ? (int)std::floor((shape_.Te - t0) / dt_) + 1 : 0;

    // open phase: (s, c) = E0 e^(alpha t0) (sin, cos)(omega t0), then a damped
    // rotation by omega*dt per sample, as increments.
    const float env = shape_.E0 * std::exp(shape_.alpha * t0);
    s_ = env * std::sin(shape_.omega * t0);
    c_ = env * std::cos(shape_.omega * t0);
    const float gm1 = std::expm1(shape_.alpha * dt_);
    const float g = 1.0f + gm1;
    const float sh = std::sin(0.5f * shape_.omega * dt_);
    ca_ = gm1 - 2.0f * g * sh * sh;                       // g cos(theta) - 1
    cb_ = g * std::sin(shape_.omega * dt_);               // g sin(theta)

    // return phase, seeded at the first sample past Te
    const float tr = t0 + (float)openLeft_ * dt_;
    u_ = std::exp(-shape_.epsilon * (tr - shape_.Te));
    ud_ = std::expm1(-shape_.epsilon * dt_);
}

}  // namespace wp
