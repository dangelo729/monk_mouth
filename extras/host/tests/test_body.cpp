// monk_mouth — tests/test_body.cpp  (plan: Tests / Body)
#include <cmath>
#include <vector>

#include "core/mm_body.h"
#include "mm_analysis.h"
#include "mm_test.h"

using namespace mm;

namespace {

struct Rig {
    Tube tube;
    Body body;
    explicit Rig(int n = kDefaultSections) : tube(n) { body.init(n, kDefaultFs); }
    void run(float seconds) {
        const int blocks = (int)(seconds * kDefaultFs / kBlock);
        for (int i = 0; i < blocks; ++i) body.tick(tube);
    }
    int blocksUntilClosed(int limit) {
        for (int i = 0; i < limit; ++i) { body.tick(tube); if (body.closed()) return i + 1; }
        return -1;
    }
};

float minDiameter(const Body& b) {
    float m = 1e9f;
    for (int i = 0; i < b.sections(); ++i) if (b.diameters()[i] < m) m = b.diameters()[i];
    return m;
}

}  // namespace

// a -0.3 target closes in finite time — and holds, and releases at a defined instant.
MM_TEST(body_closure_arrives_in_finite_time) {
    for (int n : {16, 22, 26}) {
        for (float place : {0.5f, 0.72f, 0.82f, 0.93f}) {
            Rig r(n);
            r.body.setConstriction(0, place, -1.0f);
            const int blocks = r.blocksUntilClosed(200);
            CHECK_MSG(blocks > 0 && blocks * kBlock < 0.060f * kDefaultFs, "N=%d place %.2f closed after %d blocks", n, place, blocks);
            CHECK(minDiameter(r.body) == 0.0f);
            r.run(0.2f);
            CHECK(r.body.closed());                                  // it holds
            r.body.clearConstriction(0);
            r.body.tick(r.tube);
            CHECK(!r.body.closed());                                 // and lets go on the next block
        }
    }
    Rig lips;                                                        // the lips close the same way
    lips.body.setLips(0.0f);
    CHECK(lips.blocksUntilClosed(200) > 0);
}

// a cleared slot goes back up and deactivates once it clears the base shape.
MM_TEST(body_slot_lifecycle) {
    Rig r;
    float before[kMaxSections];
    r.run(0.05f);
    for (int i = 0; i < r.body.sections(); ++i) before[i] = r.body.diameters()[i];
    r.body.setConstriction(1, 0.82f, 0.15f);
    r.body.tick(r.tube);
    CHECK(r.body.pose().slot[1].active);
    CHECK(r.body.pose().slot[1].degree > 0.5f);                      // no dead travel: it starts at the tube's diameter
    r.run(0.2f);
    CHECK(std::fabs(minDiameter(r.body) - 0.15f) < 0.02f);
    r.body.clearConstriction(1);
    r.run(0.5f);
    CHECK(!r.body.pose().slot[1].active);
    for (int i = 0; i < r.body.sections(); ++i) CHECK(r.body.diameters()[i] == before[i]);
}

// one release fires exactly one transient; none with the velum open.
MM_TEST(body_release_fires_one_transient) {
    for (int source = 0; source < 3; ++source) {
        for (int nasal = 0; nasal < 2; ++nasal) {
            Rig r;
            r.body.setBreath(1.0f);
            r.body.setVelum(nasal ? 1.0f : 0.0f);
            r.run(0.3f);
            if (source == 2) r.body.setLips(0.0f); else r.body.setConstriction(source, 0.82f, -1.0f);
            r.run(0.15f);
            CHECK(r.body.closed());
            if (nasal) CHECK(r.body.pressure() == 0.0f);             // the nose vents it
            else CHECK(r.body.pressure() > 0.5f);
            const int before = r.body.bursts();
            if (source == 2) r.body.setLips(1.0f); else r.body.clearConstriction(source);
            r.run(0.3f);
            CHECK_MSG(r.body.bursts() - before == (nasal ? 0 : 1), "source %d nasal %d: %d transients",
                      source, nasal, r.body.bursts() - before);
        }
    }
}

// long closures and voiceless closures burst harder.
MM_TEST(body_pressure_scales_bursts) {
    auto pressureAfter = [](float hold, float glottisOpen) {
        Rig r;
        r.body.setBreath(1.0f);
        r.body.setGlottis(glottisOpen);
        r.run(0.3f);
        r.body.setConstriction(0, 0.82f, -1.0f);
        r.run(hold);
        return r.body.pressure();
    };
    CHECK(pressureAfter(0.12f, 0.0f) > pressureAfter(0.05f, 0.0f));
    CHECK(pressureAfter(0.06f, 1.0f) > pressureAfter(0.06f, 0.0f));
    Rig quiet;                                                       // no breath, no pressure, no burst
    quiet.body.setConstriction(0, 0.82f, -1.0f);
    quiet.run(0.2f);
    quiet.body.clearConstriction(0);
    quiet.run(0.2f);
    CHECK(quiet.body.bursts() == 0);
}

// turbulence is zero when shut, when wide, and when breath is 0.
MM_TEST(body_turbulence_window) {
    auto turb = [](float degree, float breath) {
        Rig r;
        r.body.setBreath(breath);
        r.body.setConstriction(0, 0.82f, degree);
        r.run(0.5f);
        return r.body.frame().turb[0];
    };
    CHECK(turb(-1.0f, 1.0f) == 0.0f);                                // shut
    CHECK(turb(0.8f, 1.0f) == 0.0f);                                 // wide
    CHECK(turb(0.15f, 0.0f) == 0.0f);                                // no breath
    CHECK(turb(0.15f, 1.0f) > 0.1f);                                 // in the window, with air
    CHECK(turb(0.30f, 1.0f) < turb(0.15f, 1.0f));                    // the window's weak edge (TH, DH)

    // a closure anywhere stops the air: the other sources fall silent too
    Rig r;
    r.body.setBreath(1.0f);
    r.body.setConstriction(0, 0.82f, 0.15f);
    r.body.setConstriction(1, 0.5f, -1.0f);
    r.run(0.5f);
    CHECK(r.body.frame().turb[0] == 0.0f);

    // air that leaves through the nose does not hiss in the mouth: the rule that
    // keeps nasals from bursting. part open, some still does (the benchmark's pose).
    auto nasalTurb = [](float velum) {
        Rig n;
        n.body.setBreath(1.0f);
        n.body.setVelum(velum);
        n.body.setConstriction(0, 0.82f, 0.15f);
        n.run(0.5f);
        return n.body.frame().turb[0];
    };
    CHECK(nasalTurb(1.0f) == 0.0f);
    CHECK(nasalTurb(0.3f) > 0.0f && nasalTurb(0.3f) < nasalTurb(0.0f));

    // voiced fricatives hiss less than voiceless
    Rig voiced, voiceless;
    voiced.body.setBreath(1.0f);    voiced.body.setConstriction(0, 0.82f, 0.15f);
    voiceless.body.setBreath(1.0f); voiceless.body.setConstriction(0, 0.82f, 0.15f); voiceless.body.setGlottis(1.0f);
    voiced.run(0.5f); voiceless.run(0.5f);
    CHECK(voiced.body.frame().turb[0] < voiceless.body.frame().turb[0]);
    CHECK(voiced.body.frame().voicing > 0.5f);
    CHECK(voiceless.body.frame().voicing == 0.0f);
}

// a breath of 0 arrives: it aims below zero like a closure does.
MM_TEST(body_breath_reaches_zero) {
    Rig r;
    r.body.setBreath(1.0f);
    r.run(0.3f);
    CHECK(r.body.breath() > 0.99f);
    r.body.setBreath(0.0f);
    r.run(0.3f);
    CHECK(r.body.breath() == 0.0f);
    CHECK(r.body.frame().voicing == 0.0f && r.body.frame().aspiration == 0.0f);
}

// positions are fractions of tract length, so a pose means the same thing at
// every voice size, whole or not: its formants scale with 1/N and little else
// (a section at a region's edge is in both in part, which is near, not exact).
MM_TEST(body_pose_is_size_independent) {
    const float poses[][3] = {{0.30f, 2.41f, 1.5f}, {0.62f, 2.32f, 1.5f}, {0.52f, 2.15f, 0.5f}, {0.41f, 2.56f, 1.5f}};
    for (const float* p : poses) {
        Pose pose;
        pose.tongueX = p[0]; pose.tongueD = p[1]; pose.lipD = p[2];
        float d[kMaxSections], ends[2];
        shapeTract(pose, 22, d);
        const std::vector<double> ref = formants(22, d, kVelumShut);
        for (int quarter = 4 * kTestedSizeMin; quarter <= 4 * kTestedSizeMax; ++quarter) {
            const float n = 0.25f * (float)quarter;
            shapeTract(pose, n, d, nullptr, nullptr, Tuning(), ends);
            const std::vector<double> f = formants(n, d, kVelumShut, kDefaultFs, 5000.0, Tuning(), ends);
            CHECK(f.size() >= 2 && ref.size() >= 2);
            if (f.size() < 2 || ref.size() < 2) continue;
            for (int k = 0; k < 2; ++k) {
                const double scaled = f[(size_t)k] * n / 22.0;
                CHECK_MSG(std::fabs(scaled / ref[(size_t)k] - 1.0) < 0.15, "pose %.2f/%.2f N=%.2f F%d: %.0f vs %.0f",
                          p[0], p[1], n, k + 1, scaled, ref[(size_t)k]);
            }
        }
    }
}

// a size moves the sound continuously. every change in whole sections — the
// tract's, where the nose joins, the nose's — falls on a whole length, and
// across each one from 15 to 47, a thousandth of a section wide, a pose's
// response moves by less than 0.1 dB, the nasal ones too: the pieces at the
// ends are exactly the sections that grow whole there.
MM_TEST(body_size_moves_the_sound_continuously) {
    const float poses[][4] = {{0.30f, 2.41f, 1.5f, kVelumShut}, {0.62f, 2.32f, 1.5f, kVelumShut},
                              {0.52f, 2.15f, 0.5f, kVelumShut}, {0.41f, 2.56f, 1.5f, kVelumShut},
                              {0.41f, 2.56f, 1.5f, kVelumOpen}, {0.62f, 2.32f, 0.0f, kVelumOpen}};
    auto response = [](const Pose& pose, float length) {
        float d[kMaxSections], ends[2];
        shapeTract(pose, length, d, nullptr, nullptr, Tuning(), ends);
        const std::vector<float> ir = impulseResponse(length, d, pose.velumD, 4096, Tuning(), ends);
        const std::vector<double> mag = magnitudeSpectrum(ir.data(), (int)ir.size(), 8192);
        std::vector<double> db;
        for (size_t i = 22; i < 930; ++i) db.push_back(20.0 * std::log10(mag[i] + 1e-12));   // 120 .. 5000 Hz
        return db;
    };
    for (const float* p : poses) {
        Pose pose;
        pose.tongueX = p[0]; pose.tongueD = p[1]; pose.lipD = p[2]; pose.velumD = p[3];
        double worst = 0.0;
        int worstAt = 0;
        for (int whole = 15; whole <= 47; ++whole) {
            const std::vector<double> a = response(pose, (float)whole - 0.0005f), b = response(pose, (float)whole + 0.0005f);
            double sq = 0.0;
            for (size_t i = 0; i < a.size(); ++i) sq += (a[i] - b[i]) * (a[i] - b[i]);
            const double moved = std::sqrt(sq / (double)a.size());
            if (moved > worst) { worst = moved; worstAt = whole; }
        }
        CHECK_MSG(worst < 0.1, "pose %.2f/%.2f lips %.1f velum %.3f: %.3f dB across N=%d", p[0], p[1], p[2], p[3], worst,
                  worstAt);
    }
}

// the anatomy's landmarks fall where the reference puts them, at the reference's own size ratio.
MM_TEST(body_anatomy_landmarks) {
    Pose rest;
    float d[kMaxSections];
    shapeTract(rest, 22, d);
    CHECK(d[0] == 0.6f && d[2] == 0.6f);                             // throat: below 7/44
    CHECK(d[3] == 1.1f && d[4] == 1.1f);                             // below 12/44, until the tongue takes over at 10/44
    CHECK(d[19] == 1.5f && d[21] == 1.5f);                           // lips, open
    rest.lipD = 0.4f;
    shapeTract(rest, 22, d);
    CHECK(d[18] != 0.4f && d[19] == 0.4f && d[20] == 0.4f && d[21] == 0.4f);   // x >= 39/44, exactly
    for (int n = kMinSections; n <= kMaxSections; ++n) {             // every size: finite, positive, bounded
        Pose p;
        p.tongueX = kTongueXMax; p.tongueD = kTongueDMin;
        shapeTract(p, n, d);
        for (int i = 0; i < n; ++i) CHECK(d[i] > 0.2f && d[i] < 3.0f);
    }
}
