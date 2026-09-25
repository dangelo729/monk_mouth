// wind_pipe — wp_body.cpp  (plan §4, §5)
#include "wp_body.h"

#include "wp_noise.h"

namespace wp {

// ============================================================================
// anatomy
// ============================================================================

// region tests on section centres, exact in integers. the centre of section i
// of n is (2i+1)/(2n), and at n = 22 it lands exactly on the reference's odd
// landmarks (7, 17, 39 of 44) — a float compare there would be a coin toss.
//   centre >= l44/44   <=>   (2i+1) * 22 >= n * l44
static inline bool atOrPast(int i, int n, int l44) { return (2 * i + 1) * 22 >= n * l44; }

// the reference softens the tongue curve on sections 10, 37 and 38 of 44
// (x0.94, x0.94, x0.8). as a function of position: piecewise linear through
// those sections' centres, which reproduces the reference exactly at n = 44.
static float tongueSoften(float x) {
    const float c10 = 10.5f / 44.0f, c11 = 11.5f / 44.0f;
    const float c36 = 36.5f / 44.0f, c37 = 37.5f / 44.0f, c38 = 38.5f / 44.0f;
    if (x <= c10) return 0.94f;
    if (x < c11)  return lerpf(0.94f, 1.0f, (x - c10) / (c11 - c10));
    if (x <= c36) return 1.0f;
    if (x < c37)  return lerpf(1.0f, 0.94f, (x - c36) / (c37 - c36));
    if (x < c38)  return lerpf(0.94f, 0.8f, (x - c37) / (c38 - c37));
    return 0.8f;
}

// TractUI.handleTouches: a constriction is wide at the back, narrow at the front
static float slotWidth(float place) {
    const float back = 25.0f / 44.0f, front = 32.0f / 44.0f;
    const float wide = 10.0f / 44.0f, narrow = 5.0f / 44.0f;
    if (place < back) return wide;
    if (place >= front) return narrow;
    return lerpf(wide, narrow, (place - back) / (front - back));
}

void shapeTract(const Pose& pose, int n, float* d, int* slotEnd, float* slotBaseMax, const Tuning& tuning) {
    const float pi = 3.14159265358979f;
    const float fixedTongue = 2.0f + (pose.tongueD - 2.0f) / 1.5f;
    const float lipD = clampf(pose.lipD, 0.0f, kLipDMax);

    for (int i = 0; i < n; ++i) {
        const float x = ((float)i + 0.5f) / (float)n;
        float di;
        if (atOrPast(i, n, 10) && !atOrPast(i, n, 39)) {          // tongue
            const float t = 1.1f * pi * (pose.tongueX - x) / 0.5f;
            const float curve = (3.2f - fixedTongue) * std::cos(t) * tongueSoften(x);
            di = 1.5f - curve;
        } else if (!atOrPast(i, n, 7)) {                          // throat
            di = tuning.throatD;
        } else if (!atOrPast(i, n, 12)) {
            di = tuning.throatUpperD;
        } else {
            di = 1.5f;
        }
        if (atOrPast(i, n, 39) && lipD < di) di = lipD;           // lips
        d[i] = di;
    }

    // the two free constrictions, over whatever the tongue and lips left
    for (int j = 0; j < 2; ++j) {
        if (slotEnd) slotEnd[j] = -1;
        if (slotBaseMax) slotBaseMax[j] = 0.0f;
        const Pose::Slot& s = pose.slot[j];
        if (!s.active) continue;
        const float degree = s.degree > 0.0f ? s.degree : 0.0f;
        const float width = slotWidth(s.place);
        const float halfSection = 0.5f / (float)n;
        for (int i = 0; i < n; ++i) {
            const float x = ((float)i + 0.5f) / (float)n;
            const float rel = std::fabs(x - s.place) - halfSection;
            if (rel > width) continue;
            if (slotBaseMax && d[i] > slotBaseMax[j]) slotBaseMax[j] = d[i];
            if (degree >= d[i]) continue;
            const float shrink = rel <= 0.0f ? 0.0f : 0.5f * (1.0f - std::cos(pi * rel / width));
            d[i] = degree + (d[i] - degree) * shrink;
            if (slotEnd) slotEnd[j] = i;                          // walking toward the lips: the last one wins
        }
    }
}

// ============================================================================
// dynamics
// ============================================================================

void Body::init(int sections, float fs, const Tuning& t) {
    n_ = sections < kMinSections ? kMinSections : (sections > kMaxSections ? kMaxSections : sections);
    fs_ = fs;
    dt_ = (float)kBlock / fs;

    t_ = t;
    pose_ = Pose();
    breath_.init(0.0f);
    breath_.target = kBreathAim;
    glottisAsk_ = velumAsk_ = 0.0f;
    glottis_.init(glottisTarget());
    tense_.init(kDefaultTenseness);
    tongueX_.init(pose_.tongueX);
    tongueD_.init(pose_.tongueD);
    lips_.init(kLipDMax);
    velum_.init(velumTarget());
    for (int j = 0; j < 2; ++j) {
        slot_[j] = SlotDyn();
        slot_[j].degree.init(kLipDMax);
        slot_[j].place.init(0.8f);
    }

    shapeTract(pose_, n_, d_, nullptr, nullptr, t);
    for (int j = 0; j < kSources; ++j) prevSource_[j] = 10.0f;
    prevSource_[kLipSource] = kLipDMax;
    pressure_ = hiss_ = 0.0f;
    closed_ = false;
    bursts_ = 0;
    coefPressFall_ = approachCoef(kPressureRateFall, dt_);

    burstLife_ = (int)(kBurstLife * fs);
    killTransients();
    frame_ = BodyFrame();
    retune(t);
}

// init ends here too: a body is built by tuning it with the defaults.
void Body::retune(const Tuning& t) {
    t_ = t;
    breath_.retune(t.rateBreathUp, t.rateBreathDown, dt_);
    glottis_.retune(t.rateGlottis, t.rateGlottis, dt_);
    tense_.retune(t.rateTenseness, t.rateTenseness, dt_);
    tongueX_.retune(t.rateTongue, t.rateTongue, dt_);
    tongueD_.retune(t.rateTongue, t.rateTongue, dt_);
    lips_.retune(t.rateLips, t.rateLips, dt_);
    velum_.retune(t.rateVelum, t.rateVelum, dt_);
    for (int j = 0; j < 2; ++j) {                                 // diction: the consonant gestures' speed
        slot_[j].degree.retune(kRateSlotRelease * t.diction, kRateSlotClose * t.diction, dt_);   // up = releasing, down = closing
        slot_[j].place.retune(kRateSlotClose * t.diction, kRateSlotClose * t.diction, dt_);
    }
    glottis_.target = glottisTarget();                            // the same asks, from the new rests
    velum_.target = velumTarget();
    burstDecay_ = std::exp2(-1.0f / (t.burstHalfLife * fs_));
}

void Body::setBreath(float p01, float rate) {
    breath_.aim(p01 > 0.0f ? p01 : kBreathAim, rate, dt_);         // a breath of 0 arrives; past 1 is only louder
}
void Body::setGlottis(float open01, float rate) {
    glottisAsk_ = clampf(open01, 0.0f, 1.0f);
    glottis_.aim(glottisTarget(), rate, dt_);
}
// the voicing gain is a root of tenseness: below zero it has no value. past 1
// the pulse has stopped changing and the voice only gets louder.
void Body::setTenseness(float t01, float rate)   { tense_.aim(t01 > 0.0f ? t01 : 0.0f, rate, dt_); }
void Body::setTongue(float tongueX, float tongueD, float rate) {
    tongueX_.aim(clampf(tongueX, kTongueXMin, kTongueXMax), rate, dt_);
    tongueD_.aim(clampf(tongueD, kTongueDMin, kTongueDMax), rate, dt_);
}
void Body::setLips(float lipD, float rate) {
    lips_.aim(lipD > 0.0f ? clampf(lipD, 0.0f, kLipDMax) : kClosureAim, rate, dt_);
}
void Body::setVelum(float open01, float rate) {
    velumAsk_ = clampf(open01, 0.0f, 1.0f);
    velum_.aim(velumTarget(), rate, dt_);
}

float Body::diameterAt(float place) const {
    int i = (int)(place * (float)n_);
    if (i < 0) i = 0;
    if (i > n_ - 1) i = n_ - 1;
    return d_[i];
}

void Body::setConstriction(int slot, float place, float degree, float rate) {
    if (slot < 0 || slot > 1) return;
    SlotDyn& s = slot_[slot];
    place = clampf(place, 0.0f, 1.0f);
    if (!s.active) {
        // start from the tube's current diameter at the place: no dead travel
        s.active = true;
        s.degree.x = diameterAt(place);
        s.place.x = place;
    }
    s.clearing = false;
    s.place.aim(place, rate, dt_);
    s.degree.aim(degree > 0.0f ? degree : kClosureAim, rate, dt_);
}

void Body::clearConstriction(int slot, float rate) {
    if (slot < 0 || slot > 1) return;
    SlotDyn& s = slot_[slot];
    if (!s.active) return;
    s.clearing = true;
    s.degree.aim(s.baseMax + kSlotClearMargin, rate, dt_);        // re-aimed each block
}

void Body::fire(int section, float amplitude) {
    if (amplitude <= 0.0f) return;
    int best = 0;
    for (int i = 0; i < kBurstPool; ++i) {
        if (pool_[i].life <= 0) { best = i; break; }
        if (pool_[i].life < pool_[best].life) best = i;           // pool full: retire the oldest
    }
    pool_[best].section = section;
    pool_[best].amp = amplitude;
    pool_[best].life = burstLife_;
    liveTransients_ = kBurstPool;                                 // recounted on the next sample
    ++bursts_;
}

void Body::killTransients() {
    for (int i = 0; i < kBurstPool; ++i) pool_[i] = Transient();
    liveTransients_ = 0;
}

void Body::tick(Tube& tube) {
    // ---- every channel approaches its target ---------------------------------------
    breath_.ease();   if (breath_.x < 0.0f) breath_.x = 0.0f;
    glottis_.ease();
    tense_.ease();
    tongueX_.ease();
    tongueD_.ease();
    lips_.ease();     if (lips_.x < 0.0f) lips_.x = 0.0f;         // closures arrive, and hold at 0
    velum_.ease();
    for (int j = 0; j < 2; ++j) {
        SlotDyn& s = slot_[j];
        if (!s.active) continue;
        if (s.clearing) s.degree.target = s.baseMax + kSlotClearMargin;
        s.degree.ease();
        if (s.degree.x < 0.0f) s.degree.x = 0.0f;
        s.place.ease();
    }

    // ---- anatomy: pose -> diameters -> the tube ---------------------------------------
    pose_.tongueX = tongueX_.x;
    pose_.tongueD = tongueD_.x;
    pose_.lipD = lips_.x;
    pose_.velumD = velum_.x;
    for (int j = 0; j < 2; ++j) {
        pose_.slot[j].active = slot_[j].active;
        pose_.slot[j].place = slot_[j].place.x;
        pose_.slot[j].degree = slot_[j].degree.x;
    }
    int end[2];
    float baseMax[2];
    shapeTract(pose_, n_, d_, end, baseMax, t_);
    for (int j = 0; j < 2; ++j) {
        SlotDyn& s = slot_[j];
        if (!s.active) continue;
        s.baseMax = baseMax[j];
        if (s.clearing && end[j] < 0) {                           // it has cleared the base shape
            s.active = s.clearing = false;
            pose_.slot[j].active = false;
        }
    }
    tube.setShape(d_, pose_.velumD);

    // ---- watchers: the three sources -----------------------------------------------------
    // front: the first section past a source's narrowing, toward the lips —
    // where its burst goes. the lips narrow to the end of the tube, so theirs
    // is the last section.
    float src[kSources], place[kSources];
    int front[kSources];
    for (int j = 0; j < 2; ++j) {
        src[j] = slot_[j].active ? slot_[j].degree.x : 10.0f;
        place[j] = slot_[j].place.x;
        if (end[j] < 0) {                                         // narrowing nothing: its own place
            const int i = (int)(place[j] * (float)n_);
            front[j] = i < 0 ? 0 : (i > n_ - 1 ? n_ - 1 : i);
        } else {
            front[j] = end[j] < n_ - 1 ? end[j] + 1 : n_ - 1;
        }
    }
    src[kLipSource] = lips_.x;
    place[kLipSource] = kLipPlace;
    front[kLipSource] = n_ - 1;

    const bool nasal = tube.noseArea() >= kNasalArea;
    const float glottisOpen = glottis_.x;

    // releases: a source whose eased degree crosses from <= 0 to > 0. nasals don't burst.
    // the burst goes into the front cavity, never the section that just opened:
    // the tube eases that one's reflections open across this block, and an
    // injection still held between two near-total reflections leaks out
    // doubled at each face, as a spike.
    for (int j = 0; j < kSources; ++j) {
        if (prevSource_[j] <= 0.0f && src[j] > 0.0f && !nasal)
            fire(front[j], t_.burstGain * pressure_);
        prevSource_[j] = src[j];
    }

    // pressure: builds behind a closure while the velum is shut. it only scales bursts.
    closed_ = src[0] <= 0.0f || src[1] <= 0.0f || src[2] <= 0.0f;
    if (closed_ && !nasal) {
        const float rate = lerpf(kPressureRateVoiced, kPressureRateVoiceless, glottisOpen);
        pressure_ = approach(pressure_, breath_.x, approachCoef(rate, dt_));
    } else {
        pressure_ = approach(pressure_, 0.0f, coefPressFall_);
    }

    // turbulence: each source hisses through its own window, and only while air
    // can get past the other two — and is not leaving through the nose instead
    // (the same rule that keeps nasals from bursting keeps them from hissing).
    const float oral = 1.0f - clampf(tube.noseArea() / kNasalArea, 0.0f, 1.0f);
    const float flow = t_.turbulenceGain * breath_.x * lerpf(t_.voicedFrication, 1.0f, glottisOpen) * oral;
    hiss_ = 0.0f;
    for (int j = 0; j < kSources; ++j) {
        float g = turbulenceWindow(src[j]);
        for (int o = 0; o < kSources; ++o)
            if (o != j) g *= clampf(30.0f * src[o], 0.0f, 1.0f);
        if (g > hiss_) hiss_ = g;
        frame_.turb[j] = g * flow;

        // the two sections downstream of the place, split by fractional index
        const float index = place[j] * (float)n_ - 0.5f;
        int i = (int)std::floor(index);
        frame_.turbFrac[j] = index - (float)i;
        if (i < 0) i = 0;
        if (i > n_ - 2) i = n_ - 2;
        frame_.turbIdx[j] = i + 1;                                // and i + 2, clamped by the caller
    }
    hiss_ *= breath_.x * oral;

    // ---- the larynx -------------------------------------------------------------------------
    const float tense = tense_.x;
    const float shut = 1.0f - smoothstep01(glottisOpen);
    frame_.voicing = breath_.x * std::sqrt(std::sqrt(tense)) * shut;
    frame_.voicedness = tense * breath_.x * shut;
    frame_.aspiration = breath_.x * lerpf(1.0f - std::sqrt(tense), 1.0f, glottisOpen) * t_.aspirationGain;
}

}  // namespace wp
