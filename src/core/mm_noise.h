// monk_mouth — mm_noise.h  (plan §3)
// white xorshift32 into two RBJ bandpass biquads — aspiration and frication —
// plus the reference's noise modulator and turbulence window. nothing here
// calls libm per sample.
#pragma once

#include "mm_config.h"
#include "mm_tuning.h"

namespace mm {

// RBJ bandpass, constant 0 dB peak gain (what WebAudio's "bandpass" is).
class Bandpass {
public:
    void set(float fs, float hz, float q);
    void reset() { z1_ = z2_ = 0.0f; }
    inline float process(float x) {                       // transposed direct form II
        const float y = b0_ * x + z1_;
        z1_ = -a1_ * y + z2_;                             // b1 = 0
        z2_ = -b0_ * x - a2_ * y;                         // b2 = -b0
        return y;
    }
private:
    float b0_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

class Noise {
public:
    void init(float fs, const Tuning& t = Tuning());
    void retune(float fs, const Tuning& t);               // both bands' colour; the filters keep their state
    void reset() { asp_.reset(); fric_.reset(); }
    // one white sample (the reference's Math.random(), centred) through both bands
    inline void step(Rng& rng, float& aspiration, float& frication) {
        const float w = 0.5f * rng.bipolar();
        aspiration = asp_.process(w);
        frication = fric_.process(w);
    }
private:
    Bandpass asp_, fric_;
};

// Glottis.getNoiseModulator: noise pulses with the voice. t is the glottal
// phase; voicedness blends the pulsing (a floor plus a half-sine of depth
// pulse) toward the flat unvoiced level as voicing falls. the three levels are
// the tuning's (the reference's are 0.1, 0.2 and 0.3).
// sin(2 pi t) on [0, 0.5] is the parabola 16 t (0.5 - t) — no libm.
inline float noiseModulator(float t, float voicedness, float floor, float pulse, float unvoiced) {
    const float half = t < 0.5f ? 16.0f * t * (0.5f - t) : 0.0f;
    const float voiced = floor + pulse * half;
    return voicedness * voiced + (1.0f - voicedness) * unvoiced;
}

// the reference's turbulence window on the real section diameter:
// silent when shut, full by 0.03, gone by 0.4.
inline float turbulenceWindow(float d) {
    return clampf(30.0f * d, 0.0f, 1.0f) * clampf(8.0f * (0.4f - d), 0.0f, 1.0f);
}

}  // namespace mm
