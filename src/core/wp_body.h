// wind_pipe — wp_body.h  (plan §4, §5; whistle plan §2)
// the body: a pose eased toward its targets, turned into diameters once per
// block, and watched for the three things a tube cannot know about itself —
// where it hisses, when a closure lets go, and how hard.
//
// everything that moves obeys one law: x += (target - x) * (1 - exp(-rate dt)).
// the body has no events. a stop bursts because a closed section opened.
#pragma once

#include "wp_config.h"
#include "wp_tube.h"
#include "wp_tuning.h"

namespace wp {

// ---- anatomy (§4): a pure function of the pose -----------------------------------
// raw anatomy units: tongueX in [kTongueXMin, kTongueXMax], tongueD in
// [kTongueDMin, kTongueDMax], lipD in [0, 1.5], positions as fractions of tract
// length (glottis 0, lips 1). evaluated at section centres, so a pose means the
// same thing at every voice size.
struct Pose {
    float tongueX = 0.293f, tongueD = 2.43f;      // the reference's rest tongue (12.9 / 44)
    float lipD = kLipDMax;
    float velumD = kVelumShut;
    struct Slot { bool active = false; float place = 0.8f, degree = 1.5f; } slot[2];
};

// pose -> the layout's n diameters. optionally reports, per slot, the section
// nearest the lips that it narrowed (-1: none) and the widest base diameter
// within its reach, and the diameters a section past either end (the ends'
// pieces: the glottis', the lips'). the throat below the tongue is the
// tuning's. each region
// begins at its landmark, and a section near one is in each region in part,
// so a section crosses over smoothly as the length changes; at n = 22 every
// section is wholly in one region, exactly as the reference has it.
void shapeTract(const Pose& pose, const Layout& at, float* diameters,
                int* slotEnd = nullptr, float* slotBaseMax = nullptr,
                const Tuning& tuning = Tuning(), float* ends = nullptr);
// the same, at a length's own layout (layoutFor)
void shapeTract(const Pose& pose, float length, float* diameters,
                int* slotEnd = nullptr, float* slotBaseMax = nullptr,
                const Tuning& tuning = Tuning(), float* ends = nullptr);

// a vowel's lips (lipD > 0) as a singer who holds them lipRound would make
// them: its own at 0, pursed to kLipRoundD at 1, opened to kLipDMax at -1 —
// a straight line on a log scale, as an opening is heard, and on past either
// end. a vowel already rounder than kLipRoundD opens toward it, and the
// result is not held: past the lips' range is the body's to hold.
float vowelLips(float lipD, float lipRound);

// ---- dynamics (§5) ------------------------------------------------------------------
struct Channel {
    float x = 0.0f, target = 0.0f;
    float coefUp = 1.0f, coefDown = 1.0f;          // in force
    float defUp = 1.0f, defDown = 1.0f;            // the channel's own

    void init(float value, float rateUp, float rateDown, float dt) {
        x = target = value;
        coefUp = defUp = approachCoef(rateUp, dt);
        coefDown = defDown = approachCoef(rateDown, dt);
    }
    void init(float value) { init(value, 0.0f, 0.0f, 0.0f); }      // its rates come from a retune
    // the channel's own rates. one easing at them picks the new ones up at
    // once; one under a rate override keeps it until its next aim.
    void retune(float rateUp, float rateDown, float dt) {
        const bool own = coefUp == defUp && coefDown == defDown;
        defUp = approachCoef(rateUp, dt);
        defDown = approachCoef(rateDown, dt);
        if (own) { coefUp = defUp; coefDown = defDown; }
    }
    // rate > 0 overrides both directions until the next aim; else the defaults
    void aim(float t, float rate, float dt) {
        target = t;
        if (rate > 0.0f) coefUp = coefDown = approachCoef(rate, dt);
        else { coefUp = defUp; coefDown = defDown; }
    }
    void ease() { x += (target - x) * (target > x ? coefUp : coefDown); }
};

// what the sample loop needs from one block of body
struct BodyFrame {
    float voicing = 0.0f;        // gain on the glottal pulse
    float aspiration = 0.0f;     // gain on the aspiration band
    float voicedness = 0.0f;     // how much the noise pulses with the voice
    float turb[3] = {0, 0, 0};   // turbulence gain: slot 0, slot 1, lips
    int   turbIdx[3] = {1, 1, 1};
    float turbFrac[3] = {0, 0, 0};
    // the airflow through the lips (whistle plan §2), si units
    float jetFlow = 0.0f;        // m³/s: what the jet carries, after the nose's share
    float jetDelay = 1.0f;       // samples: a nudge's transit along the jet
    float jetHalfWidth = 0.0f;   // m: half the lip orifice
    float lipArea = 0.0f;        // m²: the lip orifice
};

class Body {
public:
    enum { kSlot0 = 0, kSlot1 = 1, kLipSource = 2, kSources = 3 };

    void init(float sections, float fs, const Tuning& t = Tuning());
    void retune(const Tuning& t);                                 // block-rate: rates, gains, the burst's decay
    void resize(float sections);                                  // block-rate: the same pose, at another length

    // ---- targets, raw units. rate <= 0 means the channel's default. -------------
    // glottis and velum are asked for from 0: the tuning's rest, not shut.
    void setBreath(float p01, float rate = 0.0f);
    void setGlottis(float open01, float rate = 0.0f);
    void setTenseness(float t01, float rate = 0.0f);
    void setTongue(float tongueX, float tongueD, float rate = 0.0f);
    void setLips(float lipD, float rate = 0.0f, bool vowel = false);   // <= 0 closes; a vowel's, lipRound reshapes
    void setVelum(float open01, float rate = 0.0f);
    void setConstriction(int slot, float place, float degree, float rate = 0.0f);  // degree <= 0 closes
    void clearConstriction(int slot, float rate = 0.0f);

    // ---- per block: ease, shape the tube, run the watchers -----------------------
    void tick(Tube& tube);
    const BodyFrame& frame() const { return frame_; }

    // ---- per sample: the release transients (one multiply each) -----------------
    inline void stepTransients(Tube& tube) {
        if (!liveTransients_) return;
        int live = 0;
        for (int i = 0; i < kBurstPool; ++i) {
            Transient& t = pool_[i];
            if (t.life <= 0) continue;
            tube.inject(t.section, t.amp);
            t.amp *= burstDecay_;
            --t.life;
            ++live;
        }
        liveTransients_ = live;
    }
    bool transientsAlive() const { return liveTransients_ > 0; }
    void killTransients();

    // ---- readback -------------------------------------------------------------------
    const Pose&  pose() const        { return pose_; }
    const float* diameters() const   { return d_; }
    const float* ends() const        { return ends_; }             // the ends' pieces: the glottis' and the lips'
    int   sections() const           { return n_; }                // whole: the diameters' count
    const Layout& layout() const     { return at_; }
    float breath() const             { return breath_.x; }
    float breathTarget() const       { return breath_.target; }
    float glottisOpen() const        { return glottis_.x; }
    float tenseness() const          { return tense_.x; }
    float pressure() const           { return pressure_; }
    float hiss() const               { return hiss_; }
    bool  closed() const             { return closed_; }
    int   bursts() const             { return bursts_; }        // transients fired, ever

private:
    struct Transient { int section = 0; float amp = 0.0f; int life = 0; };
    struct SlotDyn {
        bool  active = false, clearing = false;
        Channel degree, place;
        float baseMax = kLipDMax;
    };

    void fire(int section, float amplitude);
    int   sectionAt(float place) const;
    float diameterAt(float place) const;
    float glottisTarget() const { return lerpf(t_.glottisRest, 1.0f, glottisAsk_); }
    float velumTarget() const   { return lerpf(kVelumShut, kVelumOpen, lerpf(t_.velumRest, 1.0f, velumAsk_)); }
    float lipsTarget() const;

    Layout at_ = layoutFor(kDefaultSections);
    int   n_ = at_.n;
    float fs_ = kDefaultFs, dt_ = kBlock / kDefaultFs;
    Tuning t_;

    Channel breath_, glottis_, tense_, tongueX_, tongueD_, lips_, velum_;
    float glottisAsk_ = 0.0f, velumAsk_ = 0.0f;   // the last asks, 0..1: a retune re-aims them from its rests
    float lipsAsk_ = kLipDMax;                    // the last lips asked for, raw: a retune re-rounds a vowel's
    bool  lipsVowel_ = false;
    SlotDyn slot_[2];

    Pose  pose_;
    float d_[kMaxSections];
    float ends_[2] = {1.5f, 1.5f};
    float prevSource_[kSources];
    float pressure_ = 0.0f, hiss_ = 0.0f;
    bool  closed_ = false;
    int   bursts_ = 0;

    float coefPressFall_ = 0.0f;

    Transient pool_[kBurstPool];
    int   liveTransients_ = 0;
    float burstDecay_ = 0.0f;
    int   burstLife_ = 0;

    BodyFrame frame_;
};

}  // namespace wp
