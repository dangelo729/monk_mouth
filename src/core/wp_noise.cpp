// wind_pipe — wp_noise.cpp  (plan §3)
#include "wp_noise.h"

namespace wp {

void Bandpass::set(float fs, float hz, float q) {
    const float w0 = 2.0f * 3.14159265358979f * hz / fs;
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    b0_ = alpha / a0;
    a1_ = -2.0f * std::cos(w0) / a0;
    a2_ = (1.0f - alpha) / a0;
}

void Noise::init(float fs, const Tuning& t) {
    retune(fs, t);
    reset();
}

void Noise::retune(float fs, const Tuning& t) {
    asp_.set(fs, t.aspirationHz, t.aspirationQ);
    fric_.set(fs, t.fricationHz, t.fricationQ);
}

}  // namespace wp
