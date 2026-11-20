// monk_mouth — tests/test_tube.cpp  (plan: Tests / Tube)
#include <cmath>
#include <vector>

#include "core/mm_glottis.h"
#include "core/mm_tube.h"
#include "mm_analysis.h"
#include "mm_test.h"

using namespace mm;

// a uniform tube resonates at (2n-1) Fs / 4N, +-2 %, at every tested size.
MM_TEST(tube_uniform_resonances) {
    for (int n = kTestedSizeMin; n <= kTestedSizeMax; ++n) {
        std::vector<float> d((size_t)n, 1.5f);
        const std::vector<double> f = formants(n, d.data(), kVelumShut, kDefaultFs, 6000.0);
        CHECK_MSG(f.size() >= 3, "N=%d found %d peaks", n, (int)f.size());
        if (f.size() < 3) continue;
        for (int j = 0; j < 3; ++j) {
            const double expect = (2 * j + 1) * kDefaultFs / (4.0 * n);
            CHECK_MSG(std::fabs(f[(size_t)j] - expect) < 0.02 * expect,
                      "N=%d F%d = %.1f Hz, expected %.1f", n, j + 1, f[(size_t)j], expect);
        }
    }
}

// between whole lengths too: a uniform tube a quarter, a half and three
// quarters of a section past each whole one resonates at (2n-1) Fs / 4L, +-2 %.
// its ends' pieces are lengths, not approximations of the nearest whole one.
MM_TEST(tube_uniform_resonances_between_whole_lengths) {
    for (int whole = kTestedSizeMin; whole < kTestedSizeMax; ++whole) {
        for (float part : {0.25f, 0.5f, 0.75f}) {
            const float length = (float)whole + part;
            const std::vector<float> d((size_t)layoutFor(length).n, 1.5f);
            const std::vector<double> f = formants(length, d.data(), kVelumShut, kDefaultFs, 6000.0);
            CHECK_MSG(f.size() >= 3, "N=%.2f found %d peaks", length, (int)f.size());
            if (f.size() < 3) continue;
            for (int j = 0; j < 3; ++j) {
                const double expect = (2 * j + 1) * kDefaultFs / (4.0 * length);
                CHECK_MSG(std::fabs(f[(size_t)j] - expect) < 0.02 * expect,
                          "N=%.2f F%d = %.1f Hz, expected %.1f", length, j + 1, f[(size_t)j], expect);
            }
        }
    }
}

// a whole length lays out as a tube built at it always has: every section
// whole, the nose joined and as long as the reference's ratios round to.
MM_TEST(tube_whole_lengths_lay_out_as_ever) {
    for (int n = kMinSections; n <= kMaxSections; ++n) {
        const Layout at = layoutFor((float)n);
        CHECK_MSG(at.n == n && at.ns == (int)std::lround(n * 17.0 / 44.0) && at.m == (int)std::lround(n * 28.0 / 44.0),
                  "N=%d lays out as %d, joined at %d, nose %d", n, at.n, at.ns, at.m);
        CHECK_MSG(at.glottis == 0.0f && at.lips == 0.0f && at.nostrils == 0.0f, "N=%d has pieces left: %g %g %g", n,
                  at.glottis, at.lips, at.nostrils);
    }
}

// random static shapes: an impulse's energy decays, whatever the shape.
MM_TEST(tube_energy_decays) {
    Rng rng; rng.seed(1234);
    for (int trial = 0; trial < 200; ++trial) {
        const int n = kMinSections + (int)(rng.next() % (kMaxSections - kMinSections + 1));
        float d[kMaxSections];
        for (int i = 0; i < n; ++i) {
            d[i] = 1.5f + 1.7f * rng.bipolar();                 // -0.2 .. 3.2: includes closures
        }
        const float velum = kVelumShut + (kVelumOpen - kVelumShut) * (0.5f + 0.5f * rng.bipolar());
        Tube tube(n);
        tube.setShape(d, velum);
        tube.snap();
        tube.step(1.0f);
        float peak = tube.energy();
        bool finite = true;
        for (int i = 0; i < 8192; ++i) {
            const float y = tube.step(0.0f);
            if (!std::isfinite(y)) finite = false;
            if (i < 64) { const float e = tube.energy(); if (e > peak) peak = e; }
        }
        CHECK(finite);
        CHECK_MSG(tube.energy() < 1e-6f * peak, "trial %d: energy %g of peak %g", trial, tube.energy(), peak);
    }
}

static double drivenRms(int n, const float* d, float velum) {
    Tube tube(n);
    tube.setShape(d, velum);
    tube.snap();
    Rng rng; rng.seed(99);
    std::vector<float> out(44100);
    for (size_t i = 0; i < out.size(); ++i) out[i] = tube.step(rng.bipolar());
    return rms(out.data() + 4410, (int)out.size() - 4410);
}

// the engine's own signal path: glottal pulse -> tube -> halfband.
// (a closed section, clamped at k = +-0.999, is a two-sample resonator that
// leaks at exactly DC and Fs/2; the pulse has no DC and the halfband has no
// Fs/2, so white noise is the wrong probe for a closure.)
static double voicedRms(int n, const float* d, float velum) {
    Tube tube(n);
    tube.setShape(d, velum);
    tube.snap();
    Glottis g;
    g.setTargets(110.0f, 0.6f);
    g.init(kDefaultFs);
    Halfband hb;
    std::vector<float> out(44100);
    for (size_t i = 0; i < out.size(); ++i) out[i] = hb.process(tube.step(g.step()));
    return rms(out.data() + 4410, (int)out.size() - 4410);
}

// a closed section drops the output > 50 dB. sealed velum: this is the tube's
// own closure. with the reference's "shut" velum of 0.01 the nose leaks a
// little, and the floor is the stop spec's 40 dB instead.
MM_TEST(tube_closure_drops_output) {
    for (int n : {16, 22, 26}) {
        for (int where : {n / 2, n * 3 / 4, n - 1}) {
            std::vector<float> open((size_t)n, 1.5f), shut((size_t)n, 1.5f);
            shut[(size_t)where] = 0.0f;
            const double sealed = toDb(voicedRms(n, open.data(), 0.0f)) - toDb(voicedRms(n, shut.data(), 0.0f));
            CHECK_MSG(sealed > 50.0, "N=%d closed at %d: dip %.1f dB", n, where, sealed);
            const double leaky = toDb(voicedRms(n, open.data(), kVelumShut)) - toDb(voicedRms(n, shut.data(), kVelumShut));
            CHECK_MSG(leaky > 40.0, "N=%d closed at %d, velum %.2f: dip %.1f dB", n, where, kVelumShut, leaky);
        }
    }
}

// with the mouth shut and the velum open the output survives: the murmur.
MM_TEST(tube_nasal_murmur_survives) {
    for (int n : {16, 22, 26}) {
        std::vector<float> open((size_t)n, 1.5f), shut((size_t)n, 1.5f);
        shut[(size_t)n - 1] = 0.0f;
        const double openDb = toDb(drivenRms(n, open.data(), kVelumShut));
        const double sealedDb = toDb(drivenRms(n, shut.data(), kVelumShut));
        const double murmurDb = toDb(drivenRms(n, shut.data(), kVelumOpen));
        CHECK_MSG(murmurDb > openDb - 30.0, "N=%d murmur %.1f dB vs open %.1f dB", n, murmurDb, openDb);
        CHECK_MSG(murmurDb > sealedDb + 25.0, "N=%d murmur %.1f dB vs sealed %.1f dB", n, murmurDb, sealedDb);
    }
}

// with the velum shut the three-port collapses to the plain two-port junction:
// the nose does not colour an oral vowel.
MM_TEST(tube_shut_velum_is_transparent) {
    std::vector<float> d(22, 1.5f);
    const std::vector<double> f = formants(22, d.data(), kVelumShut);
    const std::vector<double> g = formants(22, d.data(), 0.0f);
    CHECK(f.size() == g.size());
    for (size_t i = 0; i < f.size() && i < g.size(); ++i) CHECK(std::fabs(f[i] - g[i]) < 2.0);
}

// the halfband passes DC at unity, kills Fs/2, and halves Fs/4.
MM_TEST(halfband_response) {
    auto gainAt = [](double cyclesPerSample) {
        Halfband hb;
        double peak = 0.0;
        for (int i = 0; i < 400; ++i) {
            const float y = hb.process((float)std::cos(2.0 * M_PI * cyclesPerSample * i));
            if (i > 100 && std::fabs(y) > peak) peak = std::fabs(y);
        }
        return peak;
    };
    CHECK(std::fabs(gainAt(0.0) - 1.0) < 1e-5);
    CHECK(gainAt(0.5) < 1e-5);
    CHECK(std::fabs(gainAt(0.25) - 0.5) < 1e-3);
    CHECK(gainAt(0.05) > 0.99);
    CHECK(gainAt(0.40) < 0.05);
}
