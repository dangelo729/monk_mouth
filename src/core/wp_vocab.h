// wind_pipe — wp_vocab.h  (plan §8)
// the data: two small tables covering all 39 ARPABET phonemes. vowels are
// poses (tongue + lips, optionally a second pose for a diphthong); consonants
// are a kind, a place, a degree and their timing. everything here is tuned,
// first by `wptool fit`, then by ear.
#pragma once

#include "wp_config.h"

namespace wp {

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

}  // namespace wp
