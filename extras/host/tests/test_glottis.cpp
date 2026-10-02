// wind_pipe — tests/test_glottis.cpp  (plan: Tests / Glottis)
#include <cmath>
#include <vector>

#include "core/wp_glottis.h"
#include "wp_analysis.h"
#include "wp_test.h"

using namespace wp;

// recurrence vs closed form within 1e-4 across tenseness 0..1 x f0 60..1000 Hz, all finite.
WP_TEST(glottis_recurrence_matches_closed_form) {
    const float f0s[] = {60.0f, 82.4f, 110.0f, 146.8f, 220.0f, 329.6f, 440.0f, 659.3f, 880.0f, 1000.0f};
    double worst = 0.0;
    for (float f0 : f0s) {
        for (int ti = 0; ti <= 20; ++ti) {
            const float tense = (float)ti / 20.0f;
            Glottis g;
            g.setTargets(f0, tense);
            g.init(kDefaultFs);
            bool finite = true;
            for (int i = 0; i < 8192; ++i) {
                const float t = g.phase();
                const LfShape sh = g.shape();
                const float y = g.step();
                const float ref = lfClosedForm(sh, t);
                if (!std::isfinite(y)) finite = false;
                const double err = std::fabs((double)y - (double)ref);
                if (err > worst) worst = err;
            }
            CHECK(finite);
        }
    }
    CHECK_MSG(worst < 1e-4, "worst error %.3g", worst);
}

// measured f0 within 0.1 %, from the waveform alone: the pulse crosses from
// + to - exactly once per period (at Tp).
WP_TEST(glottis_pitch_is_exact) {
    const float f0s[] = {60.0f, 97.3f, 130.81f, 261.63f, 523.25f, 987.77f};
    for (float f0 : f0s) {
        Glottis g;
        g.setTargets(f0, 0.6f);
        g.init(kDefaultFs);
        double first = -1.0, last = -1.0;
        int crossings = 0;
        float prev = 0.0f;
        for (int i = 0; i < 3 * 44100; ++i) {
            const float y = g.step();
            if (prev > 0.0f && y <= 0.0f && prev - y < 0.5f) {       // the smooth crossing at Tp, not the period seam
                const double at = (double)(i - 1) + (double)prev / ((double)prev - (double)y);
                if (first < 0.0) first = at;
                last = at;
                ++crossings;
            }
            prev = y;
        }
        const double measured = (crossings - 1) * (double)kDefaultFs / (last - first);
        CHECK_MSG(std::fabs(measured - f0) < 0.001 * f0, "f0 %.2f measured %.4f", f0, measured);
    }
}

// each period integrates to ~0: the pulse is a flow derivative, it carries no DC.
WP_TEST(glottis_period_integrates_to_zero) {
    for (int ti = 0; ti <= 10; ++ti) {
        const float tense = (float)ti / 10.0f;
        Glottis g;
        g.setTargets(100.0f, tense);                          // 441 samples per period exactly
        g.init(kDefaultFs);
        double sum = 0.0, absSum = 0.0;
        for (int i = 0; i < 441 * 50; ++i) { const float y = g.step(); sum += y; absSum += std::fabs(y); }
        CHECK_MSG(std::fabs(sum) < 1e-3 * absSum, "tenseness %.1f: sum %.4g of |sum| %.4g", tense, sum, absSum);
    }
}

// growl: every other period is the plain pulse times 1 - growl, sample for
// sample, starting with the second — and each weak period still integrates
// to zero.
WP_TEST(glottis_growl_weakens_every_other_period) {
    for (float growl : {0.0f, 0.3f, 1.0f, -0.5f}) {
        Glottis g, plain;
        g.setTargets(100.0f, 0.6f);                           // 441 samples per period exactly
        plain.setTargets(100.0f, 0.6f);
        g.setGrowl(growl);
        g.init(kDefaultFs);
        plain.init(kDefaultFs);
        int period = 0;
        double worst = 0.0, weakSum = 0.0, weakAbs = 0.0;
        for (int i = 0; i < 441 * 20; ++i) {
            const float t = plain.phase();
            const float y = g.step(), ref = plain.step();
            const float want = (period & 1) ? (1.0f - growl) * ref : ref;
            worst = std::fmax(worst, std::fabs((double)y - (double)want));
            if (period & 1) { weakSum += y; weakAbs += std::fabs(y); }
            if (plain.phase() < t) ++period;                  // the seam: the next sample starts a period
        }
        CHECK_MSG(period >= 19, "growl %.1f: %d periods", growl, period);
        CHECK_MSG(worst < 1e-6, "growl %.1f: off the scaled pulse by %.3g", growl, worst);
        CHECK_MSG(std::fabs(weakSum) <= 1e-3 * weakAbs + 1e-9, "growl %.1f: weak periods sum %.4g", growl, weakSum);
    }
}

// spectral tilt is monotonic in tenseness: a tenser fold is a brighter pulse.
WP_TEST(glottis_tilt_monotonic_in_tenseness) {
    double prev = -1e9;
    for (int ti = 2; ti <= 16; ++ti) {                        // Rd is clamped outside 0.1 .. 0.83
        const float tense = (float)ti / 20.0f;
        Glottis g;
        g.setTargets(110.25f, tense);                         // 400 samples per period
        g.init(kDefaultFs);
        std::vector<float> x(16384);
        for (float& v : x) v = g.step();
        const std::vector<double> mag = magnitudeSpectrum(x.data(), (int)x.size(), 16384, true);
        double lo = 0.0, hi = 0.0;
        for (size_t i = 1; i < mag.size(); ++i) {
            const double hz = (double)i * kDefaultFs / 16384.0;
            (hz < 1000.0 ? lo : hi) += mag[i] * mag[i];
        }
        const double tilt = 10.0 * std::log10(hi / lo);
        CHECK_MSG(tilt > prev, "tenseness %.2f: hi/lo %.2f dB, previous %.2f dB", tense, tilt, prev);
        prev = tilt;
    }
}

// f0 and tenseness latch once per period, and a pitch sweep stays finite and bounded.
WP_TEST(glottis_sweep_is_bounded) {
    Glottis g;
    g.init(kDefaultFs);
    float peak = 0.0f;
    for (int i = 0; i < 4 * 44100; ++i) {
        if ((i & 127) == 0) {
            const float f0 = 60.0f * std::pow(2.0f, 4.0f * (float)i / (4.0f * 44100.0f));
            g.setTargets(f0, 0.5f + 0.5f * std::sin((float)i * 1e-4f));
        }
        const float y = g.step();
        CHECK(std::isfinite(y));
        if (std::fabs(y) > peak) peak = std::fabs(y);
    }
    CHECK_MSG(peak < 1.5f, "peak %.3f", peak);
}
