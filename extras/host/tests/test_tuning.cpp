// wind_pipe — tests/test_tuning.cpp
// the tunables: a default tuning is no tuning at all, every field is wired to
// something, nothing is limited but what would end the voice, and the three
// behaviours the tuning adds — a resting glottis, a resting velum, diction —
// do what they say.
#include <cmath>
#include <cstring>
#include <vector>

#include "core/wp_voice.h"
#include "wp_analysis.h"
#include "wp_test.h"

using namespace wp;

namespace {

// a Tuning is nothing but floats, so a test can walk its fields by index.
constexpr int kFields = (int)(sizeof(Tuning) / sizeof(float));
static_assert(sizeof(Tuning) == kFields * sizeof(float), "Tuning is floats and nothing else");
static_assert(kFields == 32, "a new tunable: name it in kFieldNames below");

const char* const kFieldNames[kFields] = {
    "glottisRest", "aspirationGain", "aspirationHz", "aspirationQ", "noiseVoicedFloor", "noiseVoicedPulse",
    "noiseUnvoiced", "glottalReflection", "lipReflection", "damp", "throatD", "throatUpperD", "velumRest",
    "turbulenceGain", "fricationHz", "fricationQ", "voicedFrication", "burstGain", "burstHalfLife", "diction",
    "rateBreathUp", "rateBreathDown", "rateGlottis", "rateTenseness", "rateTongue", "rateLips", "rateVelum",
    "vibFadeRate", "wobbleSlowHz", "wobbleFastHz", "wobbleSlowCents", "wobbleFastCents",
};

float field(const Tuning& t, int i) {
    float f[kFields];
    std::memcpy(f, &t, sizeof t);
    return f[i];
}
void setField(Tuning& t, int i, float v) {
    float f[kFields];
    std::memcpy(f, &t, sizeof t);
    f[i] = v;
    std::memcpy(&t, f, sizeof t);
}
Tuning filled(float v) {
    Tuning t;
    for (int i = 0; i < kFields; ++i) setField(t, i, v);
    return t;
}

// a line that leans on every part of a voice: voiceless and voiced hiss, a
// nasal, a stop, an aspirate, rounded lips, vibrato on held notes, wobble, a
// tenseness move half way through — and then a note played by hand, because
// only a live breath rises and falls at the body's own rates.
const Syllable kLine[] = {{"S AA", 48.0f, 450}, {"Z UW M", 50.0f, 600}, {"B IY", 52.0f, 500}, {"HH AH", 50.0f, 450}};
const int kLineCount = 4;

std::vector<float> singLine(Voice& v) {
    const int line = (int)(2.6f * kDefaultFs), note = (int)(0.3f * kDefaultFs), tail = (int)(0.5f * kDefaultFs);
    std::vector<float> x((size_t)(line + note + tail));
    v.sing(kLine, kLineCount);
    v.render(x.data(), line / 2);
    v.setTenseness(0.9f);
    v.render(x.data() + line / 2, line - line / 2);
    v.noteOn(47.0f, 0.8f);
    v.render(x.data() + line, note);
    v.noteOff();
    v.render(x.data() + line + note, tail);
    return x;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

struct Rig {
    Tube tube;
    Body body;
    explicit Rig(const Tuning& t = Tuning()) : tube(kDefaultSections) { body.init(kDefaultSections, kDefaultFs, t); }
    void run(int blocks) { for (int i = 0; i < blocks; ++i) body.tick(tube); }
};

}  // namespace

// a voice handed the defaults is the voice nobody tuned: the same samples.
WP_TEST(tuning_default_is_no_tuning) {
    for (int n : {22, 16, 26}) {
        Voice plain(n, 5), tuned(n, 5);
        tuned.setTuning(Tuning());
        CHECK(same(singLine(plain), singLine(tuned)));
    }
}

// the same tuning set again in the middle of a note changes no sample: a
// retune moves numbers, it resets nothing.
WP_TEST(tuning_retune_resets_nothing) {
    Tuning t;
    t.glottisRest = 0.2f; t.lipReflection = -0.9f; t.rateTongue = 20.0f; t.diction = 1.4f; t.wobbleFastHz = 9.0f;
    Voice a(22, 9), b(22, 9);
    a.setTuning(t);
    b.setTuning(t);
    std::vector<float> xa(44100), xb(44100);
    a.sing(kLine, kLineCount);
    b.sing(kLine, kLineCount);
    a.render(xa.data(), 11025);
    b.render(xb.data(), 11025);
    for (int k = 0; k < 3; ++k) {                          // mid consonant, mid vowel, wherever these land
        b.setTuning(b.tuning());
        a.render(xa.data() + 11025 * (k + 1), 11025);
        b.render(xb.data() + 11025 * (k + 1), 11025);
    }
    CHECK(same(xa, xb));
}

// every field, moved, changes what comes out: no tunable is wired to nothing.
WP_TEST(tuning_every_field_is_heard) {
    Voice ref(22, 5);
    const std::vector<float> base = singLine(ref);
    const Tuning d;
    for (int i = 0; i < kFields; ++i) {
        Tuning t;
        const float was = field(d, i);
        setField(t, i, was == 0.0f ? 0.2f : was * 1.3f);
        Voice v(22, 5);
        v.setTuning(t);
        CHECK_MSG(field(v.tuning(), i) != was, "%s did not move", kFieldNames[i]);
        CHECK_MSG(!same(base, singLine(v)), "%s changes nothing", kFieldNames[i]);
    }
}

namespace {

// the fields that have a limit, and the limit: past it the voice does not
// sound bad, it ends. every other field is handed through as given.
struct Limit { const char* name; float lo, hi; };
const float kOpen = 1e30f;                                              // no limit on that side
const Limit kLimits[] = {
    {"aspirationHz", 0.0f, kDefaultFs * 0.5f}, {"fricationHz", 0.0f, kDefaultFs * 0.5f},
    {"aspirationQ", 1e-6f, kOpen},             {"fricationQ", 1e-6f, kOpen},
    {"glottalReflection", -1.0f, 1.0f},        {"lipReflection", -1.0f, 1.0f},  {"damp", -1.0f, 1.0f},
    {"burstHalfLife", 0.0f, kOpen},
};
const Limit* limitOf(int i) {
    for (const Limit& l : kLimits) if (!std::strcmp(l.name, kFieldNames[i])) return &l;
    return nullptr;
}
// only the limited fields pushed to v; the rest left at their defaults
Tuning limitedAt(float v) {
    Tuning t;
    for (int i = 0; i < kFields; ++i) if (limitOf(i)) setField(t, i, v);
    return t;
}

}  // namespace

// a field with no limit is handed through as given, however far out; one with
// a limit stops exactly there; NaN falls back to the default; and what a voice
// reports is what it was held to.
WP_TEST(tuning_limits_only_what_ends_the_voice) {
    const Tuning lo = clamped(filled(-kOpen), kDefaultFs), hi = clamped(filled(kOpen), kDefaultFs);
    const Tuning d;
    int limited = 0;
    for (int i = 0; i < kFields; ++i) {
        const Limit* l = limitOf(i);
        if (l) ++limited;
        const float wantLo = l ? l->lo : -kOpen, wantHi = l ? l->hi : kOpen;
        CHECK_MSG(field(lo, i) == wantLo, "%s: floor %.6g, expected %.6g", kFieldNames[i], field(lo, i), wantLo);
        CHECK_MSG(field(hi, i) == wantHi, "%s: ceiling %.6g, expected %.6g", kFieldNames[i], field(hi, i), wantHi);
        if (l) CHECK_MSG(l->lo <= field(d, i) && field(d, i) <= l->hi, "%s: default outside its own limit", kFieldNames[i]);
    }
    CHECK(limited == (int)(sizeof kLimits / sizeof kLimits[0]));         // every name above is a real field

    const Tuning nan = clamped(filled(std::nanf("")), kDefaultFs);
    for (int i = 0; i < kFields; ++i) CHECK_MSG(field(nan, i) == field(d, i), "%s: NaN got through", kFieldNames[i]);

    Voice v(22, 1);
    v.setTuning(filled(kOpen));
    const Tuning heldTo = v.tuning();
    CHECK(std::memcmp(&hi, &heldTo, sizeof hi) == 0);
}

// every limited field pushed far below its floor, far above its ceiling, and
// the most resonant pipe the limits allow (both ends and the walls lossless):
// loud, but finite, inside [-1, 1], the tube's energy under a fixed bound —
// and the same voice sings again once it is handed the defaults.
WP_TEST(tuning_survives_its_limits) {
    Tuning ringing;
    ringing.glottalReflection = kOpen; ringing.lipReflection = -kOpen; ringing.damp = kOpen;
    Tuning cases[] = {limitedAt(-kOpen), limitedAt(kOpen), ringing};
    const char* const names[] = {"floor", "ceiling", "ringing"};
    for (int n : {22, 16, 26})
        for (int c = 0; c < 3; ++c) {
            Voice v(n, 3);
            v.setTuning(cases[c]);
            std::vector<float> buf(4410);
            bool finite = true;
            float peak = 0.0f;
            double most = 0.0;
            for (int step = 0; step < 40; ++step) {                     // 4 s: the line, twice over
                if (step % 20 == 0) v.sing(kLine, kLineCount);
                if (step % 20 == 0) v.setBreath(1.0f);
                v.render(buf.data(), (int)buf.size());
                for (float x : buf) {
                    if (!std::isfinite(x)) finite = false;
                    if (std::fabs(x) > peak) peak = std::fabs(x);
                }
                const double e = v.tube().energy();
                if (!std::isfinite(e)) finite = false;
                if (e > most) most = e;
            }
            CHECK_MSG(finite, "N=%d %s: not finite", n, names[c]);
            CHECK_MSG(peak <= 1.0f, "N=%d %s: peak %.3f", n, names[c], peak);
            CHECK_MSG(most < 2e8, "N=%d %s: tube energy %.3g", n, names[c], most);

            v.setTuning(Tuning());
            v.stop();
            for (int step = 0; step < 30; ++step) v.render(buf.data(), (int)buf.size());   // let it ring out
            CHECK_MSG(v.state().breath == 0.0f && std::isfinite(v.tube().energy()) && v.tube().energy() < 1e-3,
                      "N=%d %s: did not settle, energy %.3g", n, names[c], v.tube().energy());
        }

}

// the live setters take what they are given: a breath past 1 is only louder,
// a tenseness past 1 still sings (below zero it would have no voicing gain, so
// that one floor stands), and wobble and vibrato run at any value.
WP_TEST(tuning_setters_are_not_capped) {
    auto level = [](float vel) {
        Voice v(22, 7);
        v.setWobble(0.0f);
        v.noteOn(45.0f, vel);
        std::vector<float> x(22050);
        v.render(x.data(), (int)x.size());
        return rms(x.data() + 11025, 11025);
    };
    const double one = level(1.0f), two = level(2.0f);
    CHECK_MSG(two > 1.8 * one && two < 2.2 * one, "breath 2 is %.2fx breath 1", two / one);

    for (float tense : {1.5f, 5.0f, -0.5f, -100.0f}) {
        Voice v(22, 7);
        v.setTenseness(tense);
        std::vector<float> x = singLine(v);
        bool finite = true;
        for (float s : x) if (!std::isfinite(s)) finite = false;
        CHECK_MSG(finite, "tenseness %.1f: not finite", tense);
        CHECK_MSG(v.state().tenseness >= 0.0f, "tenseness %.1f: eased to %.3f", tense, v.state().tenseness);
    }
    {   Voice v(22, 7);
        v.setTenseness(1.5f);
        std::vector<float> x(22050);
        v.render(x.data(), (int)x.size());
        CHECK_MSG(std::fabs(v.state().tenseness - 1.5f) < 1e-2f, "tenseness 1.5 held at %.3f", v.state().tenseness); }

    for (int k = 0; k < 4; ++k) {
        Voice v(22, 7);
        if (k == 0) v.setWobble(25.0f);
        if (k == 1) v.setWobble(-3.0f);
        if (k == 2) v.setVibrato(-6.0f, 400.0f, -1.0f);
        if (k == 3) v.setVibrato(900.0f, -45.0f, 0.0f);
        bool finite = true;
        for (float s : singLine(v)) if (!std::isfinite(s)) finite = false;
        CHECK_MSG(finite && std::isfinite(v.state().f0), "case %d: not finite", k);
    }
}

// a drift too slow for a float coefficient is a drift that never moves — not a
// division by zero. any speed is a legal speed.
WP_TEST(tuning_wobble_speed_has_no_dead_zone) {
    for (float hz : {1e-30f, 1e-6f, 1e-5f, 0.0f, -2.0f, 1e6f}) {
        Tuning t;
        t.wobbleSlowHz = hz; t.wobbleFastHz = hz;
        Voice v(22, 7);
        v.setTuning(t);
        v.setWobble(1.0f);
        bool finite = true;
        for (float s : singLine(v)) if (!std::isfinite(s)) finite = false;
        CHECK_MSG(finite && std::isfinite(v.state().f0) && v.state().f0 > 0.0f, "wobble at %g Hz: f0 %.3f", hz, v.state().f0);
    }
}

// a channel easing at its own rate takes a new one at once; one a score has
// given a rate keeps it until it is aimed again.
WP_TEST(tuning_rates_land_on_channels_at_their_own_rate) {
    Tuning fast;
    fast.rateTongue = 140.0f;
    const float target = 0.6f;

    Rig own, ownFast, held, heldFast;
    own.body.setTongue(target, 2.4f);
    ownFast.body.setTongue(target, 2.4f);
    held.body.setTongue(target, 2.4f, 5.0f);                            // a score's override
    heldFast.body.setTongue(target, 2.4f, 5.0f);
    ownFast.body.retune(fast);
    heldFast.body.retune(fast);
    own.run(10); ownFast.run(10); held.run(10); heldFast.run(10);

    const float gap = std::fabs(target - own.body.pose().tongueX), gapFast = std::fabs(target - ownFast.body.pose().tongueX);
    CHECK_MSG(gapFast < 0.1f * gap, "own rate: %.4f vs %.4f from the target", gapFast, gap);
    CHECK(held.body.pose().tongueX == heldFast.body.pose().tongueX);   // the override stood

    heldFast.body.setTongue(0.3f, 2.4f);                                // aimed again: its own rate, the new one
    held.body.setTongue(0.3f, 2.4f);
    held.run(10); heldFast.run(10);
    CHECK(std::fabs(0.3f - heldFast.body.pose().tongueX) < 0.1f * std::fabs(0.3f - held.body.pose().tongueX));
}

// glottisRest: "voiced" means this voice's rest. a held vowel is noisier, the
// folds come back to the rest after a voiceless consonant — not to shut — and
// a retune re-aims a glottis that is already there.
WP_TEST(tuning_glottis_rests_open) {
    auto held = [](float rest, double& per, float& glottis) {
        Tuning t;
        t.glottisRest = rest;
        Voice v(22, 2);
        v.setTuning(t);
        v.setWobble(0.0f);
        v.setVibrato(0.0f, 0.0f, 0.0f);
        v.noteOn(45.0f, 0.8f);
        std::vector<float> x(44100);
        v.render(x.data(), (int)x.size());
        per = periodicity(x.data() + 30000, 8192, (int)(kDefaultFs / 400.0f), (int)(kDefaultFs / 60.0f));
        glottis = v.state().glottis;
    };
    double perShut, perOpen;
    float gShut, gOpen;
    held(0.0f, perShut, gShut);
    held(0.6f, perOpen, gOpen);
    CHECK(gShut == 0.0f);
    CHECK_MSG(std::fabs(gOpen - 0.6f) < 1e-3f, "glottis %.4f", gOpen);
    CHECK_MSG(perOpen < perShut - 0.02, "periodicity %.4f resting open vs %.4f shut", perOpen, perShut);

    Tuning t;
    t.glottisRest = 0.4f;
    Voice v(22, 2);
    v.setTuning(t);
    v.noteOn(45.0f, 0.8f);
    v.syllable("S AA");
    std::vector<float> x(4410);
    float widest = 0.0f;
    for (int k = 0; k < 10; ++k) {
        v.render(x.data(), (int)x.size());
        if (v.state().glottis > widest) widest = v.state().glottis;
    }
    CHECK_MSG(widest > 0.9f, "the S opened it to %.3f", widest);
    CHECK_MSG(std::fabs(v.state().glottis - 0.4f) < 1e-3f, "after the S it rests at %.4f", v.state().glottis);

    t.glottisRest = 0.1f;                                               // nobody asks again: the retune re-aims
    v.setTuning(t);
    for (int k = 0; k < 5; ++k) v.render(x.data(), (int)x.size());
    CHECK_MSG(std::fabs(v.state().glottis - 0.1f) < 1e-3f, "re-aimed to %.4f", v.state().glottis);
}

// velumRest: the nose stays coupled through a score's commit and through a
// nasal's close. half open, a stop still bursts; wide open it no longer does —
// the body's own rule for a nasal voice, not a limit on the tuning.
WP_TEST(tuning_velum_rests_open) {
    Tuning t;
    t.velumRest = 0.3f;
    Voice v(22, 2);
    v.setTuning(t);
    const Syllable line[] = {{"M AA", 45.0f, 400}, {"B AA", 45.0f, 400}};
    v.sing(line, 2);
    std::vector<float> x(4410);
    float widest = 0.0f, narrowest = 1.0f;
    for (int k = 0; k < 14; ++k) {
        v.render(x.data(), (int)x.size());
        const float velum = v.state().velum;
        if (velum > widest) widest = velum;
        if (k > 0 && velum < narrowest) narrowest = velum;
    }
    CHECK_MSG(widest > 0.9f, "the M opened it to %.3f", widest);
    CHECK_MSG(narrowest > 0.3f - 1e-3f, "it shut to %.4f", narrowest);
    CHECK_MSG(std::fabs(v.state().velum - 0.3f) < 1e-3f, "after the line it rests at %.4f", v.state().velum);

    const Syllable stop[] = {{"AA", 45.0f, 300}, {"B AA", 45.0f, 400}};
    for (float rest : {0.5f, 1.0f}) {
        t.velumRest = rest;
        Voice nasal(22, 2);
        nasal.setTuning(t);
        CHECK(nasal.tuning().velumRest == rest);                        // held as asked
        nasal.sing(stop, 2);
        for (int k = 0; k < 10; ++k) nasal.render(x.data(), (int)x.size());
        if (rest < 0.55f) CHECK_MSG(nasal.body().bursts() >= 1, "rest %.1f: %d bursts", rest, nasal.body().bursts());
        else CHECK_MSG(nasal.body().bursts() == 0, "rest %.1f: %d bursts", rest, nasal.body().bursts());
    }
}

// diction 2: the closure is asked for at twice the rate and leads by half the
// time, its hold and the event count are what they were.
WP_TEST(tuning_diction_scales_the_gesture_not_the_score) {
    const Syllable syl[] = {{"AA", 45.0f, 400}, {"B AA", 45.0f, 500}, {"S T AA N", 47.0f, 600}};
    auto closure = [&](float diction, int& count, float& rate, float& release, int& lead) {
        Event events[kMaxEvents];
        ScoreInfo info;
        ScoreOptions opt;
        opt.diction = diction;
        count = compileSing(syl, 3, opt, events, kMaxEvents, &info);
        rate = release = 0.0f; lead = 0;
        for (int i = 0; i < count; ++i) {
            if (events[i].phone != ph::B) continue;
            rate = events[i].rate;
            const int holdTicks = (int)((float)consonant(ph::B).holdMs * kDefaultFs * 0.001f);
            lead = (int)info.beat[1] - holdTicks - (int)events[i].tick;
            for (int k = i + 1; k < count; ++k)                         // its clear, on the same slot
                if (events[k].channel == events[i].channel && events[k].a == kSlotClear) { release = events[k].rate; break; }
        }
    };
    int n1, n2, lead1, lead2;
    float rate1, rate2, rel1, rel2;
    closure(1.0f, n1, rate1, rel1, lead1);
    closure(2.0f, n2, rate2, rel2, lead2);
    CHECK(n1 > 0 && n1 == n2);
    CHECK(rate1 == consonant(ph::B).closeRate && rate2 == 2.0f * rate1);
    CHECK(rel1 == consonant(ph::B).releaseRate && rel2 == 2.0f * rel1);
    CHECK_MSG(lead1 > 0 && std::abs(lead1 - 2 * lead2) <= 2, "lead %d then %d samples", lead1, lead2);

    // and through a voice: its own tuning reaches its own compiler
    Tuning t;
    t.diction = 2.0f;
    Voice crisp(22, 4), plain(22, 4);
    crisp.setTuning(t);
    CHECK(!same(singLine(crisp), singLine(plain)));
}
