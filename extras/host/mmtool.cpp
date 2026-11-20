// monk_mouth — mmtool.cpp  (plan §11)
// the one CLI. renders WAVs for the ear and numbers for the tables.
//
//   mmtool vowel AA --midi 48 --sec 2 -o aa.wav         ("rest" = the reference's rest shape)
//   mmtool pose --tongue .3 .8 --lips .9 -o pose.wav
//   mmtool formants --tongue .3 .8 --lips .9            F1..F4 of a static shape (or: formants AA)
//   mmtool fit                                          grid-search every vowel pose
//   mmtool syllable "B AA" -o ba.wav
//   mmtool sing song.txt -o song.wav                    one "phones midi ms" per line
//   mmtool say "HH AH0 L OW1 / W ER1 L D" -o hello.wav
//   mmtool speak "S T AA1 P / DH AE2 T" --midi 50 --range 5 --speed 1 -o stop.wav   spoken, not sung
//   mmtool corpus corpus build/corpus                   everything, for the ear
//   mmtool trace "AA B AA"                              10 ms frames: what the body did, what came out
//                                                       (--speak: through speak(), with --range --speed)
//   mmtool throat [step]                                sweep the throat: where the vowel gates hold
//   mmtool whistle --tongue .8 .6 --lips .2 -o w.wav    a live whistle pose: lock, level, pitch against the lip modes
//   mmtool whistle 84 -o w.wav                          a note through Voice::whistle and the table: the same report
//   mmtool whistle fit                                  grid-search the whistle table; prints the block for mm_vocab.cpp
//
// common options: --size N (sections, whole or not, 16..26)  --seed S  --midi M  --sec T
//                 --breath B  --tense T  --no-halfband  --no-wobble
//                 --tune NAME=VALUE (any field of mm::Tuning; repeatable)
//                 --png FILE (a spectrogram of the render: 0-8 kHz, ticks every 100 ms)
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "core/mm_voice.h"
#include "mm_analysis.h"
#include "mm_png.h"
#include "mm_wav.h"

using namespace mm;

namespace {

struct Args {
    std::vector<std::string> pos;
    std::string out = "out.wav";
    std::string png;                                       // --png: a spectrogram of whatever was rendered
    float size = kDefaultSections;
    uint32_t seed = 1;
    float midi = 48.0f, sec = 2.0f, breath = 0.8f, tense = -1.0f;
    float tonguePos = -1.0f, tongueHeight = -1.0f, lips = -1.0f, velum = -1.0f;
    float slotPlace = -1.0f, slotDegree = 0.0f;           // --slot: a free constriction (slot 0)
    bool  secGiven = false, midiGiven = false, breathGiven = false;
    float range = Speech().range, speed = Speech().speed;   // speak
    bool  speak = false;                                   // trace: through speak()
    bool  halfband = true, wobble = true;
    Tuning tuning;                                         // --tune: who is singing
    bool  tuned = false;
};

// --tune NAME=VALUE: every field of mm::Tuning, by name
struct TuneField { const char* name; float Tuning::* field; };
const TuneField kTuneFields[] = {
    {"glottisRest", &Tuning::glottisRest},             {"growl", &Tuning::growl},
    {"aspirationGain", &Tuning::aspirationGain},       {"aspirationHz", &Tuning::aspirationHz},
    {"aspirationQ", &Tuning::aspirationQ},             {"noiseVoicedFloor", &Tuning::noiseVoicedFloor},
    {"noiseVoicedPulse", &Tuning::noiseVoicedPulse},   {"noiseUnvoiced", &Tuning::noiseUnvoiced},
    {"glottalReflection", &Tuning::glottalReflection}, {"lipReflection", &Tuning::lipReflection},
    {"damp", &Tuning::damp},                           {"throatD", &Tuning::throatD},
    {"throatUpperD", &Tuning::throatUpperD},           {"velumRest", &Tuning::velumRest},
    {"lipRound", &Tuning::lipRound},                   {"gain", &Tuning::gain},
    {"turbulenceGain", &Tuning::turbulenceGain},       {"fricationHz", &Tuning::fricationHz},
    {"fricationQ", &Tuning::fricationQ},               {"voicedFrication", &Tuning::voicedFrication},
    {"burstGain", &Tuning::burstGain},                 {"burstHalfLife", &Tuning::burstHalfLife},
    {"diction", &Tuning::diction},                     {"rateBreathUp", &Tuning::rateBreathUp},
    {"rateBreathDown", &Tuning::rateBreathDown},       {"rateGlottis", &Tuning::rateGlottis},
    {"rateTenseness", &Tuning::rateTenseness},         {"rateTongue", &Tuning::rateTongue},
    {"rateLips", &Tuning::rateLips},                   {"rateVelum", &Tuning::rateVelum},
    {"vibFadeRate", &Tuning::vibFadeRate},             {"wobbleSlowHz", &Tuning::wobbleSlowHz},
    {"wobbleFastHz", &Tuning::wobbleFastHz},           {"wobbleSlowCents", &Tuning::wobbleSlowCents},
    {"wobbleFastCents", &Tuning::wobbleFastCents},     {"breathPressure", &Tuning::breathPressure},
    {"jetLength", &Tuning::jetLength},                 {"jetGain", &Tuning::jetGain},
};

bool parseTune(const std::string& s, Tuning& t) {
    const size_t eq = s.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= s.size()) return false;
    const std::string name = s.substr(0, eq);
    for (const TuneField& f : kTuneFields)
        if (name == f.name) { t.*f.field = (float)atof(s.c_str() + eq + 1); return true; }
    fprintf(stderr, "not a tunable: %s\n", name.c_str());
    return false;
}

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 2; i < argc; ++i) {
        const std::string s = argv[i];
        auto num = [&](float& v) { if (i + 1 >= argc) return false; v = (float)atof(argv[++i]); return true; };
        if (s == "-o") { if (i + 1 >= argc) return false; a.out = argv[++i]; }
        else if (s == "--png") { if (i + 1 >= argc) return false; a.png = argv[++i]; }
        else if (s == "--size") { float v; if (!num(v)) return false; a.size = v; }
        else if (s == "--seed") { float v; if (!num(v)) return false; a.seed = (uint32_t)v; }
        else if (s == "--midi") { if (!num(a.midi)) return false; a.midiGiven = true; }
        else if (s == "--sec") { if (!num(a.sec)) return false; a.secGiven = true; }
        else if (s == "--breath") { if (!num(a.breath)) return false; a.breathGiven = true; }
        else if (s == "--range") { if (!num(a.range)) return false; }
        else if (s == "--speed") { if (!num(a.speed)) return false; }
        else if (s == "--speak") a.speak = true;
        else if (s == "--tense") { if (!num(a.tense)) return false; }
        else if (s == "--tongue") { if (!num(a.tonguePos) || !num(a.tongueHeight)) return false; }
        else if (s == "--lips") { if (!num(a.lips)) return false; }
        else if (s == "--velum") { if (!num(a.velum)) return false; }
        else if (s == "--slot") { if (!num(a.slotPlace) || !num(a.slotDegree)) return false; }
        else if (s == "--tune") { if (i + 1 >= argc || !parseTune(argv[++i], a.tuning)) return false; a.tuned = true; }
        else if (s == "--no-halfband") a.halfband = false;
        else if (s == "--no-wobble") a.wobble = false;
        else a.pos.push_back(s);
    }
    return true;
}

void setup(Voice& v, const Args& a) {
    if (a.tuned) v.setTuning(a.tuning);
    v.setHalfband(a.halfband);
    if (!a.wobble) v.setWobble(0.0f);
    if (a.tense >= 0.0f) v.setTenseness(a.tense);
}

void renderSeconds(Voice& v, float sec, std::vector<float>& out) {
    const size_t n = (size_t)(sec * v.sampleRate());
    const size_t at = out.size();
    out.resize(at + n);
    v.render(out.data() + at, (int)n);
}

// render until the score is done and the voice has rung out
void renderScore(Voice& v, std::vector<float>& out) {
    for (int guard = 0; guard < 600; ++guard) {
        renderSeconds(v, 0.1f, out);
        if (!v.busy()) break;
    }
    renderSeconds(v, 0.6f, out);
}

int finish(const std::vector<float>& x, const Args& a) {
    float peak = 0.0f;
    for (float s : x) if (std::fabs(s) > peak) peak = std::fabs(s);
    if (!writeWav(a.out.c_str(), x, (int)kDefaultFs)) { fprintf(stderr, "cannot write %s\n", a.out.c_str()); return 1; }
    printf("%s  %.2f s  peak %.3f  rms %.1f dB\n", a.out.c_str(), (double)x.size() / kDefaultFs, peak,
           toDb(rms(x.data(), (int)x.size())));
    if (!a.png.empty() && !writeSpectrogram(a.png.c_str(), x, kDefaultFs)) { fprintf(stderr, "cannot write %s\n", a.png.c_str()); return 1; }
    return 0;
}

// ---- vowel / pose ---------------------------------------------------------------------------------

int cmdVowel(const Args& a) {
    Voice v(a.size, a.seed);
    setup(v, a);
    std::vector<float> x;
    const std::string name = a.pos.empty() ? "rest" : a.pos[0];
    if (name == "rest") {                                  // the reference's rest shape
        v.noteOn(a.midi, a.breath);
        renderSeconds(v, a.sec, x);
        v.noteOff();
        renderSeconds(v, 0.6f, x);
        return finish(x, a);
    }
    const uint8_t p = parsePhone(name.c_str(), (int)name.size());
    if (!isVowel(p)) { fprintf(stderr, "not a vowel: %s\n", name.c_str()); return 1; }
    v.setBreath(a.breath);                                 // the level sing() will use...
    v.setBreath(0.0f);                                     // ...without sounding yet
    const Syllable s[] = {{name.c_str(), a.midi, (uint16_t)(a.sec * 1000.0f)}};
    if (!v.sing(s, 1)) return 1;
    renderScore(v, x);
    return finish(x, a);
}

int cmdPose(const Args& a) {
    Voice v(a.size, a.seed);
    setup(v, a);
    if (a.tonguePos >= 0.0f) v.setTongue(a.tonguePos, a.tongueHeight);
    if (a.lips >= 0.0f) v.setLips(a.lips);
    if (a.velum >= 0.0f) v.setVelum(a.velum);
    std::vector<float> x;
    renderSeconds(v, 0.3f, x);                             // let the body arrive, silently
    v.noteOn(a.midi, a.breath);
    renderSeconds(v, a.sec, x);
    v.noteOff();
    renderSeconds(v, 0.6f, x);
    return finish(x, a);
}

// ---- formants / fit -----------------------------------------------------------------------------------

std::vector<double> poseFormants(float n, const Tuning& tuning, float tongueX, float tongueD, float lipD,
                                 float velumD = kVelumShut) {
    Pose pose;
    pose.tongueX = tongueX; pose.tongueD = tongueD; pose.lipD = lipD; pose.velumD = velumD;
    float d[kMaxSections];
    shapeTract(pose, n, d, nullptr, nullptr, tuning);
    return formants(n, d, velumD, kDefaultFs, 5000.0, tuning);
}

int cmdFormants(const Args& a) {
    float x, dd, lip;
    if (!a.pos.empty()) {
        const uint8_t p = parsePhone(a.pos[0].c_str(), (int)a.pos[0].size());
        if (!isVowel(p)) { fprintf(stderr, "not a vowel: %s\n", a.pos[0].c_str()); return 1; }
        const VowelPose& vp = vowel(p);
        x = vp.tongueX; dd = vp.tongueD; lip = vp.lipD;
    } else {
        x = lerpf(kTongueXMin, kTongueXMax, a.tonguePos < 0 ? 0.05f : a.tonguePos);
        dd = lerpf(kTongueDMax, kTongueDMin, a.tongueHeight < 0 ? 0.74f : a.tongueHeight);
        lip = kLipDMax * (a.lips < 0 ? 1.0f : a.lips);
    }
    const float velumD = a.velum > 0.0f ? lerpf(kVelumShut, kVelumOpen, a.velum) : kVelumShut;
    const Tuning tuning = clamped(a.tuning, kDefaultFs);  // what a voice would make of it
    if (!a.pos.empty()) lip = clampf(vowelLips(lip, tuning.lipRound), 0.0f, kLipDMax);   // as the singer holds it
    const std::vector<double> f = poseFormants(a.size, tuning, x, dd, lip, velumD);
    printf("tongueX %.3f  tongueD %.3f  lipD %.3f  (N = %g)\n", x, dd, lip, a.size);
    for (size_t i = 0; i < f.size() && i < 4; ++i) printf("  F%d  %7.1f Hz\n", (int)i + 1, f[i]);
    Pose pose; pose.tongueX = x; pose.tongueD = dd; pose.lipD = lip;
    float d[kMaxSections];
    shapeTract(pose, a.size, d, nullptr, nullptr, tuning);
    printf("  d:");
    for (int i = 0; i < layoutFor(a.size).n; ++i) printf(" %.2f", d[i]);
    printf("\n");
    return 0;
}

struct GridPoint { float x, d, lip; double f1, f2, f3; };

double fitCost(const FormantTarget& t, const GridPoint& g) {
    const double e1 = std::log(g.f1 / t.f1), e2 = std::log(g.f2 / t.f2);
    double c = e1 * e1 + e2 * e2;
    if (t.f3 > 0.0f && g.f3 > 0.0) { const double e3 = std::log(g.f3 / t.f3); c += 0.5 * e3 * e3; }
    // an unrounded vowel should not borrow the lips to hit its numbers
    if (!t.rounded) { const double r = (kLipDMax - g.lip) / kLipDMax; c += 0.03 * r * r; }
    return c;
}

int cmdFit(const Args& a) {
    // ~7000 tubes: every (tongueX, tongueD, lipD) on a grid. lipD stops short
    // of the turbulence window (< 0.4 would hiss), so vowels stay clean.
    std::vector<GridPoint> grid;
    const Tuning tuning = clamped(a.tuning, kDefaultFs);
    const int nx = 23, nd = 21, nl = 15;
    for (int ix = 0; ix < nx; ++ix)
        for (int id = 0; id < nd; ++id)
            for (int il = 0; il < nl; ++il) {
                GridPoint g;
                g.x = lerpf(kTongueXMin, kTongueXMax, (float)ix / (nx - 1));
                g.d = lerpf(kTongueDMin, kTongueDMax, (float)id / (nd - 1));
                g.lip = lerpf(0.45f, kLipDMax, (float)il / (nl - 1));
                const std::vector<double> f = poseFormants(a.size, tuning, g.x, g.d, g.lip);
                if (f.size() < 3) continue;
                g.f1 = f[0]; g.f2 = f[1]; g.f3 = f[2];
                grid.push_back(g);
            }
    // the orderings are the hard gate, so they are part of the fit: keep each
    // vowel's best candidates, then pick one per vowel along the F2 chain —
    // UW < AO < AA < AH < ER < AE < EH < IH < IY, and along its top four the
    // F1 chain IY < IH < EH < AE — minimizing total cost. (the published
    // targets themselves put UW's F2 above AO's; the margin splits them.)
    const uint8_t chain[] = {ph::UW, ph::AO, ph::AA, ph::AH, ph::ER, ph::AE, ph::EH, ph::IH, ph::IY};
    const int chainLen = (int)(sizeof chain / sizeof chain[0]);
    const double margin = 1.05;
    const size_t keep = 400;

    auto targetOf = [](uint8_t p) -> const FormantTarget* {
        for (int k = 0; k < kFormantTargetCount; ++k) if (kFormantTargets[k].phone == p) return &kFormantTargets[k];
        return nullptr;
    };
    auto candidates = [&](const FormantTarget& t) {
        std::vector<std::pair<double, const GridPoint*>> c;
        for (const GridPoint& g : grid) c.push_back({fitCost(t, g), &g});
        std::sort(c.begin(), c.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
        if (c.size() > keep) c.resize(keep);
        return c;
    };
    auto inF1Chain = [](uint8_t p) { return p == ph::AE || p == ph::EH || p == ph::IH || p == ph::IY; };

    std::vector<std::vector<std::pair<double, const GridPoint*>>> cand;
    for (int i = 0; i < chainLen; ++i) cand.push_back(candidates(*targetOf(chain[i])));
    std::vector<std::vector<double>> best((size_t)chainLen);
    std::vector<std::vector<int>> from((size_t)chainLen);
    for (int i = 0; i < chainLen; ++i) {
        best[(size_t)i].assign(cand[(size_t)i].size(), 1e30);
        from[(size_t)i].assign(cand[(size_t)i].size(), -1);
        for (size_t b = 0; b < cand[(size_t)i].size(); ++b) {
            const GridPoint& gb = *cand[(size_t)i][b].second;
            if (i == 0) { best[0][b] = cand[0][b].first; continue; }
            for (size_t p = 0; p < cand[(size_t)i - 1].size(); ++p) {
                const GridPoint& gp = *cand[(size_t)i - 1][p].second;
                if (gb.f2 < gp.f2 * margin) continue;
                if (inF1Chain(chain[i]) && inF1Chain(chain[i - 1]) && gb.f1 * margin > gp.f1) continue;
                const double c = best[(size_t)i - 1][p] + cand[(size_t)i][b].first;
                if (c < best[(size_t)i][b]) { best[(size_t)i][b] = c; from[(size_t)i][b] = (int)p; }
            }
        }
    }
    std::vector<const GridPoint*> pick((size_t)ph::count, nullptr);
    {
        int at = -1; double c = 1e30;
        for (size_t b = 0; b < best[(size_t)chainLen - 1].size(); ++b)
            if (best[(size_t)chainLen - 1][b] < c) { c = best[(size_t)chainLen - 1][b]; at = (int)b; }
        if (at < 0) { fprintf(stderr, "no ordered fit exists\n"); return 1; }
        for (int i = chainLen - 1; i >= 0; --i) { pick[chain[i]] = cand[(size_t)i][(size_t)at].second; at = from[(size_t)i][(size_t)at]; }
    }
    pick[ph::UH] = candidates(*targetOf(ph::UH))[0].second;       // UH is in neither chain

    printf("// fitted by `mmtool fit` over %d tubes, N = %g, orderings enforced with a %.0f %% margin.\n",
           (int)grid.size(), (double)a.size, 100.0 * (margin - 1.0));
    printf("// targets: Peterson & Barney 1952, adult male.   tongueX tongueD lipD       F1 (target)  F2 (target)   residual\n");
    for (int k = 0; k < kFormantTargetCount; ++k) {
        const FormantTarget& t = kFormantTargets[k];
        const GridPoint* g = pick[t.phone];
        if (!g) continue;
        printf("static constexpr VowelPose k%s = {%.3ff, %.3ff, %.3ff, 0, 0, 0};   // %4.0f (%4.0f)  %5.0f (%5.0f)  %+5.1f%% %+5.1f%%%s\n",
               phoneName(t.phone), g->x, g->d, g->lip, g->f1, t.f1, g->f2, t.f2,
               100.0 * (g->f1 / t.f1 - 1.0), 100.0 * (g->f2 / t.f2 - 1.0),
               t.f3 > 0.0f ? "  (F3 in the cost)" : "");
    }
    return 0;
}

// ---- throat: where the vowel table survives another throat -------------------------------------------
// the vowel poses were fitted against one throat (0.6 / 1.1) and the throat
// moves formants. the tuning takes any throat; this is the guide to which ones
// keep the vowels. both vocab gates (tests/test_vocab.cpp) are asked of every
// throat on a grid: each monophthong within 15 % of its target at N = 22, and
// the F1 and F2 orderings strict at every human size, 16 to 26. prints the
// map, and the largest box around the fitted throat in which both hold.

enum : int { kThroatHolds = 0, kThroatOffTarget = 1, kThroatDisordered = 2 };
const int kHumanSizeMin = 16, kHumanSizeMax = 26;          // 12.7 to 20.6 cm

int throatGates(const Tuning& t) {
    auto f12 = [&](uint8_t p, int n, double& f1, double& f2) {
        const VowelPose& v = vowel(p);
        const std::vector<double> f = poseFormants(n, t, v.tongueX, v.tongueD, v.lipD);
        f1 = f.size() > 0 ? f[0] : 0.0;
        f2 = f.size() > 1 ? f[1] : 0.0;
    };
    int failed = kThroatHolds;
    for (int k = 0; k < kFormantTargetCount; ++k) {
        const FormantTarget& tg = kFormantTargets[k];
        double f1, f2;
        f12(tg.phone, kDefaultSections, f1, f2);
        if (std::fabs(f1 / tg.f1 - 1.0) >= 0.15 || std::fabs(f2 / tg.f2 - 1.0) >= 0.15) failed |= kThroatOffTarget;
    }
    const uint8_t f1chain[] = {ph::IY, ph::IH, ph::EH, ph::AE};
    const uint8_t f2chain[] = {ph::UW, ph::AO, ph::AA, ph::AH, ph::ER, ph::AE, ph::EH, ph::IH, ph::IY};
    for (int n = kHumanSizeMin; n <= kHumanSizeMax && !(failed & kThroatDisordered); ++n) {
        double f1[ph::count] = {}, f2[ph::count] = {};
        for (uint8_t p : f2chain) f12(p, n, f1[p], f2[p]);
        for (size_t i = 1; i < sizeof f1chain; ++i)
            if (!(f1[f1chain[i - 1]] < f1[f1chain[i]])) failed |= kThroatDisordered;
        for (size_t i = 1; i < sizeof f2chain; ++i)
            if (!(f2[f2chain[i - 1]] < f2[f2chain[i]])) failed |= kThroatDisordered;
    }
    return failed;
}

int cmdThroat(const Args& a) {
    const float lo = 0.2f, hi = 1.5f;
    const float step = a.pos.empty() ? 0.05f : (float)atof(a.pos[0].c_str());
    if (step < 0.005f || step > 0.5f) { fprintf(stderr, "usage: mmtool throat [step 0.005..0.5]\n"); return 1; }
    const Tuning fit;                                      // the fitted throat: the defaults
    const int cells = (int)std::lround((hi - lo) / step) + 1;
    const int fitD = (int)std::lround((fit.throatD - lo) / step), fitUpper = (int)std::lround((fit.throatUpperD - lo) / step);
    std::vector<int> map((size_t)(cells * cells));
    for (int j = 0; j < cells; ++j)
        for (int i = 0; i < cells; ++i) {
            Tuning t = a.tuning;
            t.throatD = lo + step * (float)i;
            t.throatUpperD = lo + step * (float)j;
            map[(size_t)(j * cells + i)] = throatGates(t);
        }

    printf("throatUpperD down, throatD across (%.2f .. %.2f, step %.2f).  # holds   t off target   o disordered   x both   F the fit\n",
           lo, lo + step * (float)(cells - 1), step);
    for (int j = cells - 1; j >= 0; --j) {
        printf("  %.2f  ", lo + step * (float)j);
        for (int i = 0; i < cells; ++i) {
            const int m = map[(size_t)(j * cells + i)];
            const char c = (i == fitD && j == fitUpper) ? (m == kThroatHolds ? 'F' : '!')
                         : m == kThroatHolds ? '#' : m == kThroatOffTarget ? 't' : m == kThroatDisordered ? 'o' : 'x';
            printf("%c", c);
        }
        printf("\n");
    }

    // the largest all-holding box that contains the fit
    int best = 0, bi0 = fitD, bi1 = fitD, bj0 = fitUpper, bj1 = fitUpper;
    for (int i0 = 0; i0 <= fitD; ++i0)
        for (int i1 = fitD; i1 < cells; ++i1)
            for (int j0 = 0; j0 <= fitUpper; ++j0)
                for (int j1 = fitUpper; j1 < cells; ++j1) {
                    bool holds = true;
                    for (int j = j0; j <= j1 && holds; ++j)
                        for (int i = i0; i <= i1 && holds; ++i)
                            holds = map[(size_t)(j * cells + i)] == kThroatHolds;
                    const int area = (i1 - i0 + 1) * (j1 - j0 + 1);
                    if (holds && area > best) { best = area; bi0 = i0; bi1 = i1; bj0 = j0; bj1 = j1; }
                }
    if (!best) { printf("the fitted throat itself does not hold\n"); return 1; }
    printf("largest box around the fit:  throatD %.2f .. %.2f   throatUpperD %.2f .. %.2f   (%d cells)\n",
           lo + step * (float)bi0, lo + step * (float)bi1, lo + step * (float)bj0, lo + step * (float)bj1, best);
    return 0;
}

// ---- words ------------------------------------------------------------------------------------------------

int cmdSyllable(const Args& a) {
    if (a.pos.empty()) return 1;
    Voice v(a.size, a.seed);
    setup(v, a);
    std::vector<float> x;
    v.noteOn(a.midi, a.breath);                            // live play: the player owns the note
    if (!v.syllable(a.pos[0].c_str())) { fprintf(stderr, "bad syllable: %s\n", a.pos[0].c_str()); return 1; }
    renderSeconds(v, a.sec > 1.9f ? 0.8f : a.sec, x);
    v.noteOff();
    renderSeconds(v, 0.6f, x);
    return finish(x, a);
}

int cmdSay(const Args& a) {
    if (a.pos.empty()) return 1;
    Voice v(a.size, a.seed);
    setup(v, a);
    v.setPitch(a.midi);
    std::vector<float> x;
    if (!v.say(a.pos[0].c_str())) { fprintf(stderr, "bad phonemes: %s\n", a.pos[0].c_str()); return 1; }
    renderScore(v, x);
    return finish(x, a);
}

// speak(): the line at --midi (or the voice's note), --range, --speed, --breath (or the voice's level)
bool startSpeaking(Voice& v, const Args& a) {
    v.setPitch(a.midi);
    Speech sp;
    sp.midi = a.midiGiven ? a.midi : 0.0f;
    sp.range = a.range;
    sp.speed = a.speed;
    sp.breath = a.breathGiven ? a.breath : 0.0f;
    return v.speak(a.pos[0].c_str(), sp);
}

int cmdSpeak(const Args& a) {
    if (a.pos.empty()) return 1;
    Voice v(a.size, a.seed);
    setup(v, a);
    std::vector<float> x;
    if (!startSpeaking(v, a)) { fprintf(stderr, "bad phonemes: %s\n", a.pos[0].c_str()); return 1; }
    renderScore(v, x);
    return finish(x, a);
}

// a frame-by-frame readout of a say() (or, --speak, a speak()): what the body
// did and what came out. the numbers an ear would judge: level, periodicity,
// where the energy sits.
int cmdTrace(const Args& a) {
    if (a.pos.empty()) return 1;
    Voice v(a.size, a.seed);
    setup(v, a);
    v.setPitch(a.midi);
    if (!(a.speak ? startSpeaking(v, a) : v.say(a.pos[0].c_str()))) {
        fprintf(stderr, "bad phonemes: %s\n", a.pos[0].c_str());
        return 1;
    }
    const int frame = 441;                                 // 10 ms
    const int lagLo = (int)(kDefaultFs / 400.0f), lagHi = (int)(kDefaultFs / 60.0f);
    std::vector<float> x, hist;
    printf("   ms  phone  level dB  period  centroid  breath voicing  hiss closed  press  slot0      slot1      lips velum  whistle  whistleHz  glottis   midi    vib\n");
    for (int f = 0; f < 1000; ++f) {
        std::vector<float> buf((size_t)frame);
        v.render(buf.data(), frame);
        x.insert(x.end(), buf.begin(), buf.end());
        hist.insert(hist.end(), buf.begin(), buf.end());
        if (hist.size() > 2048) hist.erase(hist.begin(), hist.end() - 2048);
        const BodyState st = v.state();
        const double level = toDb(rms(buf.data(), frame));
        const double per = hist.size() >= 2048 && level > -80.0 ? periodicity(hist.data(), 2048, lagLo, lagHi) : 0.0;
        const double cen = level > -80.0 ? spectralCentroid(buf.data(), frame, kDefaultFs) : 0.0;
        char s0[16] = "    -    ", s1[16] = "    -    ";
        if (st.slotActive[0]) snprintf(s0, sizeof s0, "%.2f@%.2f", st.slotDegree[0], st.slotPlace[0]);
        if (st.slotActive[1]) snprintf(s1, sizeof s1, "%.2f@%.2f", st.slotDegree[1], st.slotPlace[1]);
        printf("%5d  %-5s  %8.1f  %6.2f  %8.0f  %6.2f %7.2f %5.2f %6d  %5.2f  %-9s  %-9s  %4.2f %5.2f  %7.2f  %9.1f  %7.2f  %5.2f  %5.1f\n",
               f * 10, phoneName(st.phoneme), level, per, cen, st.breath, st.voicing, st.hiss, (int)st.closed,
               st.pressure, s0, s1, st.lips, st.velum, st.whistle, st.whistleHz, st.glottis, st.midi, st.vibrato);
        if (!v.busy() && st.breath == 0.0f && level < -90.0) break;
    }
    if (a.out != "out.wav") return finish(x, a);
    return 0;
}

// one "phones midi ms" per line. "-" = melisma, "_" = rest, "#" starts a comment.
bool loadSong(const char* path, std::vector<std::string>& phones, std::vector<Syllable>& syls) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    std::vector<float> midi; std::vector<int> ms;
    while (fgets(line, sizeof line, f)) {
        if (char* hash = strchr(line, '#')) *hash = 0;
        std::vector<std::string> tok;
        for (char* t = strtok(line, " \t\r\n"); t; t = strtok(nullptr, " \t\r\n")) tok.push_back(t);
        if (tok.size() < 3) continue;
        std::string p;
        for (size_t i = 0; i + 2 < tok.size(); ++i) { if (i) p += " "; p += tok[i]; }
        if (p == "-") p = "";
        phones.push_back(p);
        midi.push_back((float)atof(tok[tok.size() - 2].c_str()));
        ms.push_back(atoi(tok[tok.size() - 1].c_str()));
    }
    fclose(f);
    for (size_t i = 0; i < phones.size(); ++i)
        syls.push_back(Syllable{phones[i].c_str(), midi[i], (uint16_t)ms[i]});
    return !syls.empty();
}

// long songs go to the voice a line at a time: a score holds 256 events.
bool singSong(Voice& v, const std::vector<Syllable>& syls, std::vector<float>& x) {
    size_t at = 0;
    while (at < syls.size()) {
        size_t n = syls.size() - at;
        if (n > 12) n = 12;
        while (n > 0 && !v.sing(&syls[at], (int)n)) --n;          // shrink until it fits
        if (n == 0) { fprintf(stderr, "cannot sing \"%s\"\n", syls[at].phones); return false; }
        for (int guard = 0; guard < 3000 && (guard == 0 || v.busy()); ++guard) renderSeconds(v, 0.01f, x);
        at += n;
    }
    renderSeconds(v, 0.6f, x);
    return true;
}

int cmdSing(const Args& a) {
    if (a.pos.empty()) return 1;
    std::vector<std::string> phones; std::vector<Syllable> syls;
    if (!loadSong(a.pos[0].c_str(), phones, syls)) { fprintf(stderr, "cannot read %s\n", a.pos[0].c_str()); return 1; }
    Voice v(a.size, a.seed);
    setup(v, a);
    std::vector<float> x;
    if (!singSong(v, syls, x)) return 1;
    return finish(x, a);
}

// ---- corpus: every phoneme in three vowel contexts plus the sung lines ---------------------------------

int cmdCorpus(const Args& a) {
    if (a.pos.size() < 2) { fprintf(stderr, "usage: mmtool corpus <corpusDir> <outDir>\n"); return 1; }
    const std::string in = a.pos[0], outDir = a.pos[1];
    mkdir(outDir.c_str(), 0755);
    const char* ctx[] = {"AA", "IY", "UW"};
    int files = 0;

    auto renderSyls = [&](const std::vector<Syllable>& syls, const std::string& name) {
        Voice v(a.size, a.seed);
        setup(v, a);
        std::vector<float> x;
        if (!singSong(v, syls, x)) return;
        Args o = a; o.out = outDir + "/" + name + ".wav";
        if (finish(x, o) == 0) ++files;
    };

    for (uint8_t p = ph::firstConsonant; p <= ph::lastConsonant; ++p) {
        for (const char* c : ctx) {                        // V, C V, V C
            const std::string cv = std::string(phoneName(p)) + " " + c;
            const std::string vc = std::string(c) + " " + phoneName(p);
            renderSyls({{c, a.midi, 350}, {cv.c_str(), a.midi, 450}, {vc.c_str(), a.midi + 2, 500}},
                       std::string("c_") + phoneName(p) + "_" + c);
        }
    }
    for (uint8_t p = ph::firstVowel; p <= ph::lastVowel; ++p) {
        const std::string bare = phoneName(p);
        const std::string b = "B " + bare, s = "S " + bare + " T";
        renderSyls({{bare.c_str(), a.midi, 500}, {b.c_str(), a.midi + 2, 500}, {s.c_str(), a.midi + 4, 600}},
                   "v_" + bare);
    }
    if (DIR* dir = opendir(in.c_str())) {
        while (dirent* e = readdir(dir)) {
            const std::string f = e->d_name;
            if (f.size() < 5 || f.substr(f.size() - 4) != ".txt") continue;
            std::vector<std::string> phones; std::vector<Syllable> syls;
            if (!loadSong((in + "/" + f).c_str(), phones, syls)) continue;
            renderSyls(syls, "line_" + f.substr(0, f.size() - 4));
        }
        closedir(dir);
    }
    printf("%d files in %s\n", files, outDir.c_str());
    return 0;
}

// ---- whistle: a live pose, a note through the table, the fit (whistle plan §7.2) -----------------------
// a pose: folds open, velum shut, the tongue and lips as given, an optional
// slot, and breath. a note: Voice::whistle, through the table. either way,
// after the first 300 ms: the lock (periodicity over 300 .. 6000 Hz), the
// level, the measured whistleHz against the pose's lip modes, the second and
// third harmonics against the fundamental, and the body's whistle.

const float kLockSkipSec = 0.3f;                           // the lock is read after this

// the lock report, for a voice that has rendered x
int whistleReport(const Voice& v, const std::vector<float>& x, double whistleMean) {
    const BodyState st = v.state();
    const int skip = (int)(kLockSkipSec * kDefaultFs);
    const int n = (int)x.size() - skip;
    if (n < 4096) { fprintf(stderr, "too short: --sec at least 0.4\n"); return 1; }
    const float* steady = x.data() + skip;
    const double per = periodicity(steady, n < 8192 ? n : 8192, (int)(kDefaultFs / 6000.0f), (int)(kDefaultFs / 300.0f));
    const double level = toDb(rms(steady, n));

    float d[kMaxSections];
    const int count = v.diameters(d, kMaxSections);
    const float velumD = lerpf(kVelumShut, kVelumOpen, st.velum);
    const std::vector<double> modes = whistleModes(count, d, velumD, v.tuning());
    double nearest = 0.0;
    for (double m : modes) if (nearest == 0.0 || std::fabs(m - st.whistleHz) < std::fabs(nearest - st.whistleHz)) nearest = m;

    int fftSize = 1;
    while (fftSize * 2 <= n) fftSize *= 2;
    const std::vector<double> mag = magnitudeSpectrum(steady + (n - fftSize), fftSize, fftSize, true);
    const double f0 = st.whistleHz;
    const double h1 = f0 > 0.0 ? peakNear(mag, kDefaultFs, f0, 0.03) : 0.0;
    const double h2 = f0 > 0.0 && 2.0 * f0 < 0.5 * kDefaultFs ? peakNear(mag, kDefaultFs, 2.0 * f0, 0.03) : 0.0;
    const double h3 = f0 > 0.0 && 3.0 * f0 < 0.5 * kDefaultFs ? peakNear(mag, kDefaultFs, 3.0 * f0, 0.03) : 0.0;

    printf("  jet       flow %.3f l/s  delay %.1f samples  half-width %.2f mm  lip area %.3f cm²\n",
           1e3 * v.body().frame().jetFlow, v.body().frame().jetDelay, 1e3 * v.body().frame().jetHalfWidth,
           1e4 * v.body().frame().lipArea);
    printf("  lock      periodicity %.3f   level %.1f dB\n", per, level);
    printf("  whistle   %.3f (mean after 300 ms %.3f)   whistleHz %.1f\n", st.whistle, whistleMean, st.whistleHz);
    printf("  lip modes");
    for (double m : modes) printf(" %.0f", m);
    printf("   nearest %.0f  (%+.1f %%)\n", nearest, nearest > 0.0 ? 100.0 * (st.whistleHz / nearest - 1.0) : 0.0);
    printf("  harmonics H2 %+.1f dB  H3 %+.1f dB\n", h1 > 0.0 ? toDb(h2 / h1) : 0.0, h1 > 0.0 ? toDb(h3 / h1) : 0.0);
    return 0;
}

// render in 10 ms frames, averaging the body's whistle after the skip
double renderWhistle(Voice& v, float sec, std::vector<float>& x) {
    const int frame = 441;
    const int skip = (int)(kLockSkipSec * kDefaultFs);
    double sum = 0.0;
    int frames = 0;
    for (int f = 0; f < (int)(sec * kDefaultFs) / frame; ++f) {
        std::vector<float> buf((size_t)frame);
        v.render(buf.data(), frame);
        x.insert(x.end(), buf.begin(), buf.end());
        if ((int)x.size() > skip) { sum += v.state().whistle; ++frames; }
    }
    return frames ? sum / frames : 0.0;
}

// a table pose on the live setters: what Voice::whistle writes, less the note
void setWhistlePose(Voice& v, const WhistlePose& p, float breath) {
    v.setGlottis(1.0f);
    v.setVelum(0.0f);
    v.setTongue((p.tongueX - kTongueXMin) / (kTongueXMax - kTongueXMin), (kTongueDMax - p.tongueD) / (kTongueDMax - kTongueDMin));
    v.setLips(p.lipD / kLipDMax);
    if (p.slotPlace >= 0.0f) v.setConstriction(0, p.slotPlace, p.slotDegree);
    else v.clearConstriction(0);
    v.clearConstriction(1);
    v.setBreath(breath);
}

double midiToHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
double hzToMidi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

// ---- the fit ----------------------------------------------------------------------------------------------
// a grid of whistle poses: the tongue as the vowel fit has it (23 x 21), the
// pucker over raw lipD 0.2 .. 0.9 (11 steps), and no slot or one behind the
// lips at 4 places x 3 degrees (a degree under the pucker at or past 0.86 is
// a slit, and skipped). screened by the lip modes (a lock sits on one: the
// pose needs one in the whistle band), then every survivor is rendered at the
// fit's breath and its lock measured as `whistle` measures it: periodicity
// after 300 ms, the body's whistle and whistleHz. every lock is rendered
// again at breath 0.5 and 1.0, and at the fit's breath at N = 16 and 26.
// per semitone, the candidates are the locks within half a semitone of the
// note; a viterbi pass picks one per semitone, trading the cents error, the
// lock's distance from its own lip mode (an off-mode lock drifts more with
// breath), a lock lost at 0.5 or 1.0, and the lock's error at the other
// sizes (the jet's transit does not scale with size, so a pose near its
// threshold loses the lock or jumps mode there) against pose jumps and slot
// flips between neighbours.

struct WhistleCand {
    WhistlePose p;
    double mode = 0.0;                                     // the screen's: the lowest lip mode in the band
    double hz = 0.0, per = 0.0, whistle = 0.0, own = 0.0;  // the lock, and the lip mode nearest it
    bool locked = false;
    int held = 0;                                          // of the breaths either side, how many it locks at too
    double sizeCost = 0.0;                                 // how far the lock strays at the other human sizes
};

const double kFitBandLo = 400.0, kFitBandHi = 4500.0;     // the lip modes a lock is looked for on
const double kLockPer = 0.95, kLockWhistle = 0.5;         // what a lock is
const int kFitSizeLo = 16, kFitSizeHi = 26;               // the human sizes a row is also asked to lock at
const double kSizeMiss = 9.0;                              // a size that does not lock costs a 300-cent miss

std::vector<double> poseLipModes(float n, const Tuning& tuning, const WhistlePose& p) {
    Pose pose;
    pose.tongueX = p.tongueX; pose.tongueD = p.tongueD; pose.lipD = p.lipD; pose.velumD = kVelumShut;
    if (p.slotPlace >= 0.0f) { pose.slot[0].active = true; pose.slot[0].place = p.slotPlace; pose.slot[0].degree = p.slotDegree; }
    float d[kMaxSections];
    shapeTract(pose, n, d, nullptr, nullptr, tuning);
    return whistleModes(n, d, kVelumShut, tuning);
}

// one render of a pose; the lock read after 300 ms. periodicity only for a
// voice whose own whistle is up: it is the costly half.
void measureLock(WhistleCand& c, const Args& a, float breath, float sec, int size = 0) {
    Voice v(size > 0 ? size : a.size, a.seed);
    if (a.tuned) v.setTuning(a.tuning);
    setWhistlePose(v, c.p, breath);
    std::vector<float> x((size_t)(sec * kDefaultFs));
    v.render(x.data(), (int)x.size());
    const BodyState st = v.state();
    c.hz = st.whistleHz;
    c.whistle = st.whistle;
    c.per = 0.0;
    c.locked = false;
    if (st.whistle <= kLockWhistle || st.whistleHz <= 0.0f) return;
    const int skip = (int)(kLockSkipSec * kDefaultFs);
    const int n = (int)x.size() - skip;
    c.per = periodicity(x.data() + skip, n < 8192 ? n : 8192, (int)(kDefaultFs / 6000.0f), (int)(kDefaultFs / 300.0f));
    c.locked = c.per > kLockPer;
    float d[kMaxSections];
    const int count = v.diameters(d, kMaxSections);
    c.own = 0.0;
    for (double m : whistleModes(count, d, kVelumShut, v.tuning()))
        if (c.own == 0.0 || std::fabs(m - c.hz) < std::fabs(c.own - c.hz)) c.own = m;
}

// run f(i) for i in [0, count) on every core
template <class F> void parallelFor(int count, F f) {
    std::atomic<int> next(0);
    unsigned cores = std::thread::hardware_concurrency();
    if (cores < 1) cores = 1;
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < cores; ++t)
        pool.emplace_back([&]() { for (int i = next++; i < count; i = next++) f(i); });
    for (std::thread& t : pool) t.join();
}

double centsOff(double hz, double midi) { return 1200.0 * std::log2(hz / midiToHz(midi)); }

// the continuity term between two neighbouring rows: the pose's jump in
// normalized units (the tongue's two axes over their range, the pucker over
// its span, a slot's place and degree over their grid's), and a slot on/off
// flip, which a slide hears as a step.
double poseJump(const WhistlePose& a, const WhistlePose& b) {
    const double dx = (a.tongueX - b.tongueX) / (kTongueXMax - kTongueXMin);
    const double dd = (a.tongueD - b.tongueD) / (kTongueDMax - kTongueDMin);
    const double dl = (a.lipD - b.lipD) / 0.7;
    double c = dx * dx + dd * dd + dl * dl;
    const bool sa = a.slotPlace >= 0.0f, sb = b.slotPlace >= 0.0f;
    if (sa && sb) {
        const double sp = (a.slotPlace - b.slotPlace) / 0.15, sd = (a.slotDegree - b.slotDegree) / 0.35;
        c += sp * sp + sd * sd;
    } else if (sa != sb) {
        c += 3.0;
    }
    return c;
}

int cmdWhistleFit(const Args& a) {
    const auto t0 = std::chrono::steady_clock::now();
    const Tuning tuning = clamped(a.tuning, kDefaultFs);
    const float breath = a.breath;
    const float sec = a.secGiven ? a.sec : 0.5f;

    // ---- the grid ----
    std::vector<WhistleCand> grid;
    const int nx = 23, nd = 21, nl = 11;
    const float places[] = {0.75f, 0.80f, 0.85f, 0.90f}, degrees[] = {0.25f, 0.4f, 0.6f};
    for (int ix = 0; ix < nx; ++ix)
        for (int id = 0; id < nd; ++id)
            for (int il = 0; il < nl; ++il) {
                WhistleCand c;
                c.p.tongueX = lerpf(kTongueXMin, kTongueXMax, (float)ix / (nx - 1));
                c.p.tongueD = lerpf(kTongueDMin, kTongueDMax, (float)id / (nd - 1));
                c.p.lipD = lerpf(0.2f, 0.9f, (float)il / (nl - 1));
                c.p.slotPlace = -1.0f; c.p.slotDegree = 0.0f;
                grid.push_back(c);
                for (float sp : places)
                    for (float sd : degrees) {
                        if (sp >= 0.86f && sd < c.p.lipD) continue;          // a slit at the lips: no jet
                        WhistleCand s = c;
                        s.p.slotPlace = sp; s.p.slotDegree = sd;
                        grid.push_back(s);
                    }
            }

    // ---- the screen: a lip mode in the whistle band ----
    parallelFor((int)grid.size(), [&](int i) {
        grid[(size_t)i].mode = 0.0;
        for (double m : poseLipModes(a.size, tuning, grid[(size_t)i].p))
            if (m >= kFitBandLo && m <= kFitBandHi) { grid[(size_t)i].mode = m; break; }
    });
    std::vector<WhistleCand> cands;
    for (const WhistleCand& c : grid) if (c.mode > 0.0) cands.push_back(c);
    const auto t1 = std::chrono::steady_clock::now();
    fprintf(stderr, "screened %d poses: %d with a lip mode in %.0f .. %.0f Hz (%.1f s)\n", (int)grid.size(),
            (int)cands.size(), kFitBandLo, kFitBandHi, std::chrono::duration<double>(t1 - t0).count());

    // ---- the renders ----
    parallelFor((int)cands.size(), [&](int i) { measureLock(cands[(size_t)i], a, breath, sec); });
    std::vector<WhistleCand*> locks;
    for (WhistleCand& c : cands) if (c.locked) locks.push_back(&c);
    // every lock again at breath 0.5 and 1.0: a pose that holds its lock only
    // near the fit's breath sits at its threshold and loses it to a softer
    // note. and at the ends of the human sizes, where whistle() plays the same
    // pose for a note shifted by the size: every mode scales with 1 / N, the
    // jet's transit does not.
    parallelFor((int)locks.size(), [&](int i) {
        WhistleCand& c = *locks[(size_t)i];
        WhistleCand soft, hard;
        soft.p = hard.p = c.p;
        measureLock(soft, a, 0.5f, sec);
        measureLock(hard, a, 1.0f, sec);
        c.held = (soft.locked ? 1 : 0) + (hard.locked ? 1 : 0);
        c.sizeCost = 0.0;
        for (int n : {kFitSizeLo, kFitSizeHi}) {
            WhistleCand other;
            other.p = c.p;
            measureLock(other, a, breath, sec, n);
            if (!other.locked) { c.sizeCost += kSizeMiss; continue; }
            const double e = 1200.0 * std::log2(other.hz / (c.hz * (double)a.size / (double)n)) / 100.0;
            c.sizeCost += e * e;
        }
    });
    const auto t2 = std::chrono::steady_clock::now();
    fprintf(stderr, "rendered %d at breath %.2f for %.2f s: %d lock, each rendered again at breath 0.5 and 1.0 and at N = %d and %d (%.1f s)\n",
            (int)cands.size(), breath, sec, (int)locks.size(), kFitSizeLo, kFitSizeHi,
            std::chrono::duration<double>(t2 - t1).count());
    if (locks.empty()) { fprintf(stderr, "nothing locks\n"); return 1; }

    // ---- per semitone: the locks within half a semitone of the note ----
    double loHz = 1e30, hiHz = 0.0;
    for (const WhistleCand* c : locks) { loHz = std::min(loHz, c->hz); hiHz = std::max(hiHz, c->hz); }
    const int mLo = (int)std::ceil(hzToMidi(loHz) - 0.5), mHi = (int)std::floor(hzToMidi(hiHz) + 0.5);
    const size_t keep = 120;
    // the cost, per row: (cents / 25)², 0.3 (cents off its own lip mode / 100)²,
    // 4 for each of breath 0.5 and 1.0 it loses the lock at, and 4 (size
    // error / 100 cents)² summed over N = 16 and 26 (a lost lock counts 300
    // cents); per step: the pose's jump, and 3 more for a slot flip.
    const double kOffWeight = 0.3, kJumpWeight = 1.0, kHeldWeight = 4.0, kSizeWeight = 4.0;
    auto local = [&](const WhistleCand& c, int m) {
        const double e = centsOff(c.hz, m) / 25.0;
        const double off = c.own > 0.0 ? 1200.0 * std::log2(c.hz / c.own) / 100.0 : 1.0;
        return e * e + kOffWeight * off * off + kHeldWeight * (2 - c.held) + kSizeWeight * c.sizeCost;
    };
    std::vector<std::vector<std::pair<double, const WhistleCand*>>> per;
    for (int m = mLo; m <= mHi; ++m) {
        std::vector<std::pair<double, const WhistleCand*>> c;
        for (const WhistleCand* w : locks)
            if (std::fabs(centsOff(w->hz, m)) <= 50.0) c.push_back({local(*w, m), w});
        std::sort(c.begin(), c.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
        if (c.size() > keep) c.resize(keep);
        per.push_back(c);
    }
    // the longest run of semitones that each have a lock
    int runLo = 0, runLen = 0;
    for (int i = 0; i < (int)per.size();) {
        if (per[(size_t)i].empty()) { ++i; continue; }
        int j = i;
        while (j < (int)per.size() && !per[(size_t)j].empty()) ++j;
        if (j - i > runLen) { runLo = i; runLen = j - i; }
        i = j;
    }
    if (runLen == 0) { fprintf(stderr, "no semitone has a lock within 50 cents\n"); return 1; }
    for (int i = 0; i < (int)per.size(); ++i)
        if (per[(size_t)i].empty()) fprintf(stderr, "midi %d: no lock within 50 cents\n", mLo + i);

    // ---- viterbi over the run ----
    std::vector<std::vector<double>> best((size_t)runLen);
    std::vector<std::vector<int>> from((size_t)runLen);
    for (int k = 0; k < runLen; ++k) {
        const auto& cur = per[(size_t)(runLo + k)];
        best[(size_t)k].assign(cur.size(), 1e30);
        from[(size_t)k].assign(cur.size(), -1);
        for (size_t b = 0; b < cur.size(); ++b) {
            if (k == 0) { best[0][b] = cur[b].first; continue; }
            const auto& prev = per[(size_t)(runLo + k - 1)];
            for (size_t p = 0; p < prev.size(); ++p) {
                const double c = best[(size_t)k - 1][p] + cur[b].first + kJumpWeight * poseJump(prev[p].second->p, cur[b].second->p);
                if (c < best[(size_t)k][b]) { best[(size_t)k][b] = c; from[(size_t)k][b] = (int)p; }
            }
        }
    }
    std::vector<const WhistleCand*> pick((size_t)runLen, nullptr);
    {
        int at = 0;
        for (size_t b = 1; b < best[(size_t)runLen - 1].size(); ++b)
            if (best[(size_t)runLen - 1][b] < best[(size_t)runLen - 1][(size_t)at]) at = (int)b;
        for (int k = runLen - 1; k >= 0; --k) {
            pick[(size_t)k] = per[(size_t)(runLo + k)][(size_t)at].second;
            at = from[(size_t)k][(size_t)at];
        }
    }

    // ---- the drift: each row at breath 0.5 and 1.0 ----
    std::vector<WhistleCand> soft((size_t)runLen), hard((size_t)runLen);
    for (int k = 0; k < runLen; ++k) { soft[(size_t)k].p = hard[(size_t)k].p = pick[(size_t)k]->p; }
    parallelFor(runLen, [&](int k) {
        measureLock(soft[(size_t)k], a, 0.5f, 1.0f);
        measureLock(hard[(size_t)k], a, 1.0f, 1.0f);
    });
    const auto t3 = std::chrono::steady_clock::now();

    const int midiLo = mLo + runLo;
    double worstCents = 0.0, worstDrift = 0.0;
    int flips = 0;
    printf("// fitted by `mmtool whistle fit`: %d poses screened, %d rendered, %d lock; N = %g, breath %.2f, %.2f s.\n",
           (int)grid.size(), (int)cands.size(), (int)locks.size(), (double)a.size, breath, sec);
    printf("// a lock: periodicity above %.2f and whistle above %.1f after 300 ms. every lock also rendered at breath\n"
           "// 0.5 and 1.0 and at N = %d and %d; the cost trades cents, off-mode, held locks and size error against pose jumps.\n"
           "// drift: whistleHz from breath 0.5 to 1.0.\n", kLockPer, kLockWhistle, kFitSizeLo, kFitSizeHi);
    printf("const int kWhistleMidiLo = %d;\n", midiLo);
    printf("const WhistlePose kWhistleTable[] = {\n");
    printf("    // tongueX tongueD  lipD   slotPlace slotDegree       midi   measured (target)   cents   drift\n");
    for (int k = 0; k < runLen; ++k) {
        const WhistleCand& c = *pick[(size_t)k];
        const int m = midiLo + k;
        const double cents = centsOff(c.hz, m);
        char drift[48];
        if (soft[(size_t)k].locked && hard[(size_t)k].locked) {
            const double dc = 1200.0 * std::log2(hard[(size_t)k].hz / soft[(size_t)k].hz);
            snprintf(drift, sizeof drift, "%+4.0f c", dc);
            worstDrift = std::max(worstDrift, std::fabs(dc));
        } else {
            snprintf(drift, sizeof drift, "no lock at %s", soft[(size_t)k].locked ? "1.0" : (hard[(size_t)k].locked ? "0.5" : "0.5, 1.0"));
        }
        worstCents = std::max(worstCents, std::fabs(cents));
        if (k > 0 && (pick[(size_t)k - 1]->p.slotPlace >= 0.0f) != (c.p.slotPlace >= 0.0f)) ++flips;
        char slot[32];
        if (c.p.slotPlace >= 0.0f) snprintf(slot, sizeof slot, "%.3ff, %.3ff", c.p.slotPlace, c.p.slotDegree);
        else snprintf(slot, sizeof slot, "-1.0f,  0.000f");
        printf("    {%.3ff, %.3ff, %.3ff, %s},   // %3d   %6.1f (%6.1f)   %+4.0f   %s\n", c.p.tongueX, c.p.tongueD, c.p.lipD,
               slot, m, c.hz, midiToHz(m), cents, drift);
    }
    printf("};\n");
    printf("const int kWhistleCount = (int)(sizeof(kWhistleTable) / sizeof(kWhistleTable[0]));\n");
    fprintf(stderr, "midi %d .. %d: %d rows, worst %.0f cents, worst drift %.0f cents, %d slot flips, %.1f s in all\n",
            midiLo, midiLo + runLen - 1, runLen, worstCents, worstDrift, flips,
            std::chrono::duration<double>(t3 - t0).count());
    return 0;
}

int cmdWhistle(const Args& a) {
    if (!a.pos.empty() && a.pos[0] == "fit") return cmdWhistleFit(a);
    const float sec = a.secGiven ? a.sec : 1.5f;           // the whistle's own default length
    Voice v(a.size, a.seed);
    setup(v, a);
    if (!a.pos.empty()) {                                  // a note, through the table
        const float midi = (float)atof(a.pos[0].c_str());
        v.whistle(midi, a.breath);
        float lo, hi;
        v.whistleRange(lo, hi);
        const double hz = midiToHz(midi);
        printf("whistle %.2f (%.1f Hz)  breath %.2f  N = %g  table range %.2f .. %.2f\n", midi, hz, a.breath, (double)a.size, lo, hi);
        std::vector<float> x;
        const double mean = renderWhistle(v, sec, x);
        const BodyState st = v.state();
        printf("  note      whistleHz %.1f against %.1f Hz: %+.0f cents (%+.0f cents from the nearest octave)\n",
               st.whistleHz, hz, st.whistleHz > 0.0f ? centsOff(st.whistleHz, midi) : 0.0,
               st.whistleHz > 0.0f ? std::remainder(centsOff(st.whistleHz, midi), 1200.0) : 0.0);
        if (whistleReport(v, x, mean)) return 1;
        return finish(x, a);
    }
    if (a.tonguePos < 0.0f || a.lips < 0.0f) { fprintf(stderr, "usage: mmtool whistle NOTE | fit | --tongue POS HEIGHT --lips L ...\n"); return 1; }
    v.setGlottis(1.0f);
    v.setTongue(a.tonguePos, a.tongueHeight);
    v.setLips(a.lips);
    if (a.slotPlace >= 0.0f) v.setConstriction(0, a.slotPlace, a.slotDegree);
    v.setVelum(0.0f);
    v.setBreath(a.breath);
    char slot[32] = "-";
    if (a.slotPlace >= 0.0f) snprintf(slot, sizeof slot, "%.3f %.3f", a.slotPlace, a.slotDegree);
    printf("tongue %.3f %.3f  lips %.3f  slot %s  breath %.2f  N = %g\n", a.tonguePos, a.tongueHeight, a.lips, slot,
           a.breath, (double)a.size);
    std::vector<float> x;
    const double mean = renderWhistle(v, sec, x);
    if (whistleReport(v, x, mean)) return 1;
    return finish(x, a);
}

void usage() {
    fprintf(stderr,
        "mmtool vowel AA|rest [--midi 48] [--sec 2] -o aa.wav\n"
        "mmtool pose --tongue POS HEIGHT --lips L [--velum V] -o pose.wav\n"
        "mmtool formants AA | --tongue POS HEIGHT --lips L\n"
        "mmtool fit\n"
        "mmtool syllable \"B AA\" -o ba.wav\n"
        "mmtool sing song.txt -o song.wav\n"
        "mmtool say \"HH AH0 L OW1 / W ER1 L D\" -o hello.wav\n"
        "mmtool speak \"S T AA1 P / DH AE2 T\" [--midi M] [--range R] [--speed S] [--breath B] -o stop.wav\n"
        "                                              spoken: stress sets length, accent and loudness\n"
        "mmtool corpus <corpusDir> <outDir>\n"
        "mmtool trace \"AA B AA\" [-o trace.wav]        10 ms frames: level, periodicity, centroid, body\n"
        "             [--speak --range R --speed S]    the line through speak() instead of say()\n"
        "mmtool throat [step]                          sweep the throat: where the vowel gates hold\n"
        "mmtool whistle --tongue POS HEIGHT --lips L [--slot PLACE DEGREE] [--breath B] [--sec 1.5] -o w.wav\n"
        "                                              a live whistle pose: lock, level, pitch against the lip modes\n"
        "mmtool whistle NOTE [--breath B] [--sec 1.5] [--size N] -o w.wav\n"
        "                                              a note through whistle(midi, breath) and the table: the same report\n"
        "mmtool whistle fit [--size N] [--breath 0.8] [--sec 0.5] [--tune ...]\n"
        "                                              grid-search the whistle table (N = 22 is what whistle() assumes)\n"
        "options: --size N  --seed S  --midi M  --sec T  --breath B  --tense T  --no-halfband  --no-wobble\n"
        "         --tune NAME=VALUE   any field of mm::Tuning, repeatable\n"
        "         --png FILE   also write a spectrogram of the render\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    Args a;
    if (!parseArgs(argc, argv, a)) { usage(); return 1; }
    const std::string cmd = argv[1];
    if (cmd == "vowel")    return cmdVowel(a);
    if (cmd == "pose")     return cmdPose(a);
    if (cmd == "formants") return cmdFormants(a);
    if (cmd == "fit")      return cmdFit(a);
    if (cmd == "syllable") return cmdSyllable(a);
    if (cmd == "sing")     return cmdSing(a);
    if (cmd == "say")      return cmdSay(a);
    if (cmd == "speak")    return cmdSpeak(a);
    if (cmd == "corpus")   return cmdCorpus(a);
    if (cmd == "trace")    return cmdTrace(a);
    if (cmd == "throat")   return cmdThroat(a);
    if (cmd == "whistle")  return cmdWhistle(a);
    usage();
    return 1;
}
