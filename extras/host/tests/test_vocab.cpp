// monk_mouth — tests/test_vocab.cpp  (plan: Tests / Vocabulary)
#include <cstring>
#include <string>
#include <vector>

#include "core/mm_voice.h"
#include "mm_analysis.h"
#include "mm_test.h"

using namespace mm;

namespace {

struct F12 { double f1 = 0.0, f2 = 0.0; };

F12 formantsOf(uint8_t phone, int n = kDefaultSections, const Tuning& tuning = Tuning()) {
    Pose pose;
    pose.tongueX = vowel(phone).tongueX; pose.tongueD = vowel(phone).tongueD; pose.lipD = vowel(phone).lipD;
    float d[kMaxSections];
    shapeTract(pose, n, d, nullptr, nullptr, tuning);
    const std::vector<double> f = formants(n, d, kVelumShut, kDefaultFs, 5000.0, tuning);
    F12 r;
    if (f.size() > 0) r.f1 = f[0];
    if (f.size() > 1) r.f2 = f[1];
    return r;
}

// sing two syllables and hand back the audio with the second one's beat
struct Take { std::vector<float> x; int beat = 0; int firstBeat = 0; int bursts = 0; };

Take singPair(const char* first, const char* second) {
    const Syllable syls[] = {{first, 45.0f, 400}, {second, 45.0f, 500}};
    Event events[kMaxEvents];
    ScoreInfo info;
    ScoreOptions opt;
    Take t;
    if (compileSing(syls, 2, opt, events, kMaxEvents, &info) < 0) return t;
    t.firstBeat = (int)info.beat[0];
    t.beat = (int)info.beat[1];
    Voice v(kDefaultSections, 1);
    v.setWobble(0.0f);
    v.sing(syls, 2);
    t.x.resize((size_t)info.length + 22050);
    v.render(t.x.data(), (int)t.x.size());
    t.bursts = v.body().bursts();
    return t;
}

double windowDb(const std::vector<float>& x, int from, int len) {
    if (from < 0) from = 0;
    if (from + len > (int)x.size()) len = (int)x.size() - from;
    return toDb(rms(x.data() + from, len));
}

const int kMs = 44;                                                  // samples per millisecond, near enough

}  // namespace

// all 39 phonemes are there, parse by name, and nothing else does.
MM_TEST(vocab_covers_arpabet) {
    int vowels = 0, consonants = 0;
    for (uint8_t p = 1; p < ph::count; ++p) {
        const char* name = phoneName(p);
        CHECK(parsePhone(name, (int)strlen(name)) == p);
        if (isVowel(p)) ++vowels;
        if (isConsonant(p)) ++consonants;
    }
    CHECK(vowels == 15 && consonants == 24);
    CHECK(parsePhone("AH0", 3) == ph::AH && parsePhone("ow1", 3) == ph::OW && parsePhone("ng", 2) == ph::NG);
    CHECK(parsePhone("AX", 2) == ph::AH && parsePhone("ax0", 3) == ph::AH);      // the schwa is AH
    CHECK(parsePhone("XX", 2) == ph::none && parsePhone("A", 1) == ph::none && parsePhone("AAA", 3) == ph::none);
    CHECK(parsePhone("1", 1) == ph::none && parsePhone("", 0) == ph::none);
    for (uint8_t p = ph::firstConsonant; p <= ph::lastConsonant; ++p) {
        const Consonant& c = consonant(p);
        if (c.kind == kGlide || c.kind == kAspirate) continue;
        CHECK(c.place > 0.3f && c.place < 1.0f && c.closeRate > 0.0f && c.releaseRate > 0.0f && c.holdMs > 0);
        if (c.kind == kStop || c.kind == kNasal || c.kind == kAffricate) CHECK(c.degree < 0.0f);   // closures aim below zero
        if (c.kind == kFricative) CHECK(c.degree > 0.03f && c.degree < 0.4f);                       // inside the hiss window
        if (c.kind == kApproximant) CHECK(c.degree > 0.4f);                                         // above it
    }
    for (uint8_t p = ph::firstVowel; p <= ph::lastVowel; ++p)
        CHECK(vowel(p).lipD >= 0.4f && (!vowel(p).isDiphthong() || vowel(p).endLip >= 0.4f));       // vowels never hiss
}

// monophthong F1 and F2 within +-15 % of target (the soft gate).
MM_TEST(vocab_vowel_formants_near_target) {
    for (int k = 0; k < kFormantTargetCount; ++k) {
        const FormantTarget& t = kFormantTargets[k];
        const F12 f = formantsOf(t.phone);
        CHECK_MSG(std::fabs(f.f1 / t.f1 - 1.0) < 0.15, "%s F1 %.0f vs %.0f", phoneName(t.phone), f.f1, t.f1);
        CHECK_MSG(std::fabs(f.f2 / t.f2 - 1.0) < 0.15, "%s F2 %.0f vs %.0f", phoneName(t.phone), f.f2, t.f2);
    }
}

// the orderings hold strictly (the hard gate) — at every tested size.
MM_TEST(vocab_vowel_orderings) {
    const uint8_t f1chain[] = {ph::IY, ph::IH, ph::EH, ph::AE};
    const uint8_t f2chain[] = {ph::UW, ph::AO, ph::AA, ph::AH, ph::ER, ph::AE, ph::EH, ph::IH, ph::IY};
    for (int n = kTestedSizeMin; n <= kTestedSizeMax; ++n) {
        for (size_t i = 1; i < sizeof f1chain; ++i)
            CHECK_MSG(formantsOf(f1chain[i - 1], n).f1 < formantsOf(f1chain[i], n).f1, "N=%d F1 %s !< %s", n,
                      phoneName(f1chain[i - 1]), phoneName(f1chain[i]));
        for (size_t i = 1; i < sizeof f2chain; ++i)
            CHECK_MSG(formantsOf(f2chain[i - 1], n).f2 < formantsOf(f2chain[i], n).f2, "N=%d F2 %s !< %s", n,
                      phoneName(f2chain[i - 1]), phoneName(f2chain[i]));
    }
}

// the sung vowel carries the formants the static shape promised: each vowel
// puts its energy where its own F2 is, not where the other's is.
MM_TEST(vocab_sung_vowels_differ_as_their_shapes_do) {
    auto bandEnergy = [](uint8_t p, double centreHz) {
        const std::string name = phoneName(p);
        const Syllable syl[] = {{name.c_str(), 33.0f, 1500}};          // 55 Hz: harmonics dense enough to trace formants
        Voice v;
        v.setWobble(0.0f);
        v.setVibrato(0.0f, 0.0f, 0.0f);
        v.sing(syl, 1);
        std::vector<float> x(44100);
        v.render(x.data(), (int)x.size());
        const std::vector<double> mag = magnitudeSpectrum(x.data() + 22050, 16384, 16384, true);
        double e = 0.0;
        for (size_t i = (size_t)(centreHz * 0.88 * 16384 / kDefaultFs); i < (size_t)(centreHz * 1.12 * 16384 / kDefaultFs); ++i) e += mag[i] * mag[i];
        return e;
    };
    const uint8_t pairs[][2] = {{ph::AA, ph::IY}, {ph::UW, ph::IY}, {ph::AO, ph::EH}};
    for (const uint8_t* pr : pairs) {
        const double fa = formantsOf(pr[0]).f2, fb = formantsOf(pr[1]).f2;
        const double a = bandEnergy(pr[0], fa) / bandEnergy(pr[0], fb);   // a at its own F2 vs at b's
        const double b = bandEnergy(pr[1], fa) / bandEnergy(pr[1], fb);   // b at a's F2 vs at its own
        CHECK_MSG(a > 10.0 * b, "%s vs %s: %.3g vs %.3g", phoneName(pr[0]), phoneName(pr[1]), a, b);
    }
}

// stops: > 40 dB closure dip, a burst within 10 ms of release.
MM_TEST(vocab_stops_dip_and_burst) {
    for (uint8_t p : {(uint8_t)ph::B, (uint8_t)ph::D, (uint8_t)ph::G, (uint8_t)ph::P, (uint8_t)ph::T, (uint8_t)ph::K}) {
        for (const char* ctx : {"AA", "IY", "UW"}) {
            const std::string second = std::string(phoneName(p)) + " " + ctx;
            const Take t = singPair(ctx, second.c_str());
            CHECK(!t.x.empty());
            if (t.x.empty()) continue;
            const double vowelDb = windowDb(t.x, t.firstBeat + 150 * kMs, 100 * kMs);
            double floorDb = 0.0;                                     // quietest 5 ms in the 80 ms before release
            for (int at = t.beat - 80 * kMs; at < t.beat - 5 * kMs; at += 2 * kMs) {
                const double db = windowDb(t.x, at, 5 * kMs);
                if (db < floorDb) floorDb = db;
            }
            CHECK_MSG(vowelDb - floorDb > 40.0, "%s: dip %.1f dB", second.c_str(), vowelDb - floorDb);
            const double burstDb = windowDb(t.x, t.beat, 10 * kMs);
            CHECK_MSG(burstDb > floorDb + 25.0 && burstDb > vowelDb - 35.0, "%s: burst %.1f dB, floor %.1f, vowel %.1f",
                      second.c_str(), burstDb, floorDb, vowelDb);
            CHECK_MSG(t.bursts == 1, "%s: %d transients", second.c_str(), t.bursts);
        }
    }
}

// a burst is never louder than the vowel it opens into, at any size or
// diction. the same syllable sung with and without a burst differs by the
// burst alone: same seed, and the tube is linear.
MM_TEST(vocab_bursts_stay_under_the_vowel) {
    auto take = [](int n, const std::string& syl, float diction, float burstGain) {
        Voice v(n, 1);
        Tuning t;
        t.diction = diction;
        t.burstGain = burstGain;
        v.setTuning(t);
        v.setWobble(0.0f);
        v.noteOn(45.0f, 1.0f);
        std::vector<float> x(44100);
        v.render(x.data(), 250 * kMs);                               // the held vowel
        v.syllable(syl.c_str());
        v.render(x.data() + 250 * kMs, (int)x.size() - 250 * kMs);
        return x;
    };
    const char* vowels[] = {"AA", "AE", "AH", "AO", "EH", "ER", "IH", "IY", "UW"};
    for (int n : {16, 22, 26, 34}) {
        for (float diction : {0.3f, 1.0f, 3.0f}) {
            for (uint8_t p : {(uint8_t)ph::B, (uint8_t)ph::D, (uint8_t)ph::G, (uint8_t)ph::P, (uint8_t)ph::T, (uint8_t)ph::K}) {
                for (const char* ctx : vowels) {
                    const std::string syl = std::string(phoneName(p)) + " " + ctx;
                    const std::vector<float> with = take(n, syl, diction, Tuning().burstGain);
                    const std::vector<float> without = take(n, syl, diction, 0.0f);
                    float vowel = 0.0f, burst = 0.0f;
                    for (int i = 0; i < 250 * kMs; ++i) vowel = std::fmax(vowel, std::fabs(without[i]));
                    for (size_t i = 250 * kMs; i < with.size(); ++i) burst = std::fmax(burst, std::fabs(with[i] - without[i]));
                    CHECK(burst > 0.0f);
                    CHECK_MSG(burst < vowel, "N=%d diction %.1f %s: burst peak %.3f, vowel peak %.3f",
                              n, diction, syl.c_str(), burst, vowel);
                }
            }
        }
    }
}

// nasals murmur through the closure and do not burst.
MM_TEST(vocab_nasals_murmur_without_burst) {
    for (uint8_t p : {(uint8_t)ph::M, (uint8_t)ph::N, (uint8_t)ph::NG}) {
        const std::string second = std::string(phoneName(p)) + " AA";
        const Take t = singPair("AA", second.c_str());
        const double vowelDb = windowDb(t.x, t.firstBeat + 150 * kMs, 100 * kMs);
        const double murmurDb = windowDb(t.x, t.beat - 40 * kMs, 30 * kMs);
        CHECK_MSG(murmurDb > vowelDb - 20.0, "%s: murmur %.1f dB vs vowel %.1f", second.c_str(), murmurDb, vowelDb);
        CHECK_MSG(t.bursts == 0, "%s: %d transients", second.c_str(), t.bursts);
    }
}

// S centroid above SH — measured on the table's own places and degrees.
MM_TEST(vocab_s_is_brighter_than_sh) {
    auto centroidOf = [](uint8_t p) {
        const Consonant& c = consonant(p);
        Voice v;
        v.setGlottis(1.0f);
        v.setConstriction(0, c.place, c.degree);
        if (c.lipD > 0.0f) v.setLips(c.lipD / kLipDMax);
        v.noteOn(45.0f, 1.0f);
        std::vector<float> x(33075);
        v.render(x.data(), (int)x.size());
        CHECK(rms(x.data() + 16384, 16384) > 1e-4);                   // it does hiss
        return spectralCentroid(x.data() + 16384, 16384, kDefaultFs);
    };
    const double s = centroidOf(ph::S), sh = centroidOf(ph::SH);
    CHECK_MSG(s > sh * 1.3, "S %.0f Hz, SH %.0f Hz", s, sh);
    CHECK_MSG(s > 3500.0, "S %.0f Hz", s);
}

// voiceless spans are aperiodic; the vowels around them are not.
MM_TEST(vocab_voiceless_spans_are_aperiodic) {
    const int lagLo = (int)(kDefaultFs / 140.0f), lagHi = (int)(kDefaultFs / 90.0f);   // around 110 Hz
    for (uint8_t p : {(uint8_t)ph::S, (uint8_t)ph::SH, (uint8_t)ph::F, (uint8_t)ph::TH, (uint8_t)ph::HH}) {
        const std::string second = std::string(phoneName(p)) + " AA";
        const Take t = singPair("AA", second.c_str());
        const double inConsonant = periodicity(t.x.data() + t.beat - 60 * kMs, 2048, lagLo, lagHi);
        const double inVowel = periodicity(t.x.data() + t.beat + 200 * kMs, 2048, lagLo, lagHi);
        CHECK_MSG(inConsonant < 0.4, "%s: periodicity %.2f in the consonant", second.c_str(), inConsonant);
        CHECK_MSG(inVowel > 0.85, "%s: periodicity %.2f in the vowel", second.c_str(), inVowel);
    }
    const Take z = singPair("AA", "Z AA");                            // and a voiced fricative keeps its voice
    CHECK(periodicity(z.x.data() + z.beat - 60 * kMs, 2048, lagLo, lagHi) > 0.5);
}

// every phoneme, in three vowel contexts, both positions: renders, sounds, stays finite, ends in silence.
MM_TEST(vocab_every_phoneme_renders) {
    for (uint8_t p = ph::firstConsonant; p <= ph::lastConsonant; ++p) {
        for (const char* ctx : {"AA", "IY", "UW"}) {
            const std::string cv = std::string(phoneName(p)) + " " + ctx;
            const std::string vc = std::string(ctx) + " " + phoneName(p);
            const Syllable syls[] = {{cv.c_str(), 50.0f, 350}, {vc.c_str(), 52.0f, 400}};
            Voice v;
            CHECK(v.sing(syls, 2));
            std::vector<float> x(66150);
            v.render(x.data(), (int)x.size());
            bool finite = true;
            for (float s : x) if (!std::isfinite(s)) finite = false;
            CHECK(finite);
            CHECK_MSG(rms(x.data(), 33075) > 1e-3, "%s / %s is silent", cv.c_str(), vc.c_str());
            CHECK_MSG(x.back() == 0.0f && !v.busy(), "%s / %s never ended", cv.c_str(), vc.c_str());
            const BodyState st = v.state();
            CHECK(!st.slotActive[0] && !st.slotActive[1] && st.velum < 0.05f && st.glottis < 0.05f);
        }
    }
}
