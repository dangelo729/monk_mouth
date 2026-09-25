// wind_pipe — wptool.cpp  (plan §11)
// the one CLI. renders WAVs for the ear and numbers for the tables.
//
//   wptool vowel AA --midi 48 --sec 2 -o aa.wav         ("rest" = the reference's rest shape)
//   wptool pose --tongue .3 .8 --lips .9 -o pose.wav
//   wptool formants --tongue .3 .8 --lips .9            F1..F4 of a static shape (or: formants AA)
//   wptool fit                                          grid-search every vowel pose
//   wptool syllable "B AA" -o ba.wav
//   wptool sing song.txt -o song.wav                    one "phones midi ms" per line
//   wptool say "HH AH0 L OW1 / W ER1 L D" -o hello.wav
//   wptool corpus corpus build/corpus                   everything, for the ear
//   wptool trace "AA B AA"                              10 ms frames: what the body did, what came out
//   wptool throat [step]                                sweep the throat: where the vowel gates hold
//
// common options: --size N (sections, 16..26)  --seed S  --midi M  --sec T
//                 --breath B  --tense T  --no-halfband  --no-wobble
//                 --tune NAME=VALUE (any field of wp::Tuning; repeatable)
//                 --png FILE (a spectrogram of the render: 0-8 kHz, ticks every 100 ms)
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "core/wp_voice.h"
#include "wp_analysis.h"
#include "wp_png.h"
#include "wp_wav.h"

using namespace wp;

namespace {

struct Args {
    std::vector<std::string> pos;
    std::string out = "out.wav";
    std::string png;                                       // --png: a spectrogram of whatever was rendered
    int   size = kDefaultSections;
    uint32_t seed = 1;
    float midi = 48.0f, sec = 2.0f, breath = 0.8f, tense = -1.0f;
    float tonguePos = -1.0f, tongueHeight = -1.0f, lips = -1.0f, velum = -1.0f;
    bool  halfband = true, wobble = true;
    Tuning tuning;                                         // --tune: who is singing
    bool  tuned = false;
};

// --tune NAME=VALUE: every field of wp::Tuning, by name
struct TuneField { const char* name; float Tuning::* field; };
const TuneField kTuneFields[] = {
    {"glottisRest", &Tuning::glottisRest},             {"aspirationGain", &Tuning::aspirationGain},
    {"aspirationHz", &Tuning::aspirationHz},           {"aspirationQ", &Tuning::aspirationQ},
    {"noiseVoicedFloor", &Tuning::noiseVoicedFloor},   {"noiseVoicedPulse", &Tuning::noiseVoicedPulse},
    {"noiseUnvoiced", &Tuning::noiseUnvoiced},         {"glottalReflection", &Tuning::glottalReflection},
    {"lipReflection", &Tuning::lipReflection},         {"damp", &Tuning::damp},
    {"throatD", &Tuning::throatD},                     {"throatUpperD", &Tuning::throatUpperD},
    {"velumRest", &Tuning::velumRest},                 {"turbulenceGain", &Tuning::turbulenceGain},
    {"fricationHz", &Tuning::fricationHz},             {"fricationQ", &Tuning::fricationQ},
    {"voicedFrication", &Tuning::voicedFrication},     {"burstGain", &Tuning::burstGain},
    {"burstHalfLife", &Tuning::burstHalfLife},         {"diction", &Tuning::diction},
    {"rateBreathUp", &Tuning::rateBreathUp},           {"rateBreathDown", &Tuning::rateBreathDown},
    {"rateGlottis", &Tuning::rateGlottis},             {"rateTenseness", &Tuning::rateTenseness},
    {"rateTongue", &Tuning::rateTongue},               {"rateLips", &Tuning::rateLips},
    {"rateVelum", &Tuning::rateVelum},                 {"vibFadeRate", &Tuning::vibFadeRate},
    {"wobbleSlowHz", &Tuning::wobbleSlowHz},           {"wobbleFastHz", &Tuning::wobbleFastHz},
    {"wobbleSlowCents", &Tuning::wobbleSlowCents},     {"wobbleFastCents", &Tuning::wobbleFastCents},
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
        else if (s == "--size") { float v; if (!num(v)) return false; a.size = (int)v; }
        else if (s == "--seed") { float v; if (!num(v)) return false; a.seed = (uint32_t)v; }
        else if (s == "--midi") { if (!num(a.midi)) return false; }
        else if (s == "--sec") { if (!num(a.sec)) return false; }
        else if (s == "--breath") { if (!num(a.breath)) return false; }
        else if (s == "--tense") { if (!num(a.tense)) return false; }
        else if (s == "--tongue") { if (!num(a.tonguePos) || !num(a.tongueHeight)) return false; }
        else if (s == "--lips") { if (!num(a.lips)) return false; }
        else if (s == "--velum") { if (!num(a.velum)) return false; }
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

std::vector<double> poseFormants(int n, const Tuning& tuning, float tongueX, float tongueD, float lipD,
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
    const std::vector<double> f = poseFormants(a.size, tuning, x, dd, lip, velumD);
    printf("tongueX %.3f  tongueD %.3f  lipD %.3f  (N = %d)\n", x, dd, lip, a.size);
    for (size_t i = 0; i < f.size() && i < 4; ++i) printf("  F%d  %7.1f Hz\n", (int)i + 1, f[i]);
    Pose pose; pose.tongueX = x; pose.tongueD = dd; pose.lipD = lip;
    float d[kMaxSections];
    shapeTract(pose, a.size, d, nullptr, nullptr, tuning);
    printf("  d:");
    for (int i = 0; i < a.size; ++i) printf(" %.2f", d[i]);
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

    printf("// fitted by `wptool fit` over %d tubes, N = %d, orderings enforced with a %.0f %% margin.\n",
           (int)grid.size(), a.size, 100.0 * (margin - 1.0));
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
    if (step < 0.005f || step > 0.5f) { fprintf(stderr, "usage: wptool throat [step 0.005..0.5]\n"); return 1; }
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

// a frame-by-frame readout of a say(): what the body did and what came out.
// the numbers an ear would judge: level, periodicity, where the energy sits.
int cmdTrace(const Args& a) {
    if (a.pos.empty()) return 1;
    Voice v(a.size, a.seed);
    setup(v, a);
    v.setPitch(a.midi);
    if (!v.say(a.pos[0].c_str())) { fprintf(stderr, "bad phonemes: %s\n", a.pos[0].c_str()); return 1; }
    const int frame = 441;                                 // 10 ms
    const int lagLo = (int)(kDefaultFs / 400.0f), lagHi = (int)(kDefaultFs / 60.0f);
    std::vector<float> x, hist;
    printf("   ms  phone  level dB  period  centroid  breath voicing  hiss closed  press  slot0      slot1      lips velum\n");
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
        printf("%5d  %-5s  %8.1f  %6.2f  %8.0f  %6.2f %7.2f %5.2f %6d  %5.2f  %-9s  %-9s  %4.2f %5.2f\n",
               f * 10, phoneName(st.phoneme), level, per, cen, st.breath, st.voicing, st.hiss, (int)st.closed,
               st.pressure, s0, s1, st.lips, st.velum);
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
    if (a.pos.size() < 2) { fprintf(stderr, "usage: wptool corpus <corpusDir> <outDir>\n"); return 1; }
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

void usage() {
    fprintf(stderr,
        "wptool vowel AA|rest [--midi 48] [--sec 2] -o aa.wav\n"
        "wptool pose --tongue POS HEIGHT --lips L [--velum V] -o pose.wav\n"
        "wptool formants AA | --tongue POS HEIGHT --lips L\n"
        "wptool fit\n"
        "wptool syllable \"B AA\" -o ba.wav\n"
        "wptool sing song.txt -o song.wav\n"
        "wptool say \"HH AH0 L OW1 / W ER1 L D\" -o hello.wav\n"
        "wptool corpus <corpusDir> <outDir>\n"
        "wptool trace \"AA B AA\" [-o trace.wav]        10 ms frames: level, periodicity, centroid, body\n"
        "wptool throat [step]                          sweep the throat: where the vowel gates hold\n"
        "options: --size N  --seed S  --midi M  --sec T  --breath B  --tense T  --no-halfband  --no-wobble\n"
        "         --tune NAME=VALUE   any field of wp::Tuning, repeatable\n"
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
    if (cmd == "corpus")   return cmdCorpus(a);
    if (cmd == "trace")    return cmdTrace(a);
    if (cmd == "throat")   return cmdThroat(a);
    usage();
    return 1;
}
