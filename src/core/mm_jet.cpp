// monk_mouth — mm_jet.cpp  (whistle plan §3, §4)
#include "mm_jet.h"

namespace mm {
namespace {

// the swing is read against at least this flow: 0.1 l/s, under a soft
// whistle's (0.35 l/s at the hand-found pose's threshold), over what a jet
// keeps while the folds are closing on a voice. a starved jet reads 0.
constexpr float kStarvedFlow = 1e-4f;       // m³/s
// the swing above which the crossings are the whistle's pitch, not noise
constexpr float kWhistleOn   = 0.3f;
// how much one period differs from the last, over the period: a lock holds
// its period to a fraction of a sample, a swing forced by hiss or a voice
// wanders by tens of percent. the whistle reads full under the first, none
// past the second.
constexpr float kJitterLocked = 0.02f;
constexpr float kJitterNone   = 0.1f;
constexpr float kPeakSec     = 0.005f;     // the swing's peak follower falls at this time constant
constexpr float kHzRate      = 50.0f;      // per second: whistleHz approaches its measure at ~20 ms

}  // namespace

void Jet::init(float fs) {
    fs_ = fs;
    const float pi = 3.14159265358979f;
    leak_ = std::exp(-2.0f * pi * kJetLeakHz / fs);
    lowpass_ = 1.0f - std::exp(-2.0f * pi * kJetLowpassHz / fs);
    peakDecay_ = std::exp(-1.0f / (kPeakSec * fs));
    hzCoef_ = approachCoef(kHzRate, (float)kBlock / fs);
    reset();
}

void Jet::reset() {
    for (int i = 0; i < kMaxJetDelay; ++i) line_[i] = 0.0f;
    pos_ = 0;
    drift_ = nudge_ = 0.0f;
    flow_ = dFlow_ = flowT_ = 0.0f;
    delay_ = delayT_ = 1.0f;
    dDelay_ = 0.0f;
    peak_ = prevD_ = lastFrac_ = lastPeriod_ = periodSum_ = jitterSum_ = 0.0f;
    since_ = kSinceCap;
    periods_ = 0;
    steady_ = whistle_ = whistleHz_ = 0.0f;
}

// land exactly on the last targets, then aim at the new ones: the tube's idiom.
void Jet::set(float flow, float delaySamples, float halfWidth, float lipArea) {
    flow_ = flowT_;
    flowT_ = flow;
    dFlow_ = (flowT_ - flow_) * kInvBlock;
    delay_ = delayT_;
    delayT_ = delaySamples;
    dDelay_ = (delayT_ - delay_) * kInvBlock;
    invHalfWidth_ = halfWidth > 0.0f ? 1.0f / halfWidth : 0.0f;
    velScale_ = lipArea > 0.0f ? kWaveToFlow / lipArea : 0.0f;
}

// the swing over its cap, while the period holds (a block with no crossing
// holds what it had), approached at ~20 ms; the pitch from the mean period,
// only from a block that swung and held its period.
void Jet::endBlock() {
    const float flow = flowT_ > kStarvedFlow ? flowT_ : kStarvedFlow;
    float swing = peak_ / (0.5f * flow);
    if (swing > 1.0f) swing = 1.0f;
    float held = 0.0f;                                     // this block's own: how well its period held
    if (periods_ > 0 && periodSum_ > 0.0f) {
        const float jitter = jitterSum_ / periodSum_;
        held = clampf((kJitterNone - jitter) / (kJitterNone - kJitterLocked), 0.0f, 1.0f);
        steady_ = approach(steady_, held, hzCoef_);
    }
    whistle_ = approach(whistle_, swing * steady_, hzCoef_);
    if (swing * held > kWhistleOn) {
        const float hz = fs_ * (float)periods_ / periodSum_;
        whistleHz_ = whistleHz_ > 0.0f ? approach(whistleHz_, hz, hzCoef_) : hz;   // the first measure lands
    }
    periodSum_ = jitterSum_ = 0.0f;
    periods_ = 0;
}

}  // namespace mm
