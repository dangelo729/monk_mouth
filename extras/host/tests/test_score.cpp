// monk_mouth — tests/test_score.cpp
#include <cstring>
#include <string>
#include <vector>

#include "core/mm_voice.h"
#include "mm_analysis.h"
#include "mm_test.h"

using namespace mm;

namespace {

struct Compiled {
    Event ev[kMaxEvents];
    ScoreInfo info;
    int n = -1;
};

Compiled sing(const std::vector<Syllable>& syls) {
    Compiled c;
    c.n = compileSing(syls.data(), (int)syls.size(), ScoreOptions(), c.ev, kMaxEvents, &c.info);
    return c;
}

bool isSlot(const Event& e)  { return e.channel == kChSlot0 || e.channel == kChSlot1; }
bool isClear(const Event& e) { return isSlot(e) && e.a < 0.0f; }
bool isSet(const Event& e)   { return isSlot(e) && e.a >= 0.0f; }

int countChannel(const Compiled& c, uint8_t ch) {
    int k = 0;
    for (int i = 0; i < c.n; ++i) if (c.ev[i].channel == ch) ++k;
    return k;
}

std::vector<float> render(Voice& v, int samples) {
    std::vector<float> x((size_t)samples);
    v.render(x.data(), samples);
    return x;
}

Compiled sayEvents(const char* phones) {
    Compiled c;
    c.n = compileSay(phones, 50.0f, ScoreOptions(), c.ev, kMaxEvents, &c.info);
    return c;
}

// a say() block by block: each block's first tick, the readback after it, the block's level
struct Frame { uint32_t tick; BodyState st; double db; };
std::vector<Frame> sayFrames(const char* phones, int blocks = 600) {
    Voice v;
    v.setPitch(50.0f);
    std::vector<Frame> f;
    if (!v.say(phones)) return f;
    float buf[kBlock];
    for (int b = 0; b < blocks; ++b) {
        v.render(buf, kBlock);
        f.push_back(Frame{(uint32_t)(b * kBlock), v.state(), toDb(rms(buf, kBlock))});
    }
    return f;
}

uint32_t firstClear(const Compiled& c) {
    for (int i = 0; i < c.n; ++i) if (isClear(c.ev[i])) return c.ev[i].tick;
    return c.info.beat[0];                                           // HH: no slot, it ends on the beat
}

// where the first closure lets go: its slot's next event (an affricate's frication, or the clear)
uint32_t firstLetGo(const Compiled& c) {
    for (int i = 0; i < c.n; ++i) {
        if (!isSet(c.ev[i])) continue;
        for (int j = i + 1; j < c.n; ++j) if (c.ev[j].channel == c.ev[i].channel) return c.ev[j].tick;
    }
    return 0;
}

}  // namespace

// for "B AA" at beat T the slot's clear event is at T: the onset releases on the beat.
MM_TEST(score_onset_releases_on_the_beat) {
    const Compiled c = sing({{"B AA", 48, 500}});
    CHECK(c.n > 0);
    const uint32_t T = c.info.beat[0];
    int clears = 0;
    uint32_t setTick = 0, poseTick = 0;
    for (int i = 0; i < c.n; ++i) {
        if (isClear(c.ev[i])) { ++clears; CHECK(c.ev[i].tick == T); }
        if (isSet(c.ev[i])) { setTick = c.ev[i].tick; CHECK(c.ev[i].phone == ph::B && c.ev[i].b < 0.0f); }
        if (c.ev[i].channel == kChTongue) poseTick = c.ev[i].tick;
    }
    CHECK(clears == 1);
    CHECK(setTick < T);                                              // scheduled backward from the beat
    CHECK(poseTick == setTick);                                      // the vowel pose is sent when the onset starts
    CHECK(T > 0);

    // a cluster: every consonant before the beat, the last one released on it, slots alternating
    const Compiled s = sing({{"S T R IY", 48, 600}});
    const uint32_t Ts = s.info.beat[0];
    uint32_t lastClear = 0;
    uint8_t prevCh = 255;
    for (int i = 0; i < s.n; ++i) {
        if (isSet(s.ev[i])) { CHECK(s.ev[i].tick < Ts); CHECK(s.ev[i].channel != prevCh); prevCh = s.ev[i].channel; }
        if (isClear(s.ev[i])) { CHECK(s.ev[i].tick <= Ts); lastClear = s.ev[i].tick; }
    }
    CHECK(lastClear == Ts);
    CHECK(countChannel(s, kChGlottis) == 2);                         // S and T are one voiceless gesture: open once, shut once
}

// events come out time-sorted, and a score is self-contained: breath up at the start, down at the end.
MM_TEST(score_is_sorted_and_bracketed_by_breath) {
    const Compiled c = sing({{"HH AH", 55, 300}, {"L OW", 57, 500}, {"_", 0, 200}, {"W ER L D", 60, 700}});
    CHECK(c.n > 0);
    for (int i = 1; i < c.n; ++i) CHECK(c.ev[i - 1].tick <= c.ev[i].tick);
    int up = 0;                                                      // the first breath event: a rise, once the mouth is ready
    while (up < c.n && c.ev[up].channel != kChBreath) ++up;
    CHECK(up < c.n && c.ev[up].a > 0.0f && c.ev[up].tick > 0);
    CHECK(c.ev[0].channel == kChGlottis && c.ev[0].a > 0.0f && c.ev[0].tick == 0);   // HH: the glottis opens first
    CHECK(c.ev[c.n - 1].channel == kChBreath && c.ev[c.n - 1].a == 0.0f && c.ev[c.n - 1].tick == c.info.length);
    CHECK(countChannel(c, kChBreath) == 4);                          // up, down at the rest, up again, down at the end
    CHECK(c.info.syllables == 4);
    CHECK(c.info.beat[1] - c.info.beat[0] == (uint32_t)(0.300f * kDefaultFs));   // beats are the score's own clock
}

// codas end inside their syllable.
MM_TEST(score_codas_end_inside_their_syllable) {
    const Compiled c = sing({{"K AE T S", 50, 450}, {"S IH T", 52, 450}, {"AA N D", 53, 500}});
    CHECK(c.n > 0);
    // walk the slot events: every clear issued for a coda lands before the next beat
    for (int k = 0; k < 3; ++k) {
        const uint32_t from = c.info.beat[k], to = k < 2 ? c.info.beat[k + 1] : c.info.length;
        int sets = 0, clears = 0;
        for (int i = 0; i < c.n; ++i) {
            if (c.ev[i].tick <= from || c.ev[i].tick > to) continue;
            if (isSet(c.ev[i])) ++sets;
            if (isClear(c.ev[i])) ++clears;
        }
        CHECK_MSG(sets > 0 && clears >= sets - 1, "syllable %d: %d sets, %d clears inside its time", k, sets, clears);
    }
    uint32_t lastClear = 0;
    for (int i = 0; i < c.n; ++i) if (isClear(c.ev[i])) lastClear = c.ev[i].tick;
    CHECK(lastClear < c.info.length);                                // the final D is released before the breath falls
    int open = 0;                                                    // every set has its clear: nothing is left in the mouth
    for (int i = 0; i < c.n; ++i) { if (isSet(c.ev[i]) && c.ev[i].b < 0.2f && c.ev[i].phone) ++open; if (isClear(c.ev[i])) --open; }
    CHECK(open == 0);
}

// a melisma emits pitch events only.
MM_TEST(score_melisma_is_pitch_only) {
    const Compiled plain = sing({{"L AA", 60, 400}});
    const Compiled mel = sing({{"L AA", 60, 400}, {"", 62, 300}, {"", 64, 300}});
    CHECK(plain.n > 0 && mel.n == plain.n + 2);
    CHECK(countChannel(mel, kChPitch) == countChannel(plain, kChPitch) + 2);
    for (int i = 0; i < mel.n; ++i) {
        if (mel.ev[i].channel != kChPitch) continue;
        if (mel.ev[i].a == 62.0f) CHECK(mel.ev[i].tick == mel.info.beat[1]);   // pitch changes on the beat
        if (mel.ev[i].a == 64.0f) CHECK(mel.ev[i].tick == mel.info.beat[2]);
    }
    CHECK(mel.info.length == plain.info.length + (uint32_t)(0.6f * kDefaultFs));
    // and a coda waits for the end of the melisma
    const Compiled coda = sing({{"L AA T", 60, 400}, {"", 62, 600}});
    for (int i = 0; i < coda.n; ++i) if (isSet(coda.ev[i]) && coda.ev[i].phone == ph::T) CHECK(coda.ev[i].tick > coda.info.beat[1]);
}

// holds compress toward a floor when the note is short; order survives.
MM_TEST(score_short_notes_compress) {
    const Compiled slow = sing({{"AA S T", 50, 900}, {"S T R IY", 50, 900}});
    const Compiled fast = sing({{"AA S T", 50, 260}, {"S T R IY", 50, 260}});
    CHECK(slow.n > 0 && fast.n == slow.n);                           // same gestures, less time
    auto onsetSpan = [](const Compiled& c) {
        uint32_t first = 0xFFFFFFFFu;
        for (int i = 0; i < c.n; ++i)
            if (isSet(c.ev[i]) && c.ev[i].tick > c.info.beat[0] + 2000 && c.ev[i].tick < first) first = c.ev[i].tick;
        return c.info.beat[1] - first;
    };
    CHECK(onsetSpan(fast) < onsetSpan(slow));
    for (int i = 0; i < fast.n; ++i) CHECK(fast.ev[i].tick <= fast.info.length);
    Voice v;                                                         // and it still sings: finite, ends clean
    const Syllable syls[] = {{"AA S T", 50, 260}, {"S T R IY", 50, 260}};
    CHECK(v.sing(syls, 2));
    const std::vector<float> x = render(v, 66150);
    for (float s : x) CHECK(std::isfinite(s));
    CHECK(!v.state().slotActive[0] && !v.state().slotActive[1]);
}

// event count stays bounded; an overflow is a clean false.
MM_TEST(score_event_count_is_bounded) {
    const Compiled line = sing({{"T W IH NG", 60, 400}, {"K AH L", 60, 400}, {"T W IH NG", 67, 400}, {"K AH L", 67, 400},
                                {"L IH", 69, 400}, {"T AH L", 69, 400}, {"S T AA R", 67, 800}});
    CHECK_MSG(line.n > 0 && line.n < 160, "a sung line is %d events", line.n);
    std::vector<Syllable> many(kMaxSyllables, Syllable{"S T R EH NG K TH S", 60, 900});   // far too much
    const Compiled big = sing(many);
    CHECK(big.n == -1);
    std::vector<Syllable> tooMany((size_t)kMaxSyllables + 1, Syllable{"AA", 60, 200});
    CHECK(sing(tooMany).n == -1);
    std::vector<Syllable> justVowels((size_t)kMaxSyllables, Syllable{"AA", 60, 200});
    const Compiled v = sing(justVowels);
    CHECK(v.n > 0 && v.n <= kMaxEvents);
}

// bad phoneme and overflow return false and disturb nothing.
MM_TEST(score_failures_disturb_nothing) {
    const Syllable good[] = {{"M AA", 50, 400}, {"M IY", 52, 400}};
    const Syllable bad[] = {{"M AA", 50, 400}, {"M QQ", 52, 400}};
    std::vector<Syllable> huge(kMaxSyllables, Syllable{"S T R EH NG K TH S", 60, 900});
    Voice a, b;
    CHECK(a.sing(good, 2));
    CHECK(b.sing(good, 2));
    std::vector<float> xa = render(a, 8820), xb = render(b, 8820);
    CHECK(!b.sing(bad, 2));                                          // mid-song: refused...
    CHECK(!b.sing(huge.data(), (int)huge.size()));
    CHECK(!b.say("HH EH L OW W ER L D 7"));
    CHECK(!b.syllable(""));
    CHECK(!b.sing(nullptr, 0));
    CHECK(b.busy());
    std::vector<float> ya = render(a, 44100), yb = render(b, 44100);
    CHECK(memcmp(xa.data(), xb.data(), xa.size() * sizeof(float)) == 0);
    CHECK(memcmp(ya.data(), yb.data(), ya.size() * sizeof(float)) == 0);   // ...and the song never noticed
    CHECK(!b.busy());

    Voice c;                                                         // a good score right behind a bad one still plays
    CHECK(!c.sing(bad, 2));
    CHECK(!c.busy());
    CHECK(c.sing(good, 2));
    CHECK(c.busy());
}

// say(): consonants between vowels join the next syllable, except at a "/" word end.
MM_TEST(score_say_syllabifies) {
    Compiled joined, split;
    joined.n = compileSay("AH N AA", 50.0f, ScoreOptions(), joined.ev, kMaxEvents, &joined.info);
    split.n = compileSay("AH N / AA", 50.0f, ScoreOptions(), split.ev, kMaxEvents, &split.info);
    CHECK(joined.n > 0 && split.n > 0 && joined.info.syllables == 2 && split.info.syllables == 2);
    auto nClear = [](const Compiled& c) { for (int i = 0; i < c.n; ++i) if (isClear(c.ev[i])) return c.ev[i].tick; return 0u; };
    CHECK(nClear(joined) == joined.info.beat[1]);                    // an onset: released on the second beat
    CHECK(nClear(split) < split.info.beat[1]);                       // a coda: done before it
    Compiled hello;
    hello.n = compileSay("HH AH0 L OW1 / W ER1 L D", 50.0f, ScoreOptions(), hello.ev, kMaxEvents, &hello.info);
    CHECK(hello.n > 0 && hello.info.syllables == 3);
    CHECK(compileSay("HH AH0 L OW1 / W ER1 L D X", 50.0f, ScoreOptions(), hello.ev, kMaxEvents) == -1);
    CHECK(compileSay("   ", 50.0f, ScoreOptions(), hello.ev, kMaxEvents) == -1);
}

// syllable(): now, at the current note — it does not touch breath or pitch.
MM_TEST(score_syllable_leaves_breath_and_pitch_alone) {
    Compiled c;
    ScoreOptions opt;
    opt.breathEvents = false;
    opt.pitchEvents = false;
    c.n = compileSyllable("B AA", opt, c.ev, kMaxEvents, &c.info);
    CHECK(c.n > 0);
    CHECK(countChannel(c, kChBreath) == 0 && countChannel(c, kChPitch) == 0 && countChannel(c, kChNoteOn) == 0);
    CHECK(c.ev[0].tick == 0);                                        // it starts now: the vowel lands after the onset
    Voice v;
    v.noteOn(57.0f, 0.8f);
    CHECK(v.syllable("B AA"));
    render(v, 22050);
    CHECK(std::fabs(v.state().breath - 0.8f) < 0.01f && std::fabs(v.state().midi - 57.0f) < 0.01f);
    CHECK(v.body().bursts() == 1);
}

// the last writer wins, per channel, in the caller's own order — even inside one block.
MM_TEST(score_last_writer_wins) {
    const Syllable line[] = {{"AA", 50, 600}};
    float buf[kBlock];
    {   // a setter called before sing() loses to the score's first events
        Voice v;
        v.setBreath(0.5f);
        v.setBreath(0.0f);
        CHECK(v.sing(line, 1));
        for (int i = 0; i < 40; ++i) v.render(buf, kBlock);
        CHECK(std::fabs(v.state().breath - 0.5f) < 0.05f);            // and sing() took the level last asked for
    }
    {   // one called after wins: the score's lips are due in the same block
        Voice v;
        CHECK(v.sing(line, 1));
        v.setLips(0.4f);
        for (int i = 0; i < 100; ++i) v.render(buf, kBlock);
        CHECK(std::fabs(v.state().lips - 0.4f) < 0.02f);
    }
    {   // stop() then sing(): the stop was for the old score
        Voice v;
        CHECK(v.sing(line, 1));
        for (int i = 0; i < 10; ++i) v.render(buf, kBlock);
        v.stop();
        CHECK(v.sing(line, 1));
        for (int i = 0; i < 40; ++i) v.render(buf, kBlock);
        CHECK(v.busy() && v.state().breath > 0.5f);
    }
    {   // a live hand on one channel leaves the score the others
        Voice v;
        CHECK(v.sing(line, 1));
        for (int i = 0; i < 20; ++i) v.render(buf, kBlock);
        v.setLips(0.4f);
        for (int i = 0; i < 100; ++i) v.render(buf, kBlock);
        CHECK(std::fabs(v.state().lips - 0.4f) < 0.02f && v.state().breath > 0.5f && v.busy());
    }
}

// stop(): the score ends, the mouth lets go, the voice falls silent.
MM_TEST(score_stop) {
    Voice v;
    const Syllable line[] = {{"M AA", 50, 2000}, {"N IY", 52, 2000}};
    CHECK(v.sing(line, 2));
    render(v, 11025);
    CHECK(v.busy() && v.state().phoneme != ph::none);
    v.stop();
    render(v, 33075);
    const BodyState s = v.state();
    CHECK(!v.busy() && !s.busy && s.breath == 0.0f && !s.slotActive[0] && !s.slotActive[1] && s.phoneme == ph::none);
    const std::vector<float> tail = render(v, 4410);
    for (float x : tail) CHECK(x == 0.0f);
}

// the readback names the phoneme being sung.
MM_TEST(score_phoneme_readback) {
    Voice v;
    const Syllable line[] = {{"S AA", 50, 600}};
    Compiled c = sing({{"S AA", 50, 600}});
    CHECK(v.sing(line, 1));
    render(v, (int)c.info.beat[0] - 1500);
    CHECK(v.state().phoneme == ph::S && v.state().hiss > 0.3f);
    render(v, 1500 + 8820);
    CHECK(v.state().phoneme == ph::AA && v.state().hiss == 0.0f && v.state().voicing > 0.4f);
    render(v, 66150);
    CHECK(v.state().phoneme == ph::none);
}

// out of silence the mouth is ready before the air: no voice before a word's
// first voiceless consonant releases, nothing out of a closure before it
// releases, the breath only once the first consonant is in place, and a word
// that opens on a vowel opens on its pose. live play keeps its own timing.
MM_TEST(score_starts_clean) {
    const char* voiceless[] = {"S AA1", "SH AA1", "F AA1", "TH AA1", "HH AA1", "P AA1", "T AA1", "K AA1",
                               "CH AA1", "K W IH1 T / IH1 T", "S T AA1 P / DH AE2 T"};
    for (const char* w : voiceless) {
        const uint32_t rel = firstClear(sayEvents(w));
        float worst = 0.0f;
        for (const Frame& f : sayFrames(w, 200))
            if (f.tick + 2 * kBlock <= rel && f.st.voicing > worst) worst = f.st.voicing;
        CHECK_MSG(worst < 0.02f, "%s: voicing %.2f before the first release", w, worst);
    }
    // a closure is shut before the breath: 40 dB under the vowel until it lets go (a voice bar is fine)
    for (const char* w : {"B AA1", "D AA1", "G AA1", "P AA1", "T AA1", "K AA1", "CH AA1", "JH AA1", "N AA1"}) {
        const Compiled c = sayEvents(w);
        const uint32_t rel = firstLetGo(c);
        const std::vector<Frame> f = sayFrames(w, 200);
        double before = -200.0, vowel = -200.0;
        for (const Frame& fr : f) {
            if (fr.tick + 2 * kBlock <= rel && fr.st.velum < 0.1f && fr.db > before) before = fr.db;
            if (fr.tick > c.info.beat[0] + 4410 && fr.tick < c.info.beat[0] + 8820 && fr.db > vowel) vowel = fr.db;
        }
        CHECK_MSG(before < vowel - 40.0, "%s: %.1f dB before the release, vowel %.1f", w, before, vowel);
    }
    // every first consonant has started moving before the breath comes
    for (const char* w : {"S AA1", "P AA1", "T AA1", "K AA1", "CH AA1", "B AA1", "D AA1", "G AA1", "M AA1", "W AA1",
                          "L AA1", "HH AA1", "K W IH1 T"}) {
        const Compiled c = sayEvents(w);
        uint32_t set = 0xFFFFFFFFu, up = 0xFFFFFFFFu;
        for (int i = 0; i < c.n; ++i) {
            if (c.ev[i].phone && c.ev[i].phone != ph::clear && isConsonant(c.ev[i].phone) && c.ev[i].tick < set) set = c.ev[i].tick;
            if (c.ev[i].channel == kChBreath && c.ev[i].a > 0.0f && c.ev[i].tick < up) up = c.ev[i].tick;
        }
        CHECK_MSG(set < up, "%s: consonant at %u, breath at %u", w, set, up);
    }
    // a vowel-initial word: the tongue is where the vowel wants it when the voice starts
    for (const char* w : {"IY1", "UW1", "IH1 T", "EH1 G", "ER1"}) {
        const uint8_t p = parsePhone(w, 2);
        const float pos = (vowel(p).tongueX - kTongueXMin) / (kTongueXMax - kTongueXMin);
        const float height = (kTongueDMax - vowel(p).tongueD) / (kTongueDMax - kTongueDMin);
        for (const Frame& f : sayFrames(w, 100)) {
            if (f.st.voicing <= 0.1f) continue;
            CHECK_MSG(std::fabs(f.st.tonguePos - pos) < 0.1f && std::fabs(f.st.tongueHeight - height) < 0.1f,
                      "%s: tongue %.2f %.2f, the vowel's %.2f %.2f", w, f.st.tonguePos, f.st.tongueHeight, pos, height);
            break;
        }
    }
    // syllable(): under a held note a voiceless onset still voices until part way into its approach
    Compiled live;
    ScoreOptions opt;
    opt.breathEvents = false;
    opt.pitchEvents = false;
    live.n = compileSyllable("S AA", opt, live.ev, kMaxEvents, &live.info);
    uint32_t set = 0, open = 0;
    for (int i = 0; i < live.n; ++i) {
        if (isSet(live.ev[i]) && live.ev[i].phone == ph::S) set = live.ev[i].tick;
        if (live.ev[i].channel == kChGlottis && live.ev[i].a > 0.0f) open = live.ev[i].tick;
    }
    CHECK_MSG(open > set, "syllable S: glottis at %u, the S at %u", open, set);
}

// into silence the glottis stays open until the breath has gone: no voice
// comes back after a word's last voiceless consonant, at the end or before a
// rest. followed by a vowel, a stop still voices again kVotMs after it lets go.
MM_TEST(score_ends_clean) {
    const char* words[] = {"K W IH1 T / IH1 T", "AW1 CH", "S T AA1 P", "B AE1 K", "M IH1 S", "F IH1 SH", "B AE1 TH",
                           "K AE1 T S", "HH AH1 F", "AA1 T _ M AA1", "AA1 S _ AA1"};
    for (const char* w : words) {
        const Compiled c = sayEvents(w);
        uint32_t fall = 0, rel = 0, again = 0xFFFFFFFFu;
        for (int i = 0; i < c.n; ++i)
            if (c.ev[i].channel == kChBreath && c.ev[i].a == 0.0f) { fall = c.ev[i].tick; break; }
        for (int i = 0; i < c.n; ++i) {
            if (isClear(c.ev[i]) && c.ev[i].tick <= fall) rel = c.ev[i].tick;
            if (c.ev[i].channel == kChBreath && c.ev[i].a > 0.0f && c.ev[i].tick > fall && c.ev[i].tick < again) again = c.ev[i].tick;
        }
        Voice v;
        v.setPitch(50.0f);
        CHECK(v.say(w));
        float buf[kBlock], worst = 0.0f;
        for (int b = 0; b < 600; ++b) {
            v.render(buf, kBlock);
            const uint32_t tick = (uint32_t)(b * kBlock);
            if (tick >= rel + kBlock && tick + 2 * kBlock <= again && v.state().voicing > worst) worst = v.state().voicing;
        }
        CHECK_MSG(worst < 0.02f, "%s: voicing %.2f after the last release", w, worst);
        const std::vector<float> tail = render(v, 4410);
        CHECK_MSG(!v.busy() && v.state().glottis < 0.05f && tail.back() == 0.0f, "%s: never came to rest", w);
    }
    // a stop before a vowel keeps its voice-onset time: the glottis shuts kVotMs after the release
    const Compiled c = sayEvents("AA1 T AA1");
    uint32_t rel = 0, shut = 0;
    for (int i = 0; i < c.n; ++i) {
        if (isClear(c.ev[i])) rel = c.ev[i].tick;
        if (c.ev[i].channel == kChGlottis && c.ev[i].a == 0.0f) shut = c.ev[i].tick;
    }
    CHECK_MSG(shut == rel + (uint32_t)(kVotMs * kDefaultFs * 0.001f), "T: released %u, glottis shut %u", rel, shut);
}

// ---- speak() -------------------------------------------------------------------------------------

namespace {

Compiled speakEvents(const char* phones, const Speech& sp = Speech()) {
    Compiled c;
    c.n = compileSpeak(phones, sp, ScoreOptions(), c.ev, kMaxEvents, &c.info);
    return c;
}

// how long each syllable's vowel sounds before the next gesture starts (or the line ends)
float vowelMs(const Compiled& c, int k) {
    const uint32_t from = c.info.beat[k];
    for (int i = 0; i < c.n; ++i) if (isSet(c.ev[i]) && c.ev[i].tick > from) return (float)(c.ev[i].tick - from) / 44.1f;
    return (float)(c.info.length - from) / 44.1f;
}

// a speak() block by block, as sayFrames
std::vector<Frame> speakFrames(Voice& v, const char* phones, const Speech& sp, int blocks = 600) {
    std::vector<Frame> f;
    if (!v.speak(phones, sp)) return f;
    float buf[kBlock];
    for (int b = 0; b < blocks; ++b) {
        v.render(buf, kBlock);
        f.push_back(Frame{(uint32_t)(b * kBlock), v.state(), toDb(rms(buf, kBlock))});
    }
    return f;
}

}  // namespace

// a vowel's length is its stress: primary, then secondary, then unstressed;
// a diphthong is longer, the line's last syllable longer still; the speed divides it all.
MM_TEST(speak_durations_follow_stress) {
    const Compiled c = speakEvents("D AA1 D AA2 D AA0 D AA1");
    CHECK(c.n > 0 && c.info.syllables == 4);
    const float p = vowelMs(c, 0), s = vowelMs(c, 1), u = vowelMs(c, 2), last = vowelMs(c, 3);
    CHECK_MSG(std::fabs(p - kSpeakPrimaryMs) < 1.0f && std::fabs(s - kSpeakSecondaryMs) < 1.0f &&
              std::fabs(u - kSpeakUnstressedMs) < 1.0f, "vowels %.1f %.1f %.1f ms", p, s, u);
    CHECK_MSG(std::fabs(last - kSpeakPrimaryMs * kSpeakFinal) < 1.0f, "the last vowel %.1f ms", last);
    CHECK_MSG(std::fabs(vowelMs(speakEvents("D AA D AA0"), 0) - kSpeakPrimaryMs) < 1.0f, "no digit is primary");
    CHECK(std::fabs(vowelMs(speakEvents("D AY1 D AA0"), 0) - kSpeakPrimaryMs * kSpeakDiphthong) < 1.0f);
    Speech fast;
    fast.speed = 2.0f;
    const Compiled f = speakEvents("D AA1 D AA2 D AA0 D AA1", fast);
    CHECK_MSG(std::fabs(vowelMs(f, 0) - 0.5f * kSpeakPrimaryMs) < 1.0f, "speed 2: %.1f ms", vowelMs(f, 0));
    const float ratio = (float)f.info.length / (float)c.info.length;
    CHECK_MSG(ratio > 0.45f && ratio < 0.6f, "speed 2: the line is %.2f as long", ratio);
    // say() is untouched by the digits: every vowel its default length
    const Compiled say = sayEvents("D AA1 D AA0 D AA1");
    CHECK(std::fabs(vowelMs(say, 0) - vowelMs(say, 1)) < 1.0f);
}

// the pitch keeps moving: an unaccented syllable on the baseline, the accent
// lifted by the range in its own vowel, the end below the baseline. glides, not steps.
MM_TEST(speak_pitch_peaks_on_the_accent_and_ends_low) {
    Speech sp;
    sp.midi = 50.0f;
    sp.range = 4.0f;
    const char* line = "B AH0 N AE1 N AH0";                          // banana
    const Compiled c = speakEvents(line, sp);
    Voice v;
    v.setGlide(0.0f);                                                // the voice's glide is a jump: speech glides anyway
    const std::vector<Frame> f = speakFrames(v, line, sp);
    float peak = 0.0f, lastVoiced = 0.0f, firstVowel = 0.0f, worstStep = 0.0f, prev = -1.0f;
    uint32_t peakAt = 0;
    for (const Frame& fr : f) {
        if (fr.st.breath > 0.0f && prev >= 0.0f && std::fabs(fr.st.midi - prev) > worstStep) worstStep = std::fabs(fr.st.midi - prev);
        prev = fr.st.breath > 0.0f ? fr.st.midi : -1.0f;
        if (fr.st.voicing < 0.1f) continue;
        if (fr.st.midi > peak) { peak = fr.st.midi; peakAt = fr.tick; }
        if (fr.tick >= c.info.beat[0] && fr.tick < c.info.beat[0] + 2205 && firstVowel == 0.0f) firstVowel = fr.st.midi;
        lastVoiced = fr.st.midi;
    }
    CHECK_MSG(peakAt > c.info.beat[1] - 2205 && peakAt < c.info.beat[2], "peak at %u, accent %u .. %u",
              peakAt, c.info.beat[1], c.info.beat[2]);
    CHECK_MSG(peak > sp.midi + 0.7f * sp.range && peak <= sp.midi + sp.range, "peak %.2f", peak);
    CHECK_MSG(std::fabs(firstVowel - sp.midi) < 0.5f, "the unaccented first syllable at %.2f", firstVowel);
    const float low = sp.midi - kSpeakDeclination - sp.range * kSpeakFinalDrop;
    CHECK_MSG(lastVoiced < sp.midi - kSpeakDeclination && lastVoiced < low + 1.0f, "ends at %.2f, low %.2f", lastVoiced, low);
    CHECK_MSG(worstStep < 0.4f, "a step of %.2f semitones in one block", worstStep);

    // the accent sits where the stress is, whichever syllable that is
    Voice w;
    const std::vector<Frame> g = speakFrames(w, "K W IH1 T / IH0 T", sp);
    const Compiled q = speakEvents("K W IH1 T / IH0 T", sp);
    float qPeak = 0.0f;
    uint32_t qAt = 0;
    for (const Frame& fr : g) if (fr.st.voicing > 0.1f && fr.st.midi > qPeak) { qPeak = fr.st.midi; qAt = fr.tick; }
    CHECK_MSG(qAt < q.info.beat[1] && qPeak > sp.midi + 0.7f * sp.range, "quit it: peak %.2f at %u, it at %u", qPeak, qAt, q.info.beat[1]);
}

// no vibrato while speaking, from the first voiced block; afterwards the voice
// sings with the vibrato and glide it was given, as if it had never spoken.
MM_TEST(speak_has_no_vibrato_and_leaves_the_voice_as_set) {
    Voice v;
    float buf[kBlock];
    v.setVibrato(5.0f, 40.0f, 0.1f);                                 // quick and deep: it would show in a spoken line
    v.setGlide(0.0f);
    CHECK(v.speak("S T AA1 P / DH AE2 T"));
    float worst = 0.0f;
    int blocks = 0;
    for (; blocks < 1000 && (blocks == 0 || v.busy()); ++blocks) {
        v.render(buf, kBlock);
        if (std::fabs(v.state().vibrato) > worst) worst = std::fabs(v.state().vibrato);
    }
    CHECK_MSG(worst == 0.0f && blocks > 200, "vibrato %.2f cents while speaking (%d blocks)", worst, blocks);
    for (int i = 0; i < 100; ++i) v.render(buf, kBlock);
    v.noteOn(57.0f, 0.8f);
    v.render(buf, kBlock);
    CHECK_MSG(v.state().midi == 57.0f, "the glide is still a jump: %.2f", v.state().midi);
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < 700; ++i) {
        v.render(buf, kBlock);
        if (i > 350) { lo = std::fmin(lo, v.state().vibrato); hi = std::fmax(hi, v.state().vibrato); }
    }
    CHECK_MSG(hi > 39.5f && hi <= 40.0f && lo < -39.5f, "noteOn after speak: swing %.2f .. %.2f cents", lo, hi);

    Voice s;                                                         // and a sing() after it, the voice's default vibrato
    CHECK(s.speak("AW1 CH"));
    for (int i = 0; i < 400; ++i) s.render(buf, kBlock);
    const Syllable held[] = {{"AA", 50.0f, 2000}};
    CHECK(s.sing(held, 1));
    hi = 0.0f;
    for (int i = 0; i < 600; ++i) { s.render(buf, kBlock); if (i > 400) hi = std::fmax(hi, s.state().vibrato); }
    CHECK_MSG(hi > kDefaultVibCents - 0.5f && hi <= kDefaultVibCents, "sing after speak: vibrato %.2f cents", hi);
}

// out of silence and into it, the same as say(): no voice before the first
// voiceless consonant releases, none after the last one.
MM_TEST(speak_starts_and_ends_clean) {
    Speech sp;
    sp.midi = 50.0f;
    sp.range = 5.0f;
    for (const char* w : {"S T AA1 P / DH AE2 T", "K W IH1 T / IH0 T", "AW1 CH", "P AA1 TH"}) {
        const Compiled c = speakEvents(w, sp);
        uint32_t lastRel = 0, fall = c.info.length;
        for (int i = 0; i < c.n; ++i) if (isClear(c.ev[i])) lastRel = c.ev[i].tick;
        const uint32_t firstRel = w[0] == 'A' ? 0u : firstClear(c);
        Voice v;
        const std::vector<Frame> f = speakFrames(v, w, sp);
        float before = 0.0f, after = 0.0f;
        for (const Frame& fr : f) {
            if (fr.tick + 2 * kBlock <= firstRel) before = std::fmax(before, fr.st.voicing);
            if (fr.tick >= lastRel + kBlock) after = std::fmax(after, fr.st.voicing);
        }
        CHECK_MSG(before < 0.02f && after < 0.02f, "%s: voicing %.2f before the first release, %.2f after the last", w, before, after);
        CHECK_MSG(!v.busy() && v.state().glottis < 0.05f && f.back().db < -200.0, "%s: never came to rest", w);
        CHECK(fall > lastRel);
    }
}

// loudness follows stress; Speech.breath sets the stressed level and the voice's own level stays.
MM_TEST(speak_loudness_follows_stress) {
    const char* line = "K W IH1 T / IH0 T";
    const Compiled c = speakEvents(line);
    auto breathAt = [&](Voice& v, const Speech& sp, uint32_t tick) {
        float b = -1.0f;
        for (const Frame& fr : speakFrames(v, line, sp, 400)) if (fr.tick <= tick) b = fr.st.breath;
        return b;
    };
    const uint32_t stressed = c.info.beat[0] + 2205, unstressed = c.info.beat[1] + 2205;   // 50 ms into each vowel
    Voice a;
    const float hiB = breathAt(a, Speech(), stressed);
    Voice b;
    const float loB = breathAt(b, Speech(), unstressed);
    CHECK_MSG(std::fabs(hiB - kScoreBreath) < 0.02f, "stressed breath %.3f", hiB);
    CHECK_MSG(std::fabs(loB - kScoreBreath * kSpeakUnstressedBreath) < 0.03f, "unstressed breath %.3f", loB);
    Speech soft;
    soft.breath = 0.5f;
    Voice s;
    const float softB = breathAt(s, soft, stressed);
    CHECK_MSG(std::fabs(softB - 0.5f) < 0.02f, "Speech.breath 0.5: %.3f", softB);
    float buf[kBlock], peak = 0.0f;                                  // and the voice's own level is untouched
    CHECK(s.say("AA1"));
    for (int i = 0; i < 100; ++i) { s.render(buf, kBlock); peak = std::fmax(peak, s.state().breath); }
    CHECK_MSG(std::fabs(peak - kScoreBreath) < 0.02f, "say() after a soft speak: %.3f", peak);
}

// a bad phoneme or an overflow is a clean false that disturbs nothing; busy() and stop() work on it as on any score.
MM_TEST(speak_failures_disturb_nothing) {
    std::string huge;
    for (int i = 0; i < 40; ++i) huge += "S T R EH1 NG K TH S / ";
    Voice a, b;
    CHECK(a.speak("K W IH1 T / IH0 T"));
    CHECK(b.speak("K W IH1 T / IH0 T"));
    std::vector<float> xa = render(a, 8820), xb = render(b, 8820);
    CHECK(!b.speak("S T AA1 QQ"));
    CHECK(!b.speak(huge.c_str()));
    CHECK(!b.speak(""));
    CHECK(!b.speak("   "));
    CHECK(!b.speak(nullptr));
    CHECK(b.busy());
    std::vector<float> ya = render(a, 44100), yb = render(b, 44100);
    CHECK(memcmp(xa.data(), xb.data(), xa.size() * sizeof(float)) == 0);
    CHECK(memcmp(ya.data(), yb.data(), ya.size() * sizeof(float)) == 0);
    CHECK(!b.busy());

    Voice c;
    CHECK(!c.speak("AW1 QQ"));
    CHECK(!c.busy());
    CHECK(c.speak("AW1 CH"));
    CHECK(c.busy());
    render(c, 4410);
    c.stop();
    render(c, 33075);
    CHECK(!c.busy() && c.state().breath == 0.0f && !c.state().slotActive[0] && !c.state().slotActive[1]);
    const std::vector<float> tail = render(c, 4410);
    for (float x : tail) CHECK(x == 0.0f);
}
