// monk_mouth — tests/test_jet.cpp
// the jet at the lips: the airflow the body hands it, a bounded loop, silent
// under a voice, and a whistle at a pose — which needs breath, rises with it,
// dies through the nose and stops with the note.
//
// the hand-found pose: folds open, velum shut,
// tongue at 0.9 forward and 0.6 high, lips at 0.3, no slot. at the default
// tuning and N = 22 its lip mode is 1222 Hz; it does not lock at a breath of
// 0.2, locks weakly at 0.28 (whistle 0.43) and fully from 0.3 (1186 Hz),
// at 1252 Hz at breath 0.8, 1270 Hz at breath 1. at 16 it needs 0.5.
#include <cmath>
#include <cstring>
#include <vector>

#include "core/mm_voice.h"
#include "mm_analysis.h"
#include "mm_test.h"

using namespace mm;

namespace {

const int kSizes[] = {22, 16, 26};

// the hand-found pose, live
void posePucker(Voice& v, float breath) {
    v.setGlottis(1.0f);
    v.setTongue(0.9f, 0.6f);
    v.setLips(0.3f);
    v.setVelum(0.0f);
    v.setBreath(breath);
}

std::vector<float> render(Voice& v, float seconds) {
    std::vector<float> x((size_t)(seconds * kDefaultFs));
    v.render(x.data(), (int)x.size());
    return x;
}

// the lip mode nearest hz, from the voice's own eased diameters
double nearestLipMode(const Voice& v, double hz) {
    float d[kMaxSections];
    const int n = v.diameters(d, kMaxSections);
    const std::vector<double> modes = whistleModes(n, d, lerpf(kVelumShut, kVelumOpen, v.state().velum), v.tuning());
    double best = 0.0;
    for (double m : modes) if (best == 0.0 || std::fabs(m - hz) < std::fabs(best - hz)) best = m;
    return best;
}

// periodicity within 10 % of a period around hz
double periodicityAt(const float* x, int n, double hz) {
    const double period = kDefaultFs / hz;
    return periodicity(x, n, (int)(0.9 * period), (int)(1.1 * period) + 1);
}

struct Rig {
    Tube tube;
    Body body;
    explicit Rig(const Tuning& t = Tuning(), int n = kDefaultSections) : tube(n) { body.init(n, kDefaultFs, t); }
    void run(float seconds) {
        const int blocks = (int)(seconds * kDefaultFs / kBlock);
        for (int i = 0; i < blocks; ++i) body.tick(tube);
    }
};

// the airflow's formulas, from the body's own diameters and glottis
void expectedAirflow(const Body& b, const Tuning& t, float& flow, float& delay, float& halfWidth, float& lipArea) {
    const int n = b.sections();
    const float* d = b.diameters();
    const Layout& layout = b.layout();
    float tract = 1e30f, lip = 1e30f;
    for (int i = 0; i < n; ++i) {                                // a section past 38/44 in part is lip in part
        const float a = d[i] * d[i];
        const float w = std::fmin(std::fmax(layout.centre(i) - layout.length * 38.0f / 44.0f + 0.5f, 0.0f), 1.0f);
        if (w < 1.0f) tract = std::fmin(tract, a / (1.0f - w));
        if (w > 0.0f) lip = std::fmin(lip, a / w);
    }
    if (lip >= 1e30f) lip = d[n - 1] * d[n - 1];
    const bool open = tract >= kJetMinArea && lip >= kJetMinArea;
    const double ag = std::fmax(lerpf(kGlottisAreaShut, kGlottisAreaOpen, b.glottisOpen()), kJetMinArea) * 1e-4;
    const double at = std::fmax(tract, kJetMinArea) * 1e-4, al = std::fmax(lip, kJetMinArea) * 1e-4;
    const double u = open ? std::sqrt(2.0 * b.breath() * t.breathPressure / kAirDensity)
                            / std::sqrt(1.0 / (ag * ag) + 1.0 / (at * at) + 1.0 / (al * al)) : 0.0;
    const double oral = 1.0 - std::fmin(std::fmax(b.pose().velumD * b.pose().velumD / kNasalArea, 0.0), 1.0);
    const double lipD = std::sqrt(std::fmax(lip, kJetMinArea));
    const double pucker = std::fmin(std::fmax(b.pose().lipD, 0.0f), kLipDMax);
    const double slit = pucker > 0.0 ? lipD / pucker : 0.0;                       // the pucker's own over a slit's cut
    const double steady = smoothstep01(b.glottisOpen()) *
                          smoothstep01((float)((slit - kJetSlitCut) / (kJetSlitClear - kJetSlitCut)));
    const double vj = u / al;
    double dl = vj > 0.0 ? (t.jetLength / 100.0) / (kJetConvection * vj) * kDefaultFs : kMaxJetDelay - 2;
    dl = std::fmin(std::fmax(dl, 1.0), kMaxJetDelay - 2);
    flow = (float)(u * oral * steady);
    delay = (float)dl;
    halfWidth = (float)(0.5 * lipD / 100.0);
    lipArea = (float)al;
}

bool near(float a, float b, float rel) { return std::fabs(a - b) <= rel * std::fabs(b) + 1e-12f; }

}  // namespace

// the block-rate numbers: bernoulli through three orifices in series,
// the transit clamped, the areas floored. a closure anywhere zeroes the flow,
// and the nose takes its share. the jet carries only what is steady: nothing
// while the folds pulse, nothing through a slot cut into the lips below the pucker.
MM_TEST(jet_airflow_follows_the_pose) {
    auto pucker = [](Rig& r) {
        r.body.setBreath(0.8f);
        r.body.setGlottis(1.0f);
        r.body.setTongue(lerpf(kTongueXMin, kTongueXMax, 0.9f), lerpf(kTongueDMax, kTongueDMin, 0.6f));
        r.body.setLips(0.45f);
    };
    for (int n : kSizes) {
        const Tuning t;
        for (int shape = 0; shape < 4; ++shape) {
            Rig r(t, n);
            pucker(r);
            if (shape == 1) r.body.setGlottis(0.0f);                  // voiced: the folds are the narrowest
            if (shape == 2) r.body.setConstriction(0, 0.75f, 0.3f);   // a slot behind the lips
            if (shape == 3) r.body.setLips(1.5f);                     // wide open
            r.run(0.5f);
            float flow, delay, halfWidth, lipArea;
            expectedAirflow(r.body, t, flow, delay, halfWidth, lipArea);
            const BodyFrame& f = r.body.frame();
            CHECK_MSG(near(f.jetFlow, flow, 1e-3f), "N=%d shape %d flow %.4g vs %.4g", n, shape, f.jetFlow, flow);
            CHECK_MSG(near(f.jetDelay, delay, 1e-3f), "N=%d shape %d delay %.4g vs %.4g", n, shape, f.jetDelay, delay);
            CHECK_MSG(near(f.jetHalfWidth, halfWidth, 1e-4f), "N=%d shape %d half-width %.4g vs %.4g", n, shape, f.jetHalfWidth, halfWidth);
            CHECK_MSG(near(f.lipArea, lipArea, 1e-4f), "N=%d shape %d lip area %.4g vs %.4g", n, shape, f.lipArea, lipArea);
            if (shape == 1) CHECK(f.jetFlow == 0.0f); else CHECK(f.jetFlow > 0.0f);
        }
    }

    // a kilopascal, lips at 0.3 (d = 0.3 cm), nothing else narrow
    {   Tuning t;
        t.breathPressure = 1000.0f;
        Rig r(t);
        r.body.setBreath(1.0f);
        r.body.setGlottis(1.0f);
        r.body.setLips(0.3f);
        r.run(0.5f);
        const BodyFrame& f = r.body.frame();
        const float u = f.jetFlow;
        const float vj = u / f.lipArea;
        CHECK_MSG(vj > 35.0f && vj < 41.0f, "jet speed %.1f m/s", vj);             // 41 m/s through the lips alone
        CHECK_MSG(u > 3.0e-4f && u < 3.7e-4f, "jet flow %.3g m³/s", u);
        CHECK(near(f.lipArea, 0.09e-4f, 1e-3f) && near(f.jetHalfWidth, 1.5e-3f, 1e-3f)); }

    // a slit against the teeth breaks the jet: an F's slot in open lips
    {   Rig r;
        pucker(r);
        r.body.setLips(kLipDMax);
        r.body.setConstriction(0, 0.93f, 0.15f);
        r.run(0.5f);
        CHECK_MSG(r.body.frame().jetFlow == 0.0f, "an F's slit: flow %.3g", r.body.frame().jetFlow); }

    // half-open folds pass half a steady jet; pulsing ones, none
    {   Rig half, open;
        pucker(half); pucker(open);
        half.body.setGlottis(0.5f);
        half.run(0.5f); open.run(0.5f);
        const float h = half.body.frame().jetFlow, o = open.body.frame().jetFlow;
        CHECK_MSG(h > 0.0f && h < 0.6f * o, "half-open folds %.3g m³/s, open %.3g m³/s", h, o); }

    // the transit clamps at both ends
    for (float len : {1e-6f, -5.0f, 1e6f}) {
        Tuning t;
        t.jetLength = len;
        Rig r(t);
        pucker(r);
        r.run(0.5f);
        const float want = len > 1.0f ? (float)(kMaxJetDelay - 2) : 1.0f;
        CHECK_MSG(r.body.frame().jetDelay == want, "jetLength %g: delay %.3f", len, r.body.frame().jetDelay);
    }

    // a closure anywhere zeroes the flow: the lips, a slot, and the tongue's slot far back
    for (int where = 0; where < 3; ++where) {
        Rig r;
        pucker(r);
        if (where == 0) r.body.setLips(0.0f);
        if (where == 1) r.body.setConstriction(0, 0.82f, -1.0f);
        if (where == 2) r.body.setConstriction(1, 0.5f, -1.0f);
        r.run(0.5f);
        CHECK_MSG(r.body.frame().jetFlow == 0.0f, "closure %d: flow %.3g", where, r.body.frame().jetFlow);
        CHECK(r.body.frame().jetDelay == (float)(kMaxJetDelay - 2));
    }

    // the areas floor: a glottis resting far below shut, lips at the floor's
    // edge — finite numbers, and the formulas still hold
    for (float lips : {0.0317f, 0.04f}) {
        Tuning t;
        t.glottisRest = -50.0f;
        Rig r(t);
        pucker(r);
        r.body.setGlottis(0.5f);
        r.body.setLips(lips);
        r.run(0.5f);
        float flow, delay, halfWidth, lipArea;
        expectedAirflow(r.body, t, flow, delay, halfWidth, lipArea);
        const BodyFrame& f = r.body.frame();
        CHECK_MSG(std::isfinite(f.jetFlow) && std::isfinite(f.jetDelay) && near(f.jetFlow, flow, 1e-3f) &&
                  near(f.lipArea, lipArea, 1e-4f), "floored: flow %.3g vs %.3g, lip area %.3g vs %.3g", f.jetFlow, flow,
                  f.lipArea, lipArea);
    }

    // a nasal pose starves the jet; half open, it takes a share
    auto nasal = [&](float velum) {
        Rig r;
        pucker(r);
        r.body.setVelum(velum);
        r.run(0.5f);
        return r.body.frame().jetFlow;
    };
    CHECK(nasal(1.0f) == 0.0f);
    CHECK(nasal(0.3f) > 0.0f && nasal(0.3f) < nasal(0.0f));
}

// 60 s of target thrash with whistle poses in the mix, at the defaults and at
// the jet's limits: finite, inside [-1, 1], no DC. at 1e30 Pa every whistle is
// a full-scale square the output clamp cuts, and a clamped square's duty is
// its own: there the DC is the clamp's, and only finite and inside is asked.
MM_TEST(jet_is_bounded) {
    struct Case { const char* name; float pressure, length, gain; bool clamped; };
    const Tuning d;
    const Case cases[] = {
        {"defaults", d.breathPressure, d.jetLength, d.jetGain, false},
        {"no pressure", 0.0f, -1e30f, -1e30f, false},
        {"long, hard", d.breathPressure, 1e30f, 1e30f, false},
        {"short, hard", d.breathPressure, -1e30f, 1e30f, false},
        {"short, flipped", d.breathPressure, -1e30f, -1e30f, false},
        {"1e30 Pa", 1e30f, 1e30f, 1e30f, true},
        {"1e30 Pa, short", 1e30f, -1e30f, -1e30f, true},
    };
    for (int n : kSizes)
        for (const Case& c : cases) {
            Tuning t;
            t.breathPressure = c.pressure; t.jetLength = c.length; t.jetGain = c.gain;
            Voice v(n, 7);
            v.setTuning(t);
            Rng r; r.seed(2000u + (uint32_t)n);
            std::vector<float> buf(441);
            double sum = 0.0;
            float peak = 0.0f;
            bool finite = true;
            long count = 0;
            auto unit = [&r]() { return 0.5f + 0.5f * r.bipolar(); };
            for (int step = 0; step < 6000; ++step) {                    // 60 s, a gesture every 10 ms
                switch (r.next() % 8) {
                    case 0: if (unit() < 0.5f) posePucker(v, 0.2f + unit());   // a whistle, by hand
                            else v.whistle(20.0f + 100.0f * unit(), 0.2f + unit());  // or by note
                            break;
                    case 1: v.setTongue(unit(), unit()); break;
                    case 2: v.setLips(unit() < 0.2f ? 0.0f : 0.6f * unit()); break;
                    case 3: v.setGlottis(unit() < 0.5f ? 0.0f : 1.0f); break;
                    case 4: v.setVelum(unit() < 0.7f ? 0.0f : unit()); break;
                    case 5: v.setBreath(unit() < 0.2f ? 0.0f : 1.2f * unit()); break;
                    case 6: if (unit() < 0.5f) v.setConstriction(0, 0.7f + 0.25f * unit(), 0.5f * unit()); else v.clearConstriction(0); break;
                    default: v.noteOn(40.0f + 30.0f * unit(), unit()); break;
                }
                v.render(buf.data(), (int)buf.size());
                for (float x : buf) {
                    if (!std::isfinite(x)) finite = false;
                    if (std::fabs(x) > peak) peak = std::fabs(x);
                    sum += x;
                }
                count += (long)buf.size();
            }
            CHECK_MSG(finite && std::isfinite(v.tube().energy()), "N=%d %s: not finite", n, c.name);
            CHECK_MSG(peak <= 1.0f, "N=%d %s: peak %.3f", n, c.name, peak);
            if (!c.clamped) CHECK_MSG(std::fabs(sum / (double)count) < 1e-3, "N=%d %s: DC %.2e", n, c.name, sum / (double)count);
        }
}

// under a voice the folds are the narrowest orifice and the jet is inert: a
// sung line never reads as a whistle, and neither does a whispered wide vowel.
MM_TEST(jet_is_inert_when_voiced) {
    const Syllable line[] = {{"S AA", 48.0f, 450}, {"Z UW M", 50.0f, 600}, {"F IY", 55.0f, 500},
                             {"HH OW", 60.0f, 450}, {"W UW", 67.0f, 600}};
    for (int n : kSizes) {
        Voice v(n, 5);
        CHECK(v.sing(line, 5));
        float most = 0.0f;
        float buf[kBlock];
        for (int b = 0; b < (int)(3.2f * kDefaultFs / kBlock); ++b) {
            v.render(buf, kBlock);
            if (v.state().whistle > most) most = v.state().whistle;
        }
        CHECK_MSG(most < 0.05f, "N=%d: the sung line's whistle reached %.3f", n, most);

        Voice w(n, 5);                                                   // a whispered AA, lips wide
        w.setGlottis(1.0f);
        w.setTongue(0.1f, 0.3f);
        w.setLips(1.0f);
        w.setBreath(1.0f);
        most = 0.0f;
        for (int b = 0; b < (int)(1.0f * kDefaultFs / kBlock); ++b) {
            w.render(buf, kBlock);
            if (w.state().whistle > most) most = w.state().whistle;
        }
        CHECK_MSG(most < 0.05f, "N=%d: the whispered vowel's whistle reached %.3f", n, most);
    }
}

// the hand-found pose at breath 0.8 whistles: after 300 ms a clean periodic
// tone at a sane level, on its lip mode, the jet swinging near its cap, the
// second and third harmonics well under the fundamental.
MM_TEST(voice_whistles_at_a_pose) {
    for (int n : kSizes) {
        // a 26's lip mode is lower and the jet's transit does not scale with
        // size: at 0.8 the jet pulls its pitch 8 % sharp of the mode (1065 Hz
        // against 987), at 0.5 it sits 3 % sharp. measured.
        const float breath = n == 26 ? 0.5f : 0.8f;
        Voice v(n, 1);
        posePucker(v, breath);
        const std::vector<float> x = render(v, 1.0f);
        const BodyState s = v.state();
        const float* steady = x.data() + (int)(0.3f * kDefaultFs);
        const int count = 16384;
        const double hz = s.whistleHz, mode = nearestLipMode(v, hz);
        const double per = hz > 0.0 ? periodicityAt(steady, 8192, hz) : 0.0;
        const double level = toDb(rms(steady, count));
        CHECK_MSG(per > 0.95, "N=%d periodicity %.3f at %.0f Hz", n, per, hz);
        CHECK_MSG(level > -35.0 && level < -15.0, "N=%d level %.1f dB", n, level);
        CHECK_MSG(mode > 0.0 && std::fabs(hz / mode - 1.0) < 0.05, "N=%d whistleHz %.0f, lip mode %.0f", n, hz, mode);
        CHECK_MSG(s.whistle > 0.5f, "N=%d whistle %.3f", n, s.whistle);

        const std::vector<double> mag = magnitudeSpectrum(steady, count, count, true);
        const double h1 = peakNear(mag, kDefaultFs, hz, 0.03);
        const double h2 = peakNear(mag, kDefaultFs, 2.0 * hz, 0.03), h3 = peakNear(mag, kDefaultFs, 3.0 * hz, 0.03);
        CHECK_MSG(toDb(h2 / h1) < -15.0 && toDb(h3 / h1) < -15.0, "N=%d H2 %.1f dB, H3 %.1f dB", n,
                  toDb(h2 / h1), toDb(h3 / h1));
    }
}

// the same pose at breath 0.1 does not whistle: the jet is too slow to lock.
MM_TEST(voice_whistle_needs_breath) {
    for (int n : kSizes) {
        Voice lock(n, 1), soft(n, 1);
        posePucker(lock, 0.8f);
        posePucker(soft, 0.1f);
        render(lock, 1.0f);
        const double hz = lock.state().whistleHz;                        // where it would lock
        const std::vector<float> x = render(soft, 1.0f);
        const double per = periodicityAt(x.data() + (int)(0.3f * kDefaultFs), 8192, hz);
        CHECK_MSG(soft.state().whistle < 0.1f, "N=%d breath 0.1: whistle %.3f", n, soft.state().whistle);
        CHECK_MSG(per < 0.5, "N=%d breath 0.1: periodicity %.3f at %.0f Hz", n, per, hz);
    }
}

// a faster jet is a shorter transit: the pitch rises a little with breath.
MM_TEST(voice_whistle_rises_with_breath) {
    for (int n : kSizes) {
        Voice lo(n, 1), hi(n, 1);
        posePucker(lo, 0.5f);
        posePucker(hi, 0.9f);
        render(lo, 1.0f);
        render(hi, 1.0f);
        CHECK_MSG(lo.state().whistle > 0.5f && hi.state().whistle > 0.5f, "N=%d whistle %.2f at 0.5, %.2f at 0.9", n,
                  lo.state().whistle, hi.state().whistle);
        CHECK_MSG(hi.state().whistleHz > lo.state().whistleHz, "N=%d %.1f Hz at 0.9, %.1f Hz at 0.5", n,
                  hi.state().whistleHz, lo.state().whistleHz);
    }
}

// air through the nose is air the lips never see: the whistle dies.
MM_TEST(voice_whistle_dies_through_the_nose) {
    for (int n : kSizes) {
        Voice v(n, 1);
        posePucker(v, 0.8f);
        render(v, 0.6f);
        CHECK(v.state().whistle > 0.5f);
        v.setVelum(1.0f);
        render(v, 0.2f);
        CHECK_MSG(v.state().whistle < 0.1f, "N=%d whistle %.3f 200 ms after the velum opened", n, v.state().whistle);
    }
}

// noteOff ends a whistle as it ends a note: the silence gate lands and the
// jet's state is zero.
MM_TEST(voice_whistle_stops_with_the_note) {
    for (int n : kSizes) {
        Voice v(n, 1);
        posePucker(v, 0.8f);
        render(v, 0.6f);
        CHECK(v.state().whistle > 0.5f);
        v.noteOff();
        render(v, 0.8f);
        const std::vector<float> after = render(v, 0.1f);
        bool allZero = true;
        for (float x : after) if (x != 0.0f) allZero = false;
        CHECK_MSG(allZero, "N=%d still sounding", n);
        const Jet& j = v.jet();
        CHECK_MSG(j.flow() == 0.0f && j.drift() == 0.0f && j.nudge() == 0.0f && j.peak() == 0.0f,
                  "N=%d jet flow %.3g drift %.3g nudge %.3g peak %.3g", n, j.flow(), j.drift(), j.nudge(), j.peak());
        CHECK(v.state().whistle == 0.0f && v.state().whistleHz == 0.0f);
    }
}
