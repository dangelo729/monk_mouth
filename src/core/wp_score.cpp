// wind_pipe — wp_score.cpp  (plan §9)
#include "wp_score.h"

#include <cstring>

namespace wp {
namespace {

enum : uint8_t { kSyl, kRest, kMelisma };

struct PSyl {                       // one parsed syllable
    uint8_t onset[kMaxCluster], coda[kMaxCluster];
    uint8_t nOnset = 0, nCoda = 0, vowel = ph::none, kind = kSyl;
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

    // restoreVowel: restoreLip is a vowel's lips (kLipsVowel), not the rest's
    void consonant(uint8_t phone, Timing t, int32_t floorTick,
                   float restoreX, float restoreD, float restoreLip, bool restoreVowel) {
        const float restoreTag = restoreVowel ? kLipsVowel : 0.0f;
        const Consonant& c = wp::consonant(phone);
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
        // voice-onset time are that overlap, nothing more.
        if (c.voiceless) {
            const int32_t open = c.kind == kAspirate ? set : set + (int32_t)(0.5f * leadMs(c, diction) * spms);
            const int32_t close = rel + (c.kind == kStop ? (int32_t)(kVotMs * spms) : 0);
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

int compile(PSyl* syl, int n, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    if (n <= 0 || n > kMaxSyllables) return -1;
    const float spms = opt.fs * 0.001f;
    const float diction = opt.diction > 0.0f ? opt.diction : 1.0f;
    Sink sink{out, maxEvents, 0, false};
    Emitter em(sink, spms, diction);

    int     nextReal[kMaxSyllables];        // the next non-melisma syllable, or n
    int32_t group[kMaxSyllables];           // a syllable's own time plus its melismas
    float   scaleOn[kMaxSyllables], scaleCoda[kMaxSyllables];
    int32_t beat[kMaxSyllables], onsetStart[kMaxSyllables];

    for (int k = 0; k < n; ++k) {
        int j = k + 1;
        while (j < n && syl[j].kind == kMelisma) ++j;
        nextReal[k] = j;
        scaleOn[k] = scaleCoda[k] = 1.0f;
    }

    // ---- pass 1: durations, and how hard each boundary squeezes its holds ---------------------
    for (int k = 0; k < n; ++k) {
        PSyl& s = syl[k];
        if (s.kind == kMelisma) continue;
        const int j = nextReal[k];
        const bool nextSyl = j < n && syl[j].kind == kSyl;
        const float fixed = (s.nCoda ? clusterLeadMs(s.coda, s.nCoda, diction) + kCodaTailMs : 0.0f)
                          + (nextSyl ? clusterLeadMs(syl[j].onset, syl[j].nOnset, diction) : 0.0f);
        const float holds = clusterHoldMs(s.coda, s.nCoda)
                          + (nextSyl ? clusterHoldMs(syl[j].onset, syl[j].nOnset) : 0.0f);
        if (s.dur <= 0) {                                 // auto: room for everything
            float base = 0.0f;
            if (s.kind == kRest) base = kSayRestMs;
            else if (s.vowel) base = vowel(s.vowel).isDiphthong() ? kSayDiphthongMs : kSayVowelMs;
            s.dur = (int32_t)((base + fixed + holds) * spms);
        }
        int32_t g = s.dur;
        for (int m = k + 1; m < j; ++m) g += syl[m].dur;
        group[k] = g;
        const float avail = (float)g / spms - (s.vowel ? kMinVowelMs : 0.0f);
        if (holds > 0.0f && fixed + holds > avail) {      // holds compress toward a floor
            const float sc = clampf((avail - fixed) / holds, kHoldFloor, 1.0f);
            scaleCoda[k] = sc;
            if (nextSyl) scaleOn[j] = sc;
        }
    }

    // ---- beats. the first onset needs room before the first beat -------------------------------
    int first = 0;
    while (first < n && syl[first].kind == kMelisma) ++first;
    int32_t leadIn = opt.breathEvents ? (int32_t)(kBreathLeadMs * spms) : 0;
    if (first < n && syl[first].kind == kSyl) {
        const PSyl& s = syl[first];
        leadIn += s.nOnset ? (int32_t)((clusterLeadMs(s.onset, s.nOnset, diction) + clusterHoldMs(s.onset, s.nOnset)) * spms)
                           : (opt.breathEvents ? (int32_t)(kVowelLeadMs * spms) : 0);
    }
    beat[0] = leadIn;
    for (int k = 1; k < n; ++k) beat[k] = beat[k - 1] + syl[k - 1].dur;
    const int32_t endTick = beat[n - 1] + syl[n - 1].dur;

    Timing t[kMaxCluster];
    for (int k = 0; k < n; ++k)
        onsetStart[k] = syl[k].kind == kSyl ? layout(syl[k].onset, syl[k].nOnset, scaleOn[k], beat[k], spms, diction, t) : beat[k];

    // ---- pass 2: emit, in the order things happen ------------------------------------------------
    bool  fromSilence = true;
    float lip = kLipDMax, poseX = 0.0f, poseD = 0.0f;
    bool  havePose = false;
    int32_t prevBeat = 0;

    for (int k = 0; k < n; ++k) {
        const PSyl& s = syl[k];
        if (s.kind == kMelisma) {                          // a melisma changes only pitch
            if (opt.pitchEvents) sink.put(beat[k], kChPitch, s.midi, 0.0f, 0.0f, 0);
            continue;
        }
        if (s.kind == kRest) {
            if (opt.breathEvents) sink.put(beat[k], kChBreath, 0.0f, 0.0f, kScoreBreathFall, ph::clear);
            fromSilence = true;
            prevBeat = beat[k];
            continue;
        }

        const int j = nextReal[k];
        const bool nextSyl = j < n && syl[j].kind == kSyl;
        const int32_t groupEnd = beat[k] + group[k];

        // the vowel pose is sent when the onset starts, so the tongue is already
        // in place behind the closure: coarticulation for free.
        int32_t start = s.nOnset ? onsetStart[k] : beat[k] - (int32_t)(kVowelLeadMs * spms);
        if (start < prevBeat) start = prevBeat;
        // pitch changes on the beat, and the same event restarts the vibrato
        // clock and names the vowel. out of silence the note is set early
        // instead (the voice is not sounding; the onset is sung on the new note).
        const bool early = fromSilence && opt.breathEvents;
        if (early) {                                       // breath rises at the first onset
            const int32_t on = start - (int32_t)(kBreathLeadMs * spms);
            sink.put(on, kChBreath, opt.breath, 0.0f, 0.0f, 0);
            if (opt.pitchEvents) sink.put(on, kChPitch, s.midi, 0.0f, 0.0f, 0);
        }
        fromSilence = false;

        const VowelPose* v = s.vowel ? &vowel(s.vowel) : nullptr;
        if (v) {
            sink.put(start, kChTongue, v->tongueX, v->tongueD, 0.0f, 0);
            sink.put(start, kChLips, v->lipD, kLipsVowel, 0.0f, 0);
            lip = v->lipD; poseX = v->tongueX; poseD = v->tongueD; havePose = true;
        }

        // onset: scheduled backward, the last consonant releases on the beat
        layout(s.onset, s.nOnset, scaleOn[k], beat[k], spms, diction, t);
        for (int i = 0; i < s.nOnset; ++i)
            em.consonant(s.onset[i], t[i], prevBeat, havePose ? poseX : vowel(ph::AH).tongueX,
                         havePose ? poseD : vowel(ph::AH).tongueD, lip, havePose);

        if (opt.pitchEvents && early) sink.put(beat[k], kChNoteOn, 0.0f, 0.0f, 0.0f, s.vowel);
        else if (opt.pitchEvents)     sink.put(beat[k], kChPitch, s.midi, 1.0f, 0.0f, s.vowel);
        else if (v)                   sink.put(beat[k], kChPhone, 0.0f, 0.0f, 0.0f, s.vowel);

        // coda: finishes inside the syllable's own time, ahead of the next onset
        int32_t codaEnd = groupEnd;
        if (nextSyl && onsetStart[j] < codaEnd) codaEnd = onsetStart[j];
        codaEnd -= (int32_t)(kCodaTailMs * spms);
        const int32_t codaStart = layout(s.coda, s.nCoda, scaleCoda[k], codaEnd, spms, diction, t);

        if (v && v->isDiphthong()) {                       // the second pose, part way through the vowel
            int32_t vowelEnd = s.nCoda ? codaStart : (nextSyl && onsetStart[j] < groupEnd ? onsetStart[j] : groupEnd);
            if (vowelEnd < beat[k]) vowelEnd = beat[k];
            const int32_t at = beat[k] + (int32_t)(kDiphthongAt * (float)(vowelEnd - beat[k]));
            sink.put(at, kChTongue, v->endX, v->endD, 0.0f, 0);
            sink.put(at, kChLips, v->endLip, kLipsVowel, 0.0f, 0);
            lip = v->endLip; poseX = v->endX; poseD = v->endD;
        }
        for (int i = 0; i < s.nCoda; ++i)
            em.consonant(s.coda[i], t[i], beat[k], poseX, poseD, lip, havePose);

        prevBeat = beat[k];
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
    return compile(parsed, count, opt, out, maxEvents, info);
}

int compileSay(const char* phones, float midi, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    if (isBlank(phones)) return -1;
    PSyl parsed[kMaxSyllables];
    const int count = split(phones, midi, parsed, kMaxSyllables);
    if (count <= 0) return -1;
    return compile(parsed, count, opt, out, maxEvents, info);
}

int compileSyllable(const char* phones, const ScoreOptions& opt, Event* out, int maxEvents, ScoreInfo* info) {
    return compileSay(phones, 0.0f, opt, out, maxEvents, info);
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

}  // namespace wp
