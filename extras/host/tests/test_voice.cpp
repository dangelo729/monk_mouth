// wind_pipe — tests/test_voice.cpp  (plan: Tests / Voice)
// all of it at N = 22, and again at N = 16 and N = 26.
#include <cmath>
#include <cstring>
#include <vector>

#include "core/wp_voice.h"
#include "wp_analysis.h"
#include "wp_test.h"

using namespace wp;

namespace {

const int kSizes[] = {22, 16, 26};

float unit(Rng& r) { return 0.5f + 0.5f * r.bipolar(); }

// one random gesture on the whole public surface
void thrash(Voice& v, Rng& r) {
    switch (r.next() % 17) {
        case 0:  v.setBreath(unit(r) < 0.2f ? 0.0f : unit(r)); break;
        case 1:  v.setPitch(30.0f + 60.0f * unit(r)); break;
        case 2:  v.setGlottis(unit(r)); break;
        case 3:  v.setTenseness(unit(r)); break;
        case 4:  v.setTongue(unit(r), unit(r)); break;
        case 5:  v.setLips(unit(r) < 0.3f ? 0.0f : unit(r)); break;
        case 6:  v.setVelum(unit(r)); break;
        case 7:
        case 8:  v.setConstriction((int)(r.next() & 1), unit(r), unit(r) < 0.5f ? -1.0f : unit(r)); break;
        case 9:
        case 10: v.clearConstriction((int)(r.next() & 1)); break;
        case 11: v.setGlide(unit(r) < 0.3f ? 0.0f : 100.0f * unit(r)); break;
        case 12: v.setVibrato(8.0f * unit(r), 100.0f * unit(r), unit(r)); break;
        case 13: v.setWobble(unit(r)); break;
        case 14: v.noteOn(30.0f + 60.0f * unit(r), unit(r)); break;
        case 15: v.setGlottis(1.0f); v.setVelum(0.0f); v.setLips(0.2f + 0.3f * unit(r));        // a whistle
                 v.setTongue(0.6f + 0.4f * unit(r), 0.3f + 0.6f * unit(r)); v.setBreath(0.3f + unit(r)); break;
        default: v.noteOff(); break;
    }
}

}  // namespace

// 60 s of random target thrash: finite, |x| <= 1, DC < 1e-3.
WP_TEST(voice_survives_target_thrash) {
    for (int n : kSizes) {
        Voice v(n, 7);
        Rng r; r.seed(1000u + (uint32_t)n);
        std::vector<float> buf(441);
        double sum = 0.0;
        float peak = 0.0f;
        bool finite = true;
        long count = 0;
        for (int step = 0; step < 6000; ++step) {                    // 60 s, a gesture every 10 ms
            thrash(v, r);
            if (step % 7 == 0) thrash(v, r);
            if (step % 500 == 250) v.say("S T R IY1 T / B AA1 M Z");  // words in the storm, too
            v.render(buf.data(), (int)buf.size());
            for (float x : buf) {
                if (!std::isfinite(x)) finite = false;
                if (std::fabs(x) > peak) peak = std::fabs(x);
                sum += x;
            }
            count += (long)buf.size();
        }
        CHECK(finite);
        CHECK_MSG(peak <= 1.0f, "N=%d peak %.3f", n, peak);
        CHECK_MSG(std::fabs(sum / (double)count) < 1e-3, "N=%d DC %.2e", n, sum / (double)count);
    }
}

// every target stepped between extremes: the first difference stays bounded — no clicks.
// the yardstick is the voice's own steepest edge: the glottal closure of a held note,
// at the top of the stepped pitch range and fully tense.
WP_TEST(voice_target_steps_do_not_click) {
    for (int n : kSizes) {
        auto maxStep = [n](int which) {
            Voice v(n, 3);
            v.setWobble(0.0f);
            v.noteOn(which == 0 ? 69.0f : 45.0f, 1.0f);             // the yardstick: the steepest held note
            if (which == 0) v.setTenseness(1.0f);
            std::vector<float> buf(4410);
            float worst = 0.0f, prev = 0.0f;
            for (int step = 0; step < 40; ++step) {                  // 4 s, a step every 100 ms
                const bool hi = step & 1;
                switch (which) {
                    case 1: v.setTongue(hi ? 1.0f : 0.0f, hi ? 1.0f : 0.0f); break;
                    case 2: v.setLips(hi ? 1.0f : 0.3f); break;
                    case 3: v.setVelum(hi ? 1.0f : 0.0f); break;
                    case 4: v.setTenseness(hi ? 1.0f : 0.0f); break;
                    case 5: v.setGlottis(hi ? 1.0f : 0.0f); break;
                    case 6: v.setBreath(hi ? 1.0f : 0.05f); break;
                    case 7: v.setPitch(hi ? 69.0f : 33.0f); break;
                    case 8: if (hi) v.setConstriction(0, 0.7f, 0.5f); else v.clearConstriction(0); break;
                    default: break;                                  // 0: the held note itself
                }
                v.render(buf.data(), (int)buf.size());
                for (float x : buf) { const float d = std::fabs(x - prev); if (d > worst) worst = d; prev = x; }
            }
            return worst;
        };
        const float held = maxStep(0);
        CHECK(held > 0.0f);
        for (int which = 1; which <= 8; ++which) {
            const float w = maxStep(which);
            CHECK_MSG(w < 3.0f * held && w < 0.5f, "N=%d target %d: step %.4f vs held note %.4f", n, which, w, held);
        }
    }
}

// breath off -> exact zeros within 500 ms.
WP_TEST(voice_breath_off_reaches_exact_zero) {
    for (int n : kSizes) {
        for (int shape = 0; shape < 3; ++shape) {
            Voice v(n, 5);
            v.noteOn(50.0f, 1.0f);
            if (shape == 1) v.setConstriction(0, 0.8f, -1.0f);       // a closed tract rings longest
            if (shape == 2) { v.setLips(0.0f); v.setVelum(1.0f); }
            std::vector<float> buf(22050);
            v.render(buf.data(), (int)buf.size());
            v.noteOff();
            v.render(buf.data(), (int)buf.size());                   // the 500 ms
            std::vector<float> after(4410);
            v.render(after.data(), (int)after.size());
            bool allZero = true;
            for (float x : after) if (x != 0.0f) allZero = false;
            CHECK_MSG(allZero, "N=%d shape %d still sounding", n, shape);
            CHECK(v.state().breath == 0.0f);
        }
    }
}

// same seed, same calls -> identical bytes. and the block the caller renders in
// does not matter: control runs every 128 samples inside the library. the
// whistle's telemetry is as exact as the samples, and so is a held whistle.
WP_TEST(voice_is_deterministic) {
    for (int n : kSizes) {
        auto run = [n](uint32_t seed, int chunk) {
            Voice v(n, seed);
            Rng r; r.seed(42);
            std::vector<float> out;
            std::vector<float> buf((size_t)chunk);
            long rendered = 0;
            for (int step = 0; step < 300; ++step) {
                thrash(v, r);
                if (step == 100) v.say("HH AH0 L OW1");
                if (step == 200) { v.setGlottis(1.0f); v.setVelum(0.0f); v.setTongue(0.9f, 0.6f); v.setLips(0.3f); v.setBreath(0.8f); }
                const long until = (long)(step + 1) * 1280;          // gestures land on block edges
                while (rendered < until) {
                    const int m = (int)((until - rendered) < chunk ? (until - rendered) : chunk);
                    v.render(buf.data(), m);
                    out.insert(out.end(), buf.begin(), buf.begin() + m);
                    rendered += m;
                }
                out.push_back(v.state().whistle);                     // the telemetry, as exact as the sound
                out.push_back(v.state().whistleHz);
            }
            return out;
        };
        const std::vector<float> a = run(11, 128), b = run(11, 128), c = run(11, 37), d = run(12, 128);
        CHECK(a.size() == b.size() && memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
        CHECK(a.size() == c.size() && memcmp(a.data(), c.data(), a.size() * sizeof(float)) == 0);
        CHECK(a.size() == d.size() && memcmp(a.data(), d.data(), a.size() * sizeof(float)) != 0);
    }
}

// a held vowel is a periodic signal at the asked pitch, at a sane level.
WP_TEST(voice_holds_a_note) {
    for (int n : kSizes) {
        Voice v(n, 1);
        v.setWobble(0.0f);
        v.setVibrato(0.0f, 0.0f, 0.0f);
        v.noteOn(45.0f, 0.8f);                                       // A2 = 110 Hz
        std::vector<float> x(44100);
        v.render(x.data(), (int)x.size());
        const float* steady = x.data() + 22050;
        const double level = toDb(rms(steady, 16384));
        CHECK_MSG(level > -40.0 && level < -6.0, "N=%d level %.1f dB", n, level);
        const int period = (int)std::lround(kDefaultFs / 110.0);
        CHECK(periodicity(steady, 8192, period - 2, period + 2) > 0.9);
        CHECK(std::fabs(v.state().f0 - 110.0f) < 0.5f);
        CHECK(v.state().voicing > 0.5f && !v.state().closed);
    }
}

// the readback tells the truth about the body.
WP_TEST(voice_state_readback) {
    Voice v;
    float buf[kBlock];
    v.setTongue(0.25f, 0.75f);
    v.setLips(0.5f);
    v.setVelum(1.0f);
    v.noteOn(60.0f, 0.7f);
    for (int i = 0; i < 400; ++i) v.render(buf, kBlock);             // ~1.2 s: everything has arrived
    const BodyState s = v.state();
    CHECK(std::fabs(s.tonguePos - 0.25f) < 0.01f && std::fabs(s.tongueHeight - 0.75f) < 0.01f);
    CHECK(std::fabs(s.lips - 0.5f) < 0.01f && std::fabs(s.velum - 1.0f) < 0.01f);
    CHECK(std::fabs(s.breath - 0.7f) < 0.01f && std::fabs(s.midi - 60.0f) < 0.01f);
    float d[kMaxSections];
    CHECK(v.diameters(d, kMaxSections) == 22);
    CHECK(std::fabs(d[21] - 0.75f) < 0.01f);                         // the lips, in tract units
    v.setConstriction(0, 0.82f, -1.0f);
    for (int i = 0; i < 40; ++i) v.render(buf, kBlock);
    CHECK(v.state().closed && v.state().slotActive[0]);
    CHECK(v.state().whistle == 0.0f);                                // a voice is not a whistle

    // a whistle: folds open, lips puckered. whistle near one, its pitch on the
    // pose's lip mode; then voiced again, it falls back to nothing.
    v.clearConstriction(0);
    v.setGlottis(1.0f);
    v.setVelum(0.0f);
    v.setTongue(0.9f, 0.6f);
    v.setLips(0.3f);
    v.setBreath(0.8f);
    for (int i = 0; i < 400; ++i) v.render(buf, kBlock);
    const BodyState w = v.state();
    CHECK_MSG(w.whistle > 0.9f, "whistle %.3f", w.whistle);
    float dw[kMaxSections];
    const int count = v.diameters(dw, kMaxSections);
    double mode = 0.0;
    for (double m : whistleModes(count, dw, kVelumShut)) if (mode == 0.0 || std::fabs(m - w.whistleHz) < std::fabs(mode - w.whistleHz)) mode = m;
    CHECK_MSG(mode > 0.0 && std::fabs(w.whistleHz / mode - 1.0) < 0.05, "whistleHz %.1f, lip mode %.1f", w.whistleHz, mode);
    CHECK(w.glottis > 0.99f && w.voicing == 0.0f);
    v.setGlottis(0.0f);
    for (int i = 0; i < 100; ++i) v.render(buf, kBlock);
    CHECK_MSG(v.state().whistle < 0.05f && v.state().voicing > 0.5f, "voiced again: whistle %.3f", v.state().whistle);
    const float held = v.state().whistleHz;                          // the dying whistle's last pitch, held
    for (int i = 0; i < 100; ++i) v.render(buf, kBlock);
    CHECK_MSG(v.state().whistleHz == held && held > 2.0f * v.state().f0, "held %.1f then %.1f, f0 %.1f", held,
              v.state().whistleHz, v.state().f0);

    // whistle(): the phoneme a syllable left reads none, the note reads as
    // asked, whistling; then a stop voices again
    CHECK(v.syllable("AA"));
    for (int i = 0; i < 100; ++i) v.render(buf, kBlock);
    CHECK(v.state().phoneme == ph::AA);
    v.whistle(86.0f, 0.8f);
    for (int i = 0; i < 400; ++i) v.render(buf, kBlock);
    const BodyState ws = v.state();
    CHECK_MSG(ws.phoneme == ph::none, "phoneme %s during a whistle", phoneName(ws.phoneme));
    CHECK_MSG(std::fabs(ws.midi - 86.0f) < 0.01f, "midi %.3f", ws.midi);
    CHECK_MSG(ws.whistle > 0.5f && ws.glottis > 0.99f && ws.voicing == 0.0f, "whistle %.3f glottis %.3f", ws.whistle, ws.glottis);
}

// a target set between blocks lands at the top of the next block, for everything.
WP_TEST(voice_one_latency) {
    Voice v;
    float buf[kBlock];
    v.render(buf, kBlock);
    v.setBreath(1.0f);
    CHECK(v.state().breath == 0.0f);                                 // not yet
    v.render(buf, kBlock);
    CHECK(v.state().breath > 0.0f);                                  // one block later
}

// the last writer wins in the caller's own order, across channels too: a
// stop() shuts the breath, so a noteOn called after it — in the same block —
// must still sing.
WP_TEST(voice_stop_then_note_on_sings) {
    Voice v;
    float buf[kBlock];
    const Syllable line[] = {{"L AA", 48, 2000}};
    CHECK(v.sing(line, 1));
    for (int i = 0; i < 60; ++i) v.render(buf, kBlock);
    v.stop();
    v.noteOn(52, 0.85f);
    CHECK(v.syllable("B AA"));
    for (int i = 0; i < 60; ++i) v.render(buf, kBlock);
    CHECK(v.state().breath > 0.8f);                                  // the note holds
    v.noteOn(55, 0.85f);                                             // and the other order still stops
    v.stop();
    for (int i = 0; i < 200; ++i) v.render(buf, kBlock);
    CHECK(v.state().breath == 0.0f);
}

// sing() at a breath: the line sings at that level — half the breath is half
// the sound, within a dB — and leaves the voice's own level alone, so a plain
// sing() afterwards is as loud as it ever was.
WP_TEST(voice_sings_a_line_at_a_given_breath) {
    const Syllable line[] = {{"L AA", 48.0f, 600}, {"M IY", 50.0f, 600}};
    auto take = [&](Voice& v, float breath, float& peakBreath) {
        CHECK(breath > 0.0f ? v.sing(line, 2, breath) : v.sing(line, 2));
        std::vector<float> x(2 * 44100);
        peakBreath = 0.0f;
        for (int at = 0; at + kBlock <= (int)x.size(); at += kBlock) {
            v.render(x.data() + at, kBlock);
            if (v.state().breath > peakBreath) peakBreath = v.state().breath;
        }
        return rms(x.data(), (int)x.size());
    };
    for (int n : kSizes) {
        Voice plain(n, 3), soft(n, 3);
        float plainBreath = 0.0f, softBreath = 0.0f, againBreath = 0.0f;
        const double loud = take(plain, 0.0f, plainBreath);
        const double half = take(soft, 0.5f * kScoreBreath, softBreath);
        CHECK_MSG(std::fabs(softBreath - 0.5f * kScoreBreath) < 0.02f, "N=%d breath %.3f", n, softBreath);
        CHECK_MSG(std::fabs(toDb(half / loud) + 6.02) < 1.0, "N=%d half the breath is %.2f dB", n, toDb(half / loud));
        const double again = take(soft, 0.0f, againBreath);
        CHECK_MSG(std::fabs(againBreath - plainBreath) < 0.02f && std::fabs(toDb(again / loud)) < 1.0,
                  "N=%d plain sing after: breath %.3f, %.2f dB", n, againBreath, toDb(again / loud));
    }
}

// a voice takes any size the arrays hold, and at every one of them it sings a
// line with a hiss, a nasal and a stop in it: finite, inside [-1, 1], not
// silent, and quiet again afterwards. the limits on size are memory's.
WP_TEST(voice_sings_at_every_legal_size) {
    const Syllable line[] = {{"S AA", 48.0f, 300}, {"M IY", 50.0f, 300}, {"B UW", 52.0f, 300}};
    for (int n = kMinSections; n <= kMaxSections; ++n) {
        Voice v(n, 11);
        CHECK(v.sections() == n);
        CHECK(v.sing(line, 3));
        std::vector<float> x(3 * 44100);
        v.render(x.data(), (int)x.size());
        bool finite = true;
        float peak = 0.0f;
        for (float s : x) {
            if (!std::isfinite(s)) finite = false;
            if (std::fabs(s) > peak) peak = std::fabs(s);
        }
        CHECK_MSG(finite && peak <= 1.0f, "N=%d peak %.3f", n, peak);
        CHECK_MSG(rms(x.data(), 44100) > 1e-4, "N=%d is silent", n);
        CHECK_MSG(v.state().breath == 0.0f, "N=%d still breathing", n);
    }
    Voice under(kMinSections - 5, 1), over(kMaxSections + 50, 1);         // outside the arrays: held to them
    CHECK(under.sections() == kMinSections && over.sections() == kMaxSections);
}
