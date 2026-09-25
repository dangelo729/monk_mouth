// wind_pipe — wp_jet.h  (whistle plan §3, §4)
// a jet at the lips. the air in the lip section moves with the cavity's ring;
// its motion nudges the jet as it leaves, the nudge rides the jet and
// arrives a transit later as a change in how much air the jet delivers. when
// the transit matches the ring the delivery feeds it, and it grows until the
// jet swings as far as it can: the lock.
//
//   q     = tube flow * kWaveToFlow            m³/s  volume velocity in the lip section
//   v     = q / Al - its drift under kJetLeakHz m/s   the air's velocity through the lips
//   n     = v, lowpassed at kJetLowpassHz      m/s   the nudge the jet can carry
//   d     = n, delayed by the transit          m/s   linear interpolation; the delay ramps across the block
//   eta   = jetGain * d                        m     the jet's displacement where it lands (jetGain in ms)
//   drive = -0.5 Uj tanh~(eta / halfW)         m³/s  the flow the jet adds; odd, so zero-mean
//   out   = drive * kFlowToWave                      across the last section, as a dipole (Tube::injectFlow)
//
// four departures from the plan's §3, all measured (whistle plan §11, phase
// geometry): the jet is nudged by the air's velocity, not its displacement —
// the displacement's 1/f tilt made every pose lock at 80 .. 200 Hz, far under
// its lip mode; the drive enters as a pressure difference across the lip
// section, which the tube carries as flow (plus half into R, minus half into
// L), not as a volume source (half and half) — the open end's radiation shorts
// a volume source and it barely rings the front cavity; the drive opposes the nudge,
// which locks on the lip mode at three times as many poses as the other
// sign; and the nudge is lowpassed, the loop filter of cook's flute — without
// it the lip channel's own half-wave mode (7 .. 9 kHz) locks under the lip
// mode's threshold, and a labiodental F or TH whistles there.
//
// the telemetry (whistle plan §4): whistle is the swing over its cap, as the
// plan has it, weighted by how well the period holds from one crossing to the
// next — a swing forced by hiss or by the voice is not a whistle, and without
// the weight a sung line read up to 0.5.
//
// shaped like the glottis: block-rate targets, a sample-rate step. no libm on
// the sample path, no heap. the saturation caps |drive| at Uj / 2, so the
// loop cannot run away.
#pragma once

#include <cstdint>

#include "wp_config.h"

namespace wp {

// one tube unit is kWaveUnit of pressure-area. the two conversions multiply
// to a pure 1/2, so the loop gain does not depend on the calibration.
constexpr float kWaveToFlow = kWaveUnit / (kAirDensity * kSoundSpeed);
constexpr float kFlowToWave = kAirDensity * kSoundSpeed / (2.0f * kWaveUnit);

// x (27 + x²) / (27 + 9 x²): tanh to within 0.024, exactly ±1 past |x| = 3.
inline float jetTanh(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

class Jet {
public:
    void init(float fs);
    void reset();                                  // zero every state: the line, the drift, the telemetry
    void setGain(float jetGainMs) { gain_ = -1e-3f * jetGainMs; }   // the tuning's, in ms; the drive's sign folded in

    // per block: the body's airflow. flow and delay ramp across the next kBlock samples.
    void set(float flow, float delaySamples, float halfWidth, float lipArea);
    // per block, after its samples: the telemetry
    void endBlock();

    // one sample: the tube's flow in the lip section in, the flow to inject there out (tube units)
    inline float step(float flowIn) {
        flow_ += dFlow_;
        delay_ += dDelay_;

        const float v = flowIn * velScale_;
        drift_ = drift_ * leak_ + v * (1.0f - leak_);
        nudge_ += (v - drift_ - nudge_) * lowpass_;
        line_[pos_ & kMask] = nudge_;
        const int i0 = (int)delay_;                        // delay in [1, kMaxJetDelay - 2]
        const float fr = delay_ - (float)i0;
        const float a = line_[(pos_ - (unsigned)i0) & kMask];
        const float b = line_[(pos_ - (unsigned)i0 - 1u) & kMask];
        ++pos_;
        const float d = a + (b - a) * fr;

        const float th = jetTanh(gain_ * d * invHalfWidth_);
        const float drive = 0.5f * flow_ * th;

        // telemetry: the swing's peak, and positive-going zero crossings of d
        const float mag = drive < 0.0f ? -drive : drive;
        peak_ = mag > peak_ ? mag : peak_ * peakDecay_;
        if (since_ < kSinceCap) ++since_;
        if (prevD_ < 0.0f && d >= 0.0f) {
            const float frac = prevD_ / (prevD_ - d);      // the crossing, between the last sample and this one
            if (since_ < kSinceCap) {
                const float period = (float)since_ + frac - lastFrac_;
                const float jump = lastPeriod_ > 0.0f ? period - lastPeriod_ : 0.0f;
                jitterSum_ += jump < 0.0f ? -jump : jump;
                periodSum_ += period;
                ++periods_;
                lastPeriod_ = period;
            } else {
                lastPeriod_ = 0.0f;
            }
            since_ = 0;
            lastFrac_ = frac;
        }
        prevD_ = d;

        return drive * kFlowToWave;
    }

    float whistle() const   { return whistle_; }      // 0..1: the swing over its cap, while its period holds
    float whistleHz() const { return whistleHz_; }    // the swing's pitch, held while the whistle is down

    // state, for tests
    float drift() const        { return drift_; }
    float nudge() const        { return nudge_; }
    float peak() const         { return peak_; }
    float flow() const         { return flow_; }

private:
    static constexpr unsigned kMask = (unsigned)kMaxJetDelay - 1u;
    static_assert((kMaxJetDelay & (kMaxJetDelay - 1)) == 0, "the delay line is a power of two");
    static constexpr int kSinceCap = 1 << 20;

    float fs_ = kDefaultFs;
    float leak_ = 1.0f, lowpass_ = 1.0f, peakDecay_ = 0.0f, hzCoef_ = 1.0f;
    float gain_ = 0.0f;

    float flow_ = 0.0f, dFlow_ = 0.0f, flowT_ = 0.0f;
    float delay_ = 1.0f, dDelay_ = 0.0f, delayT_ = 1.0f;
    float invHalfWidth_ = 0.0f, velScale_ = 0.0f;

    float line_[kMaxJetDelay];
    unsigned pos_ = 0;
    float drift_ = 0.0f, nudge_ = 0.0f;

    float peak_ = 0.0f, prevD_ = 0.0f, lastFrac_ = 0.0f, lastPeriod_ = 0.0f;
    float periodSum_ = 0.0f, jitterSum_ = 0.0f;
    int   since_ = kSinceCap, periods_ = 0;
    float steady_ = 0.0f, whistle_ = 0.0f, whistleHz_ = 0.0f;
};

}  // namespace wp
