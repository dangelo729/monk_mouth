// monk_mouth — mm_score.cpp
#include "mm_score.h"

#include <cstring>

namespace mm {
namespace {

enum : uint8_t { kSyl, kRest, kMelisma };

struct PSyl {                       // one parsed syllable
    uint8_t onset[kMaxCluster], coda[kMaxCluster];
    uint8_t nOnset = 0, nCoda = 0, vowel = ph::none, kind = kSyl;
    uint8_t stress = 1;             // the vowel's digit: 1 (or none) primary, 2 secondary, 0 unstressed
    float   midi = 0.0f;
    int32_t dur = 0;                // samples; 0 = auto (say, syllable)
};

struct Timing { int32_t set, mid, rel; };   // slot set, affricate's second target, clear
struct Span   { int32_t open, close; };
constexpr int kMaxSpans = 96;

struct Sink {
    Event* out; int max; int n; bool overflow;
    void put(int32_t tick, uint8_t ch, float a, float b, float rate, uint8_t phone) {
        if (n >= max) { overflow = true; return; }
        Event& e = out[n++];
        e.tick = tick > 0 ? (uint32_t)tick : 0u;
        e.a = a; e.b = b; e.rate = rate; e.channel = ch; e.phone = phone;
    }
};

// ---- parsing ---------------------------------------------------------------------------------

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool isBlank(const char* s) {
    if (!s) return true;
    for (; *s; ++s) if (!isSpace(*s)) return false;
    return true;
}

// one phones string -> syllables. between vowels, consonants join the next
// syllable except at a "/" word end. returns how many were added, or -1.
int split(const char* s, float midi, PSyl* out, int max) {
    int n = 0, cur = -1, nPend = 0, boundary = -1;
    uint8_t pend[2 * kMaxCluster];

    auto toCoda = [&](int count) -> bool {               // pend[0..count) -> coda of cur
        if (count > kMaxCluster) return false;
        for (int i = 0; i < count; ++i) out[cur].coda[i] = pend[i];
        out[cur].nCoda = (uint8_t)count;
        return true;
    };
    auto newSyl = [&](int from, uint8_t vowel) -> bool {  // pend[from..) -> onset of a new syllable
        if (n >= max || nPend - from > kMaxCluster) return false;
        PSyl& p = out[n];
        p = PSyl();
        p.midi = midi;
        p.vowel = vowel;
        for (int i = from; i < nPend; ++i) p.onset[p.nOnset++] = pend[i];
        ++n;
        return true;
    };

    while (*s) {
        while (isSpace(*s)) ++s;
        if (!*s) break;
        const char* tok = s;
        while (*s && !isSpace(*s)) ++s;
        const int len = (int)(s - tok);

        if (len == 1 && tok[0] == '/') {                  // word end
            if (cur >= 0) boundary = nPend;
            continue;
        }
        if (len == 1 && tok[0] == '_') {                  // rest
            if (cur >= 0) { if (!toCoda(nPend)) return -1; }
            else if (nPend && !newSyl(0, ph::none)) return -1;
            if (n >= max) return -1;
            out[n] = PSyl();
            out[n].kind = kRest;
            out[n].midi = midi;
            ++n;
            cur = -1; nPend = 0; boundary = -1;
            continue;
        }
        const uint8_t p = parsePhone(tok, len);
        if (p == ph::none) return -1;
        if (isVowel(p)) {
            int from = 0;
            if (cur >= 0) {
                from = boundary >= 0 ? boundary : 0;
                if (!toCoda(from)) return -1;
            }
            if (!newSyl(from, p)) return -1;
            const char d = tok[len - 1];
            if (d == '0') out[n - 1].stress = 0;
            else if (d == '2') out[n - 1].stress = 2;
            cur = n - 1; nPend = 0; boundary = -1;
        } else {
            if (nPend >= 2 * kMaxCluster) return -1;
            pend[nPend++] = p;
        }
    }
    if (cur >= 0) { if (!toCoda(nPend)) return -1; }
    else if (nPend && !newSyl(0, ph::none)) return -1;
    return n;
}

// ---- consonant timing ------------------------------------------------------------------------

// how long a consonant takes to get there, from its close rate: the time for
// an exponential approach to cover what matters of the distance. diction
// scales the rate, so a crisper gesture leads by less and still lands on time.
float leadMs(const Consonant& c, float diction) {
    switch (c.kind) {
        case kStop: case kNasal: case kAffricate: return 1800.0f / (c.closeRate * diction);   // 1.5 -> 0 aiming at -0.3
        case kFricative:   return 2600.0f / (c.closeRate * diction);                          // into the hiss window
        case kApproximant: return 2000.0f / (c.closeRate * diction);
        case kGlide:       return kGlideLeadMs;
        default:           return kAspirateLeadMs;
    }
}
float holdMs(const Consonant& c) {
    return (float)c.holdMs + (c.kind == kAffricate ? kAffricateFricMs : 0.0f);
}
float clusterLeadMs(const uint8_t* c, int m, float diction) { return m ? leadMs(consonant(c[0]), diction) : 0.0f; }
float clusterHoldMs(const uint8_t* c, int m) {
    float h = 0.0f;
    for (int i = 0; i < m; ++i) h += holdMs(consonant(c[i]));
    return h;
}

// how long a breath at `level` takes to fall away at kScoreBreathFall: it aims
// at kBreathAim, below zero, so it reaches zero in finite time. one block more
// for the player's rounding; a level past 1000 waits as long as 1000.
int32_t fallTicks(float level, float fs) {
    if (!(level > 0.0f)) return 0;
    if (!(level < 1000.0f)) level = 1000.0f;
    return (int32_t)(std::log(1.0f - level / kBreathAim) / kScoreBreathFall * fs) + kBlock;
}

// lay a cluster out backward from `end`, the release of its last consonant.
// each consonant releases when the next has arrived: they overlap, which is
// why they alternate slots. returns the cluster's start.
int32_t layout(const uint8_t* c, int m, float scale, int32_t end, float spms, float diction, Timing* t) {
    int32_t rel = end;
    for (int k = m - 1; k >= 0; --k) {
        const Consonant& cn = consonant(c[k]);
        const int32_t fric = cn.kind == kAffricate ? (int32_t)(kAffricateFricMs * scale * spms) : 0;
        const int32_t hold = (int32_t)((float)cn.holdMs * scale * spms);
        t[k].rel = rel;
        t[k].mid = rel - fric;
        const int32_t arrived = rel - fric - hold;
        t[k].set = arrived - (int32_t)(leadMs(cn, diction) * spms);
        rel = arrived;
    }
    return m ? t[0].set : end;
}

struct Emitter {
    Emitter(Sink& s, float samplesPerMs, float dictionScale) : sink(s), spms(samplesPerMs), diction(dictionScale) {}

    Sink& sink;
    float spms, diction;
    int   slotTurn = 0;
    Span  voiceless[kMaxSpans]; int nVoiceless = 0;
    Span  nasal[kMaxSpans];     int nNasal = 0;
    bool  tooMany = false;

    // restoreVowel: restoreLip is a vowel's lips (kLipsVowel), not the rest's.
    // openAt, shutAt >= 0: a voiceless consonant's glottis opens or shuts
    // there instead (out of silence; into silence).
    void consonant(uint8_t phone, Timing t, int32_t floorTick,
                   float restoreX, float restoreD, float restoreLip, bool restoreVowel,
                   int32_t openAt = -1, int32_t shutAt = -1) {
        const float restoreTag = restoreVowel ? kLipsVowel : 0.0f;
        const Consonant& c = mm::consonant(phone);
        const int32_t set = t.set > floorTick ? t.set : floorTick;
        const int32_t mid = t.mid > set ? t.mid : set;
        const int32_t rel = t.rel > mid ? t.rel : mid;

        switch (c.kind) {
            case kGlide: {
                const VowelPose g = glidePose(phone);
                sink.put(set, kChTongue, g.tongueX, g.tongueD, kGlideMoveRate, phone);
                sink.put(set, kChLips, g.lipD, 0.0f, kGlideMoveRate, 0);
                sink.put(rel, kChTongue, restoreX, restoreD, kGlideMoveRate, 0);
                sink.put(rel, kChLips, restoreLip, restoreTag, kGlideMoveRate, 0);
                break;
            }
            case kAspirate:
                sink.put(set, kChPhone, 0.0f, 0.0f, 0.0f, phone);
                break;
            default: {
                const uint8_t ch = (uint8_t)(kChSlot0 + (slotTurn++ & 1));   // alternate: clusters overlap
                sink.put(set, ch, c.place, c.degree, c.closeRate * diction, phone);
                if (c.kind == kAffricate)                                     // same slot, two targets in a row
                    sink.put(mid, ch, c.place, 0.15f, c.releaseRate * diction, 0);
                sink.put(rel, ch, kSlotClear, 0.0f, c.releaseRate * diction, 0);
                if (c.lipD > 0.0f) {
                    sink.put(set, kChLips, c.lipD, 0.0f, 0.0f, 0);
                    sink.put(rel, kChLips, restoreLip, restoreTag, 0.0f, 0);
                }
                break;
            }
        }

        // voiceless: the glottis opens through the consonant — half way into
        // the approach, so voicing dies as the constriction arrives, not before.
        // for a stop it shuts kVotMs after the release: aspiration and
        // voice-onset time are that overlap, nothing more. out of silence it
        // is open before the air comes; into silence it stays open until the
        // breath has gone, so no voice comes back after the release.
        if (c.voiceless) {
            int32_t open = c.kind == kAspirate ? set : set + (int32_t)(0.5f * leadMs(c, diction) * spms);
            int32_t close = rel + (c.kind == kStop ? (int32_t)(kVotMs * spms) : 0);
            if (openAt >= 0 && openAt < open) open = openAt;
            if (shutAt > close) close = shutAt;
            if (nVoiceless < kMaxSpans) voiceless[nVoiceless++] = Span{open, close}; else tooMany = true;
        }
        if (c.kind == kNasal) {
            if (nNasal < kMaxSpans) nasal[nNasal++] = Span{set - (int32_t)(kVelumLeadMs * spms), rel};
            else tooMany = true;
        }
    }

    // spans that touch are one gesture: no shutting the glottis between S and T
    void flush(const Span* spans, int n, uint8_t ch, float rateOpen) {
        const int32_t gap = (int32_t)(20.0f * spms);
        for (int i = 0; i < n;) {
            const int32_t open = spans[i].open;
            int32_t close = spans[i].close;
            int j = i + 1;
            while (j < n && spans[j].open <= close + gap) {
                if (spans[j].close > close) close = spans[j].close;
                ++j;
            }
            sink.put(open, ch, 1.0f, 0.0f, rateOpen, 0);
            sink.put(close, ch, 0.0f, 0.0f, 0.0f, 0);
            i = j;
        }
    }
};

// ---- the compiler ----------------------------------------------------------------------------

// sp: speak(), resolved (a baseline, a speed in range); null for sing, say
// and syllable. speech sets each syllable's length, accent and loudness from
// its stress, and runs every consonant's hold and gesture at its speed.
int compile(PSyl* syl, int n, const ScoreOptions& opt, const Speech* sp, Event* out, int maxEvents, ScoreInfo* info) {
    if (n <= 0 || n > kMaxSyllables) return -1;
    const float spms = opt.fs * 0.001f;
    const float speed = sp ? sp->speed : 1.0f;
    const float tempo = 1.0f / speed;                 // every hold, and a spoken vowel, at the speed
    const float diction = (opt.diction > 0.0f ? opt.diction : 1.0f) * speed;
    Sink sink{out, maxEvents, 0, false};
    Emitter em(sink, spms, diction);

    int     nextReal[kMaxSyllables];        // the next non-melisma syllable, or n
    int32_t group[kMaxSyllables];           // a syllable's own time plus its melismas
    float   scaleOn[kMaxSyllables], scaleCoda[kMaxSyllables];
    int32_t beat[kMaxSyllables], onsetStart[kMaxSyllables];
    int32_t vowelEnd[kMaxSyllables];        // where each vowel gives way to its coda or the next onset
    int32_t pitchFrom[kMaxSyllables];       // speech: no accent rises before this (a block past the note set out of silence)

    int lastVowel = -1;                     // the line's last syllable with a vowel
    for (int k = 0; k < n; ++k) {
        int j = k + 1;
        while (j < n && syl[j].kind == kMelisma) ++j;
        nextReal[k] = j;
        scaleOn[k] = scaleCoda[k] = tempo;
        vowelEnd[k] = pitchFrom[k] = 0;
        if (syl[k].kind == kSyl && syl[k].vowel) lastVowel = k;
    }
    // speech: a syllable's breath, from its stress
    auto level = [&](const PSyl& s) {
        if (!sp) return opt.breath;
        if (!s.vowel || s.stress == 0) return opt.breath * kSpeakUnstressedBreath;
        return s.stress == 2 ? opt.breath * kSpeakSecondaryBreath : opt.breath;
    };

    // ---- pass 1: durations, and how hard each boundary squeezes its holds ---------------------
    for (int k = 0; k < n; ++k) {
        PSyl& s = syl[k];
        if (s.kind == kMelisma) continue;
        const int j = nextReal[k];
        const bool nextSyl = j < n && syl[j].kind == kSyl;
        const float fixed = (s.nCoda ? clusterLeadMs(s.coda, s.nCoda, diction) + kCodaTailMs * tempo : 0.0f)
                          + (nextSyl ? clusterLeadMs(syl[j].onset, syl[j].nOnset, diction) : 0.0f);
        const float holds = (clusterHoldMs(s.coda, s.nCoda)
                          + (nextSyl ? clusterHoldMs(syl[j].onset, syl[j].nOnset) : 0.0f)) * tempo;
        float minVowel = s.vowel ? kMinVowelMs : 0.0f;
        if (s.dur <= 0) {                                 // auto: room for everything
            float base = 0.0f;
            if (s.kind == kRest) base = sp ? kSpeakPauseMs * tempo : kSayRestMs;
            else if (s.vowel && sp) {
                base = s.stress == 0 ? kSpeakUnstressedMs : (s.stress == 2 ? kSpeakSecondaryMs : kSpeakPrimaryMs);
                if (vowel(s.vowel).isDiphthong()) base *= kSpeakDiphthong;
                if (k == lastVowel) base *= kSpeakFinal;
                base *= tempo;
            }
            else if (s.vowel) base = vowel(s.vowel).isDiphthong() ? kSayDiphthongMs : kSayVowelMs;
            s.dur = (int32_t)((base + fixed + holds) * spms);
            if (minVowel > base) minVowel = base;         // a short spoken vowel squeezes nothing
        }
        int32_t g = s.dur;
        for (int m = k + 1; m < j; ++m) g += syl[m].dur;
        group[k] = g;
        const float avail = (float)g / spms - minVowel;
        if (holds > 0.0f && fixed + holds > avail) {      // holds compress toward a floor
            const float sc = clampf((avail - fixed) / holds, kHoldFloor, 1.0f) * tempo;
            scaleCoda[k] = sc;
            if (nextSyl) scaleOn[j] = sc;
        }
    }

    // ---- beats. the first onset needs room before the first beat -------------------------------
    int first = 0;
    while (first < n && syl[first].kind == kMelisma) ++first;
    int32_t leadIn = opt.breathEvents ? (int32_t)(kSilentLeadMs * spms) : 0;
    if (first < n && syl[first].kind == kSyl) {
        const PSyl& s = syl[first];
        leadIn += s.nOnset ? (int32_t)((clusterLeadMs(s.onset, s.nOnset, diction) + clusterHoldMs(s.onset, s.nOnset) * tempo) * spms)
                           : (opt.breathEvents ? (int32_t)(kVowelLeadMs * spms) : 0);
    }
    beat[0] = leadIn;
    for (int k = 1; k < n; ++k) beat[k] = beat[k - 1] + syl[k - 1].dur;
    const int32_t endTick = beat[n - 1] + syl[n - 1].dur;

    Timing t[kMaxCluster];
    for (int k = 0; k < n; ++k)
        onsetStart[k] = syl[k].kind == kSyl ? layout(syl[k].onset, syl[k].nOnset, scaleOn[k], beat[k], spms, diction, t) : beat[k];

    // speech: the baseline declines across the line, from its first vowel to the end
    int firstVowel = 0;
    while (firstVowel < n && (syl[firstVowel].kind != kSyl || !syl[firstVowel].vowel)) ++firstVowel;
    const int32_t lineFrom = firstVowel < n ? beat[firstVowel] : 0;
    const float lineSpan = endTick > lineFrom ? (float)(endTick - lineFrom) : 1.0f;
    auto baseline = [&](int32_t tick) {
        return sp->midi - kSpeakDeclination * clampf((float)(tick - lineFrom) / lineSpan, 0.0f, 1.0f);
    };

    // ---- pass 2: emit, in the order things happen ------------------------------------------------
    bool  fromSilence = true;
    float lip = kLipDMax, poseX = 0.0f, poseD = 0.0f;
    bool  havePose = false;
    int32_t prevBeat = 0;
    float breathNow = 0.0f;                                // speech: the level last asked for

    for (int k = 0; k < n; ++k) {
        const PSyl& s = syl[k];
        if (s.kind == kMelisma) {                          // a melisma changes only pitch
            if (opt.pitchEvents) sink.put(beat[k], kChPitch, s.midi, 0.0f, 0.0f, 0);
            continue;
        }
        if (s.kind == kRest) {
            if (opt.breathEvents) sink.put(beat[k], kChBreath, 0.0f, 0.0f, kScoreBreathFall, ph::clear);
            breathNow = 0.0f;
            fromSilence = true;
            prevBeat = beat[k];
            continue;
        }

        const int j = nextReal[k];
        const bool nextSyl = j < n && syl[j].kind == kSyl;
        const int32_t groupEnd = beat[k] + group[k];
        const VowelPose* v = s.vowel ? &vowel(s.vowel) : nullptr;

        // the vowel pose is sent when the onset starts, so the tongue is already
        // in place behind the closure: coarticulation for free.
        int32_t start = s.nOnset ? onsetStart[k] : beat[k] - (int32_t)(kVowelLeadMs * spms);
        if (start < prevBeat) start = prevBeat;
        // out of silence the mouth is set before any air: the pose moves at
        // kSilentPoseRate, an onsetless vowel's kSilentLeadMs ahead of its
        // breath, and the breath comes once the first consonant has arrived —
        // the closure shut, the channel narrowed — with a voiceless one's
        // glottis open from kSilentLeadMs before it starts to move.
        const bool early = fromSilence && opt.breathEvents;
        fromSilence = false;
        int32_t poseAt = start;
        if (early && !s.nOnset) {
            poseAt = start - (int32_t)(kSilentLeadMs * spms);
            if (poseAt < prevBeat) poseAt = prevBeat;
        }
        if (v) {
            const float rate = early ? kSilentPoseRate : 0.0f;
            sink.put(poseAt, kChTongue, v->tongueX, v->tongueD, rate, 0);
            sink.put(poseAt, kChLips, v->lipD, kLipsVowel, rate, 0);
            lip = v->lipD; poseX = v->tongueX; poseD = v->tongueD; havePose = true;
        }

        // into silence (a rest or the end) the last voiceless consonant keeps
        // the glottis open until the breath has fallen, and no later than the
        // next syllable's mouth starts to move
        int32_t shutAt = -1;
        if (!nextSyl && opt.breathEvents) {
            shutAt = (j < n ? beat[j] : endTick) + fallTicks(opt.breath, opt.fs);
            const int after = j < n ? nextReal[j] : n;
            if (after < n && syl[after].kind == kSyl) {
                int32_t next = syl[after].nOnset ? onsetStart[after]
                                                 : beat[after] - (int32_t)((kVowelLeadMs + kSilentLeadMs) * spms);
                if (next < beat[j]) next = beat[j];
                if (shutAt > next) shutAt = next;
            }
        }

        // onset: scheduled backward, the last consonant releases on the beat
        layout(s.onset, s.nOnset, scaleOn[k], beat[k], spms, diction, t);
        int32_t on = start;
        int32_t openAt = -1;
        if (early && s.nOnset) {
            const int32_t set0 = t[0].set > prevBeat ? t[0].set : prevBeat;
            const Consonant& c0 = consonant(s.onset[0]);
            const int32_t rel0 = t[0].rel > set0 ? t[0].rel : set0;
            on = set0 + (int32_t)((leadMs(c0, diction) + (c0.degree <= 0.0f ? kSealMs : 0.0f)) * spms);
            if (on > rel0) on = rel0;
            openAt = set0 - (int32_t)(kSilentLeadMs * spms);
            if (openAt < prevBeat) openAt = prevBeat;
        }
        for (int i = 0; i < s.nOnset; ++i)
            em.consonant(s.onset[i], t[i], prevBeat, havePose ? poseX : vowel(ph::AH).tongueX,
                         havePose ? poseD : vowel(ph::AH).tongueD, lip, havePose,
                         i == 0 ? openAt : -1, i == s.nOnset - 1 && !v && !s.nCoda ? shutAt : -1);

        // pitch changes on the beat, and the same event restarts the vibrato
        // clock and names the vowel. out of silence the note is set with the
        // breath instead (the voice is not sounding; the onset is sung on the new note).
        // speech sets the note out of silence alone (the contour does the rest)
        // and moves the breath to each syllable's level as its mouth starts.
        const float lv = level(s);
        if (early) {
            sink.put(on, kChBreath, lv, 0.0f, 0.0f, 0);
            if (sp) {                                      // the note jumps in silence: a rise waits a block, to glide
                sink.put(on, kChPitch, baseline(beat[k]), -1.0f, kSpeakAccentFall, 0);
                pitchFrom[k] = on + kBlock;
            }
            else if (opt.pitchEvents) sink.put(on, kChPitch, s.midi, 0.0f, 0.0f, 0);
        } else if (sp && lv != breathNow) {
            sink.put(start, kChBreath, lv, 0.0f, kSpeakBreathRate, 0);
        }
        breathNow = lv;

        if (sp)                       { if (v) sink.put(beat[k], kChPhone, 0.0f, 0.0f, 0.0f, s.vowel); }
        else if (opt.pitchEvents && early) sink.put(beat[k], kChNoteOn, 0.0f, 0.0f, 0.0f, s.vowel);
        else if (opt.pitchEvents)     sink.put(beat[k], kChPitch, s.midi, 1.0f, 0.0f, s.vowel);
        else if (v)                   sink.put(beat[k], kChPhone, 0.0f, 0.0f, 0.0f, s.vowel);

        // coda: finishes inside the syllable's own time, ahead of the next onset
        int32_t codaEnd = groupEnd;
        if (nextSyl && onsetStart[j] < codaEnd) codaEnd = onsetStart[j];
        codaEnd -= (int32_t)(kCodaTailMs * tempo * spms);
        const int32_t codaStart = layout(s.coda, s.nCoda, scaleCoda[k], codaEnd, spms, diction, t);
        vowelEnd[k] = s.nCoda ? codaStart : (nextSyl && onsetStart[j] < groupEnd ? onsetStart[j] : groupEnd);
        if (vowelEnd[k] < beat[k]) vowelEnd[k] = beat[k];

        if (v && v->isDiphthong()) {                       // the second pose, part way through the vowel
            const int32_t at = beat[k] + (int32_t)(kDiphthongAt * (float)(vowelEnd[k] - beat[k]));
            sink.put(at, kChTongue, v->endX, v->endD, 0.0f, 0);
            sink.put(at, kChLips, v->endLip, kLipsVowel, 0.0f, 0);
            lip = v->endLip; poseX = v->endX; poseD = v->endD;
        }
        for (int i = 0; i < s.nCoda; ++i)
            em.consonant(s.coda[i], t[i], beat[k], poseX, poseD, lip, havePose, -1, i == s.nCoda - 1 ? shutAt : -1);

        prevBeat = beat[k];
    }

    // ---- speech: the pitch keeps moving ------------------------------------------------------------
    // a baseline that declines across the line; each primary syllable lifts
    // to baseline + range as its vowel starts and falls back through it; after
    // the last accent the pitch falls below the baseline by the end of the
    // last vowel. every move is a glide toward a target, never a step.
    if (sp && lastVowel >= 0) {
        int lastAccent = -1;
        for (int k = firstVowel; k <= lastVowel; ++k)
            if (syl[k].kind == kSyl && syl[k].vowel && syl[k].stress == 1) lastAccent = k;
        const float low = sp->midi - kSpeakDeclination - sp->range * kSpeakFinalDrop;
        const int32_t lead = (int32_t)(kSpeakAccentLeadMs * tempo * spms);
        auto fallStart = [&](int k) { return beat[k] + (int32_t)(kSpeakAccentAt * (float)(vowelEnd[k] - beat[k])); };
        const int32_t fall = lastAccent >= 0 ? fallStart(lastAccent) : beat[lastVowel];
        const float fallSec = (float)(vowelEnd[lastVowel] - fall) / opt.fs;
        const float fallRate = fallSec > 0.01f ? kSpeakFinalReach / fallSec : kSpeakFinalReach / 0.01f;
        for (int k = firstVowel; k <= lastVowel; ++k) {
            const PSyl& s = syl[k];
            if (s.kind != kSyl || !s.vowel) continue;
            int32_t at = beat[k] - lead;
            if (at < pitchFrom[k]) at = pitchFrom[k];
            if (s.stress == 1) {
                sink.put(at, kChPitch, baseline(beat[k]) + sp->range, -1.0f, kSpeakAccentRise, 0);
                const int32_t back = fallStart(k);
                if (k == lastAccent) sink.put(back, kChPitch, low, -1.0f, fallRate, 0);
                else                 sink.put(back, kChPitch, baseline(back), -1.0f, kSpeakAccentFall, 0);
            } else if (k < lastAccent) {
                sink.put(at, kChPitch, baseline(beat[k]), -1.0f, kSpeakAccentFall, 0);
            } else if (lastAccent < 0 && k == lastVowel) {
                sink.put(fall, kChPitch, low, -1.0f, fallRate, 0);
            } else if (lastAccent < 0) {
                sink.put(at, kChPitch, baseline(beat[k]), -1.0f, kSpeakAccentFall, 0);
            }
        }
    }

    em.flush(em.voiceless, em.nVoiceless, kChGlottis, 0.0f);
    em.flush(em.nasal, em.nNasal, kChVelum, kVelumRate);
    if (opt.breathEvents) sink.put(endTick, kChBreath, 0.0f, 0.0f, kScoreBreathFall, ph::clear);

    if (sink.overflow || em.tooMany) return -1;

    // time-sort, stable: events emitted first stay first within a tick
    for (int i = 1; i < sink.n; ++i) {
        const Event e = out[i];
        int m = i - 1;
        while (m >= 0 && out[m].tick > e.tick) { out[m + 1] = out[m]; --m; }
        out[m + 1] = e;
    }

    if (info) {
        info->events = sink.n;
        info->syllables = n;
        for (int k = 0; k < n; ++k) info->beat[k] = (uint32_t)beat[k];
        info->length = (uint32_t)endTick;
    }
    return sink.n;
}

}  // namespace

// ---- front ends --------------------------------------------------------------------------------

int compileSing(const Syllable* syls, int n, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    if (!syls || n <= 0) return -1;
    PSyl parsed[kMaxSyllables];
    int count = 0;
    for (int i = 0; i < n; ++i) {
        const int32_t dur = (int32_t)((float)syls[i].ms * opt.fs * 0.001f);
        if (isBlank(syls[i].phones)) {                    // "" = melisma
            if (count >= kMaxSyllables) return -1;
            parsed[count] = PSyl();
            parsed[count].kind = kMelisma;
            parsed[count].midi = syls[i].midi;
            parsed[count].dur = dur;
            ++count;
            continue;
        }
        const int added = split(syls[i].phones, syls[i].midi, parsed + count, kMaxSyllables - count);
        if (added <= 0) return -1;
        for (int k = 0; k < added; ++k) {                 // several vowels on one note share it
            parsed[count + k].dur = dur / added;
            if (parsed[count + k].dur < 1) parsed[count + k].dur = 1;
        }
        count += added;
    }
    return compile(parsed, count, opt, nullptr, out, maxEvents, info);
}

int compileSay(const char* phones, float midi, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    if (isBlank(phones)) return -1;
    PSyl parsed[kMaxSyllables];
    const int count = split(phones, midi, parsed, kMaxSyllables);
    if (count <= 0) return -1;
    return compile(parsed, count, opt, nullptr, out, maxEvents, info);
}

int compileSyllable(const char* phones, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    return compileSay(phones, 0.0f, opt, out, maxEvents, info);
}

int compileSpeak(const char* phones, const Speech& sp, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    if (isBlank(phones)) return -1;
    Speech held = sp;
    if (!(held.midi > 0.0f)) held.midi = kDefaultMidi;
    if (!std::isfinite(held.range)) held.range = Speech().range;
    held.speed = held.speed > 0.0f ? clampf(held.speed, kSpeakSpeedMin, kSpeakSpeedMax) : 1.0f;
    PSyl parsed[kMaxSyllables];
    const int count = split(phones, held.midi, parsed, kMaxSyllables);
    if (count <= 0) return -1;
    return compile(parsed, count, opt, &held, out, maxEvents, info);
}

// ---- the player ----------------------------------------------------------------------------------

void ScorePlayer::commit(const Event* events, int count, uint32_t seq) {
    if (count < 0) count = 0;
    if (count > kMaxEvents) count = kMaxEvents;
    pending_ = false;                                     // withdraw: nothing flips under the copy
    const int staging = 1 - active_;
    std::memcpy(buf_[staging], events, (size_t)count * sizeof(Event));
    count_[staging] = count;
    seq_[staging] = seq;
    pending_ = true;
    busy_ = true;                                         // last: the audio side cannot clear it now
}

bool ScorePlayer::beginBlock() {
    if (!pending_) return false;
    active_ = (uint8_t)(1 - active_);
    pending_ = false;
    cursor_ = 0;
    now_ = 0;
    return true;
}

const Event* ScorePlayer::next() {
    const int a = active_;
    if (cursor_ < count_[a] && buf_[a][cursor_].tick < now_ + (uint32_t)(kBlock / 2))
        return &buf_[a][cursor_++];
    return nullptr;
}

void ScorePlayer::endBlock() {
    now_ += (uint32_t)kBlock;
    if (!pending_ && cursor_ >= count_[active_]) busy_ = false;
}

void ScorePlayer::abandon() { cursor_ = count_[active_]; }

}  // namespace mm
