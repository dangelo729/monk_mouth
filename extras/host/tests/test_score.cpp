// wind_pipe — tests/test_score.cpp  (plan: Tests / Score)
#include <cstring>
#include <string>
#include <vector>

#include "core/wp_voice.h"
#include "wp_test.h"

using namespace wp;

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

}  // namespace

// for "B AA" at beat T the slot's clear event is at T: the onset releases on the beat.
WP_TEST(score_onset_releases_on_the_beat) {
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
WP_TEST(score_is_sorted_and_bracketed_by_breath) {
    const Compiled c = sing({{"HH AH", 55, 300}, {"L OW", 57, 500}, {"_", 0, 200}, {"W ER L D", 60, 700}});
    CHECK(c.n > 0);
    for (int i = 1; i < c.n; ++i) CHECK(c.ev[i - 1].tick <= c.ev[i].tick);
    CHECK(c.ev[0].channel == kChBreath && c.ev[0].a > 0.0f && c.ev[0].tick == 0);
    CHECK(c.ev[c.n - 1].channel == kChBreath && c.ev[c.n - 1].a == 0.0f && c.ev[c.n - 1].tick == c.info.length);
    CHECK(countChannel(c, kChBreath) == 4);                          // up, down at the rest, up again, down at the end
    CHECK(c.info.syllables == 4);
    CHECK(c.info.beat[1] - c.info.beat[0] == (uint32_t)(0.300f * kDefaultFs));   // beats are the score's own clock
}

// codas end inside their syllable.
WP_TEST(score_codas_end_inside_their_syllable) {
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
WP_TEST(score_melisma_is_pitch_only) {
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
WP_TEST(score_short_notes_compress) {
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
WP_TEST(score_event_count_is_bounded) {
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
WP_TEST(score_failures_disturb_nothing) {
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
WP_TEST(score_say_syllabifies) {
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
WP_TEST(score_syllable_leaves_breath_and_pitch_alone) {
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
WP_TEST(score_last_writer_wins) {
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
    {   // one called after wins
        Voice v;
        CHECK(v.sing(line, 1));
        v.setBreath(0.3f);
        for (int i = 0; i < 40; ++i) v.render(buf, kBlock);
        CHECK(std::fabs(v.state().breath - 0.3f) < 0.05f);
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
WP_TEST(score_stop) {
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
WP_TEST(score_phoneme_readback) {
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
