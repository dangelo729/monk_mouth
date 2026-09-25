// wind_pipe — wp_vocab.cpp  (plan §8)
#include "wp_vocab.h"

namespace wp {

// ---- vowels -------------------------------------------------------------------------
// monophthong poses, in raw anatomy units. this block is `wptool fit`'s output,
// pasted: re-run the fit after any change to the anatomy or the tube.
// fitted by `wptool fit` over 7245 tubes, N = 22, orderings enforced with a 5 % margin.
// targets: Peterson & Barney 1952, adult male.   tongueX tongueD lipD       F1 (target)  F2 (target)   residual
static constexpr VowelPose kAA = {0.308f, 2.412f, 1.500f, 0, 0, 0};   //  713 ( 730)   1079 ( 1090)   -2.3%  -1.0%
static constexpr VowelPose kAE = {0.273f, 3.500f, 1.500f, 0, 0, 0};   //  639 ( 660)   1638 ( 1720)   -3.2%  -4.8%
static constexpr VowelPose kAH = {0.431f, 2.485f, 1.425f, 0, 0, 0};   //  630 ( 640)   1189 ( 1190)   -1.5%  -0.0%
static constexpr VowelPose kAO = {0.361f, 2.050f, 1.425f, 0, 0, 0};   //  571 ( 570)    876 (  840)   +0.2%  +4.3%
static constexpr VowelPose kEH = {0.642f, 3.355f, 1.425f, 0, 0, 0};   //  532 ( 530)   1838 ( 1840)   +0.3%  -0.1%
static constexpr VowelPose kER = {0.273f, 2.993f, 0.900f, 0, 0, 0};   //  497 ( 490)   1356 ( 1350)   +1.5%  +0.4%  (F3 in the cost)
static constexpr VowelPose kIH = {0.606f, 2.485f, 1.425f, 0, 0, 0};   //  387 ( 390)   1976 ( 1990)   -0.7%  -0.7%
static constexpr VowelPose kIY = {0.624f, 2.122f, 1.500f, 0, 0, 0};   //  266 ( 270)   2295 ( 2290)   -1.4%  +0.2%
static constexpr VowelPose kUH = {0.273f, 2.340f, 0.825f, 0, 0, 0};   //  444 ( 440)   1017 ( 1020)   +1.0%  -0.3%
static constexpr VowelPose kUW = {0.466f, 2.485f, 0.450f, 0, 0, 0};   //  307 ( 300)    833 (  870)   +2.5%  -4.3%

// diphthongs are two poses: EY EH->IH, OW AO->UH, AY AA->IH, AW AA->UH, OY AO->IH
static constexpr VowelPose glideTo(const VowelPose& a, const VowelPose& b) {
    return VowelPose{a.tongueX, a.tongueD, a.lipD, b.tongueX, b.tongueD, b.lipD};
}
static constexpr VowelPose kAW = glideTo(kAA, kUH);
static constexpr VowelPose kAY = glideTo(kAA, kIH);
static constexpr VowelPose kEY = glideTo(kEH, kIH);
static constexpr VowelPose kOW = glideTo(kAO, kUH);
static constexpr VowelPose kOY = glideTo(kAO, kIH);

const VowelPose& vowel(uint8_t phone) {
    switch (phone) {
        case ph::AA: return kAA;  case ph::AE: return kAE;  case ph::AH: return kAH;
        case ph::AO: return kAO;  case ph::AW: return kAW;  case ph::AY: return kAY;
        case ph::EH: return kEH;  case ph::ER: return kER;  case ph::EY: return kEY;
        case ph::IH: return kIH;  case ph::IY: return kIY;  case ph::OW: return kOW;
        case ph::OY: return kOY;  case ph::UH: return kUH;  case ph::UW: return kUW;
        default:     return kAH;
    }
}

VowelPose glidePose(uint8_t phone) {
    if (phone == ph::W) {
        VowelPose p = kUW;
        p.lipD = consonant(ph::W).lipD;
        return p;
    }
    return kIY;                                   // Y
}

// ---- consonants ----------------------------------------------------------------------
// places: labial .93, dental .88, L .86, alveolar .82, postalveolar .72,
// R .68, velar .50. degrees: stop -0.3 (closures aim below zero), fricative
// 0.15 (mid hiss window), TH/DH 0.30 (the window's weak edge), approximants
// above the window. nasal = stop + velum.
static const float kShut = kClosureAim;
static const Consonant kConsonants[] = {
    //            kind          place  degree lipD  voiceless close release holdMs
    /* B  */ {kStop,        0.93f, kShut, 0.0f, false, 70.0f, 45.0f, 60},
    /* CH */ {kAffricate,   0.72f, kShut, 0.9f, true,  70.0f, 45.0f, 50},
    /* D  */ {kStop,        0.82f, kShut, 0.0f, false, 70.0f, 45.0f, 50},
    /* DH */ {kFricative,   0.88f, 0.30f, 0.0f, false, 50.0f, 35.0f, 60},
    /* F  */ {kFricative,   0.93f, 0.15f, 0.0f, true,  50.0f, 35.0f, 100},
    /* G  */ {kStop,        0.50f, kShut, 0.0f, false, 70.0f, 35.0f, 60},
    /* HH */ {kAspirate,    0.00f, 0.00f, 0.0f, true,  0.0f,  0.0f,  70},
    /* JH */ {kAffricate,   0.72f, kShut, 0.9f, false, 70.0f, 45.0f, 50},
    /* K  */ {kStop,        0.50f, kShut, 0.0f, true,  70.0f, 35.0f, 70},
    /* L  */ {kApproximant, 0.86f, 0.80f, 0.0f, false, 40.0f, 30.0f, 70},
    /* M  */ {kNasal,       0.93f, kShut, 0.0f, false, 70.0f, 45.0f, 80},
    /* N  */ {kNasal,       0.82f, kShut, 0.0f, false, 70.0f, 45.0f, 80},
    /* NG */ {kNasal,       0.50f, kShut, 0.0f, false, 70.0f, 35.0f, 90},
    /* P  */ {kStop,        0.93f, kShut, 0.0f, true,  70.0f, 45.0f, 70},
    /* R  */ {kApproximant, 0.68f, 0.60f, 0.7f, false, 40.0f, 30.0f, 70},
    /* S  */ {kFricative,   0.82f, 0.15f, 0.0f, true,  50.0f, 35.0f, 110},
    /* SH */ {kFricative,   0.72f, 0.15f, 0.9f, true,  50.0f, 35.0f, 110},
    /* T  */ {kStop,        0.82f, kShut, 0.0f, true,  70.0f, 45.0f, 60},
    /* TH */ {kFricative,   0.88f, 0.30f, 0.0f, true,  50.0f, 35.0f, 90},
    /* V  */ {kFricative,   0.93f, 0.15f, 0.0f, false, 50.0f, 35.0f, 80},
    /* W  */ {kGlide,       0.00f, 0.00f, 0.45f, false, 0.0f, 0.0f,  60},
    /* Y  */ {kGlide,       0.00f, 0.00f, 0.0f, false, 0.0f,  0.0f,  60},
    /* Z  */ {kFricative,   0.82f, 0.15f, 0.0f, false, 50.0f, 35.0f, 90},
    /* ZH */ {kFricative,   0.72f, 0.15f, 0.9f, false, 50.0f, 35.0f, 90},
};

const Consonant& consonant(uint8_t phone) {
    if (!isConsonant(phone)) phone = ph::HH;
    return kConsonants[phone - ph::firstConsonant];
}

// ---- names ------------------------------------------------------------------------------
static const char* const kNames[ph::count] = {
    "",
    "AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH", "IY", "OW", "OY", "UH", "UW",
    "B", "CH", "D", "DH", "F", "G", "HH", "JH", "K", "L", "M", "N", "NG", "P", "R", "S", "SH",
    "T", "TH", "V", "W", "Y", "Z", "ZH",
};

const char* phoneName(uint8_t phone) { return phone < ph::count ? kNames[phone] : ""; }

uint8_t parsePhone(const char* token, int len) {
    if (len > 0 && token[len - 1] >= '0' && token[len - 1] <= '9') --len;   // stress digit
    if (len < 1 || len > 2) return ph::none;
    char up[3] = {0, 0, 0};
    for (int i = 0; i < len; ++i) {
        const char c = token[i];
        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    if (up[0] == 'A' && up[1] == 'X') return ph::AH;      // the schwa: cmudict writes it AH0
    for (uint8_t p = 1; p < ph::count; ++p) {
        const char* n = kNames[p];
        if (n[0] == up[0] && n[1] == up[1] && (up[1] == 0 || n[2] == 0)) return p;
    }
    return ph::none;
}

// ---- the fit's targets --------------------------------------------------------------------
const FormantTarget kFormantTargets[] = {
    {ph::IY, 270.0f, 2290.0f, 0.0f,    false},
    {ph::IH, 390.0f, 1990.0f, 0.0f,    false},
    {ph::EH, 530.0f, 1840.0f, 0.0f,    false},
    {ph::AE, 660.0f, 1720.0f, 0.0f,    false},
    {ph::AA, 730.0f, 1090.0f, 0.0f,    false},
    {ph::AO, 570.0f, 840.0f,  0.0f,    true},
    {ph::UH, 440.0f, 1020.0f, 0.0f,    true},
    {ph::UW, 300.0f, 870.0f,  0.0f,    true},
    {ph::AH, 640.0f, 1190.0f, 0.0f,    false},
    {ph::ER, 490.0f, 1350.0f, 1690.0f, false},
};
const int kFormantTargetCount = (int)(sizeof(kFormantTargets) / sizeof(kFormantTargets[0]));

}  // namespace wp
