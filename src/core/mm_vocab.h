// monk_mouth — mm_vocab.h  (plan §8; whistle plan §6)
// the data: two small tables covering all 39 ARPABET phonemes. vowels are
// poses (tongue + lips, optionally a second pose for a diphthong); consonants
// are a kind, a place, a degree and their timing. everything here is tuned,
// first by `mmtool fit`, then by ear. beside them, the whistle's vocabulary:
// one pose per semitone, fitted by `mmtool whistle fit`.
#pragma once

#include "mm_config.h"

namespace mm {

namespace ph {
enum : uint8_t {
    none = 0,
    // vowels
    AA, AE, AH, AO, AW, AY, EH, ER, EY, IH, IY, OW, OY, UH, UW,
    // consonants
    B, CH, D, DH, F, G, HH, JH, K, L, M, N, NG, P, R, S, SH, T, TH, V, W, Y, Z, ZH,
    count,
    firstVowel = AA, lastVowel = UW,
    firstConsonant = B, lastConsonant = ZH,
    clear = 255                                   // a score tag: "no phoneme now"
};
}  // namespace ph

struct VowelPose {
    float tongueX, tongueD, lipD;                 // raw anatomy units
    float endX, endD, endLip;                     // end = diphthong, 0 = none
    bool isDiphthong() const { return endX > 0.0f; }
};

enum ConsonantKind : uint8_t {
    kStop, kFricative, kAffricate, kNasal, kApproximant, kGlide, kAspirate
};

struct Consonant {
    uint8_t  kind;
    float    place, degree, lipD;                 // lipD 0 = leave the lips alone
    bool     voiceless;
    float    closeRate, releaseRate;
    uint16_t holdMs;
};

inline bool isVowel(uint8_t p)     { return p >= ph::firstVowel && p <= ph::lastVowel; }
inline bool isConsonant(uint8_t p) { return p >= ph::firstConsonant && p <= ph::lastConsonant; }

const VowelPose& vowel(uint8_t phone);            // phone must satisfy isVowel
const Consonant& consonant(uint8_t phone);        // phone must satisfy isConsonant
const char*      phoneName(uint8_t phone);        // "AA", "" for none

// one ARPABET token of len chars; case-insensitive, a trailing stress digit is
// accepted and ignored. ph::none if it is not a phoneme.
uint8_t parsePhone(const char* token, int len);

// the pose a glide borrows: W is UW with tight lips, Y is IY.
VowelPose glidePose(uint8_t phone);

// the fit's targets (Peterson & Barney 1952, adult male). f3 = 0: not constrained.
struct FormantTarget { uint8_t phone; float f1, f2, f3; bool rounded; };
extern const FormantTarget kFormantTargets[];
extern const int kFormantTargetCount;

// ---- the whistle (whistle plan §6) ---------------------------------------------------------
// a whistled note is a pose: folds open, velum shut, the tongue, a pucker and
// an optional slot behind it. raw anatomy units; slotPlace < 0: no slot.
struct WhistlePose { float tongueX, tongueD, lipD, slotPlace, slotDegree; };

// one row per semitone from kWhistleMidiLo, fitted at N = kDefaultSections
// and the default tuning: the midi here is the pitch a 22-section voice whistles.
extern const WhistlePose kWhistleTable[];
extern const int kWhistleMidiLo;
extern const int kWhistleCount;

// the pose for a table midi: the two neighbouring rows interpolated by the
// fraction of a semitone (a slot is interpolated when both rows have one, else
// the nearer row's), clamped at the ends.
WhistlePose whistlePose(float tableMidi);

}  // namespace mm
