// wind_pipe — tests/test_whistle.cpp  (whistle plan §6, §7.3, stage 3)
// whistle(midi, breath) through the table: every row locks on its note, a
// note change slides, and a note outside the range folds into it by octaves.
#include <cmath>
#include <vector>

#include "core/wp_voice.h"
#include "wp_analysis.h"
#include "wp_test.h"

using namespace wp;

namespace {

double midiToHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
double cents(double hz, double midi) { return 1200.0 * std::log2(hz / midiToHz(midi)); }

std::vector<float> render(Voice& v, float seconds) {
    std::vector<float> x((size_t)(seconds * kDefaultFs));
    v.render(x.data(), (int)x.size());
    return x;
}

// the table's rows, whistled at breath 0.8 for 1 s: the lock's pitch in
// cents from the note, and the body's whistle
struct RowLock { double cents; float whistle; };
RowLock whistleRow(int sections, int row) {
    const double note = (double)row - 12.0 * std::log2((double)sections / (double)kDefaultSections);
    Voice v(sections, 1);
    v.whistle((float)note, 0.8f);
    render(v, 1.0f);
    const BodyState s = v.state();
    return {s.whistleHz > 0.0f ? cents(s.whistleHz, note) : 1e9, s.whistle};
}

}  // namespace

// every row of the table, at the size it was fitted at, whistles its note
// within 50 cents at breath 0.8. at 16 and 26 the size shift plays the same
// pose for a note moved by 12 log2(N / 22): every lip mode scales with 1 / N,
// but the jet's transit is physical and does not, so the lock pulls away from
// the mode, and further the nearer the pose sits to its threshold. measured
// at the fitted table: at 16 the lowest rows sit flat, row 77 by 119 cents and
// row 78 by 136; at 26 row 78 sits 117 cents sharp and the top rows up to 91
// flat. the tolerance there is 160 cents, the worst plus a margin. and the
// jet's reach in hertz does not move with size: at 16 the top two rows would
// whistle past 2.5 kHz and do not lock (row 94) or lock on another mode (row
// 95); at 26 the bottom row would whistle under 600 Hz and does not lock.
// rows the size puts outside 600 .. 2500 Hz are not asked.
WP_TEST(voice_whistle_every_table_note_locks) {
    const double kSizeTolerance = 160.0;
    const double kReachLo = 600.0, kReachHi = 2500.0;
    for (int row = kWhistleMidiLo; row < kWhistleMidiLo + kWhistleCount; ++row) {
        const RowLock r = whistleRow(kDefaultSections, row);
        CHECK_MSG(r.whistle > 0.5f && std::fabs(r.cents) <= 50.0, "N=22 row %d: %+.0f cents, whistle %.3f", row, r.cents,
                  r.whistle);
    }
    for (int n : {16, 26})
        for (int row = kWhistleMidiLo; row < kWhistleMidiLo + kWhistleCount; ++row) {
            const double hz = midiToHz(row) * (double)kDefaultSections / (double)n;
            if (hz < kReachLo || hz > kReachHi) continue;
            const RowLock r = whistleRow(n, row);
            CHECK_MSG(r.whistle > 0.5f && std::fabs(r.cents) <= kSizeTolerance, "N=%d row %d (%.0f Hz): %+.0f cents, whistle %.3f",
                      n, row, hz, r.cents, r.whistle);
        }
}

// a note change is a slide: the tongue and lips ease to the next row, the
// lock holds through it, and the pitch moves one way. once it lands, a held
// lock's pitch jitters by a tenth of a hertz: a step back of up to a cent is
// the lock holding, not the slide turning.
WP_TEST(voice_whistle_slides) {
    const float from = 84.0f, to = 89.0f;
    Voice v(kDefaultSections, 1);
    v.whistle(from, 0.8f);
    render(v, 0.8f);
    const float hzFrom = v.state().whistleHz;
    v.whistle(to, 0.8f);
    const int frame = 1024;                                             // 23 ms
    std::vector<float> buf((size_t)frame);
    float last = hzFrom, worstPer = 1.0f, quietest = 0.0f, fall = 0.0f;
    bool first = true;
    for (int f = 0; f < (int)(0.6f * kDefaultFs) / frame; ++f) {
        v.render(buf.data(), frame);
        const float hz = v.state().whistleHz;
        const double per = periodicity(buf.data(), frame, (int)(kDefaultFs / 6000.0f), (int)(kDefaultFs / 300.0f));
        const double level = toDb(rms(buf.data(), frame));
        if (per < worstPer) worstPer = (float)per;
        if (first || level < quietest) quietest = (float)level;
        first = false;
        const float back = hz > 0.0f && last > 0.0f ? 1200.0f * std::log2(last / hz) : 0.0f;
        if (back > fall) fall = back;
        last = hz;
    }
    const float hzTo = v.state().whistleHz;
    CHECK_MSG(std::fabs(cents(hzFrom, from)) <= 50.0 && std::fabs(cents(hzTo, to)) <= 50.0, "%.1f Hz then %.1f Hz", hzFrom, hzTo);
    CHECK_MSG(worstPer > 0.9f, "periodicity fell to %.3f in the slide", worstPer);
    CHECK_MSG(quietest > -45.0f, "the slide fell to %.1f dB", quietest);
    CHECK_MSG(fall <= 1.0f, "whistleHz stepped back %.2f cents in a rising slide", fall);
    CHECK_MSG(hzTo > hzFrom * 1.3f, "%.1f Hz to %.1f Hz: no slide", hzFrom, hzTo);
    CHECK(v.state().whistle > 0.5f);
}

// a note under the table's range whistles the pose of the same note raised
// by octaves into it, and one over it the pose lowered into it: the same
// pose, so the same pitch. the pitch channel keeps the asked note.
WP_TEST(voice_whistle_folds_octaves) {
    float lo, hi;
    Voice probe;
    probe.whistleRange(lo, hi);
    CHECK(lo == (float)kWhistleMidiLo && hi == (float)(kWhistleMidiLo + kWhistleCount - 1));
    const float inside = 84.0f;
    CHECK(inside >= lo && inside <= hi);
    for (float note : {48.0f, 60.0f, 108.0f}) {
        Voice a(kDefaultSections, 1), b(kDefaultSections, 1);
        a.whistle(note, 0.8f);
        b.whistle(inside, 0.8f);
        render(a, 1.0f);
        render(b, 1.0f);
        const BodyState sa = a.state(), sb = b.state();
        CHECK_MSG(sa.tonguePos == sb.tonguePos && sa.tongueHeight == sb.tongueHeight && sa.lips == sb.lips &&
                  sa.slotActive[0] == sb.slotActive[0] && sa.slotPlace[0] == sb.slotPlace[0] &&
                  sa.slotDegree[0] == sb.slotDegree[0], "whistle(%.0f): not the pose of whistle(%.0f)", note, inside);
        CHECK_MSG(sa.whistle > 0.5f && std::fabs(1200.0 * std::log2(sa.whistleHz / sb.whistleHz)) < 5.0,
                  "whistle(%.0f) %.1f Hz, whistle(%.0f) %.1f Hz", note, sa.whistleHz, inside, sb.whistleHz);
        CHECK(std::fabs(sa.midi - note) < 0.01f);
    }
    // a voice of another size folds its own range, shifted by its size
    Voice small(16, 1);
    small.whistleRange(lo, hi);
    const float shift = 12.0f * std::log2(16.0f / 22.0f);
    CHECK(std::fabs(lo - ((float)kWhistleMidiLo - shift)) < 1e-4f && std::fabs(hi - lo - (float)(kWhistleCount - 1)) < 1e-4f);
}

// the table's lookup: rows land exactly, a fraction interpolates, the ends clamp
WP_TEST(vocab_whistle_pose_interpolates) {
    const WhistlePose& r0 = kWhistleTable[0];
    const WhistlePose& rN = kWhistleTable[kWhistleCount - 1];
    const WhistlePose lo = whistlePose((float)kWhistleMidiLo - 5.0f), hi = whistlePose((float)(kWhistleMidiLo + kWhistleCount + 5));
    CHECK(lo.tongueX == r0.tongueX && lo.lipD == r0.lipD && lo.slotPlace == r0.slotPlace);
    CHECK(hi.tongueX == rN.tongueX && hi.lipD == rN.lipD && hi.slotPlace == rN.slotPlace);
    const WhistlePose nan = whistlePose(std::nanf(""));
    CHECK(nan.tongueX == r0.tongueX);
    for (int i = 0; i + 1 < kWhistleCount; ++i) {
        const WhistlePose& a = kWhistleTable[i];
        const WhistlePose& b = kWhistleTable[i + 1];
        const WhistlePose at = whistlePose((float)(kWhistleMidiLo + i));
        CHECK(at.tongueX == a.tongueX && at.tongueD == a.tongueD && at.lipD == a.lipD && at.slotPlace == a.slotPlace &&
              at.slotDegree == a.slotDegree);
        const WhistlePose q = whistlePose((float)(kWhistleMidiLo + i) + 0.25f);
        CHECK(std::fabs(q.tongueX - lerpf(a.tongueX, b.tongueX, 0.25f)) < 1e-5f);
        CHECK(std::fabs(q.lipD - lerpf(a.lipD, b.lipD, 0.25f)) < 1e-5f);
        if (a.slotPlace >= 0.0f && b.slotPlace >= 0.0f)
            CHECK(std::fabs(q.slotPlace - lerpf(a.slotPlace, b.slotPlace, 0.25f)) < 1e-5f);
        else
            CHECK(q.slotPlace == a.slotPlace);                           // the nearer row's
    }
}

// whistle() ends any words in flight: a line cut off mid-way stops playing
// at the next block, and its later events — the next onset, the next vowel's
// pose — never land on the whistle.
WP_TEST(voice_whistle_ends_words) {
    const Syllable line[] = {{"S AA", 48.0f, 450}, {"Z UW M", 50.0f, 600}, {"F IY", 55.0f, 500},
                             {"HH OW", 60.0f, 450}, {"W UW", 67.0f, 600}};
    const float note = 86.0f;
    Voice v(kDefaultSections, 1);
    CHECK(v.sing(line, 5));
    float buf[kBlock];
    for (int b = 0; b < (int)(0.7f * kDefaultFs) / kBlock; ++b) v.render(buf, kBlock);   // inside the second syllable, on a block edge
    CHECK(v.busy());
    v.whistle(note, 0.8f);
    v.render(buf, kBlock);                                               // the one block it lands in
    CHECK_MSG(!v.busy(), "still busy a block after the whistle");
    CHECK_MSG(v.state().phoneme == ph::none, "phoneme %s", phoneName(v.state().phoneme));
    render(v, 0.3f - (float)kBlock / kDefaultFs);
    const BodyState s = v.state();
    CHECK_MSG(s.whistle > 0.5f && s.whistleHz > 0.0f && std::fabs(cents(s.whistleHz, note)) <= 50.0,
              "300 ms on: whistle %.3f at %.1f Hz (%+.0f cents)", s.whistle, s.whistleHz,
              s.whistleHz > 0.0f ? cents(s.whistleHz, note) : 0.0);
    CHECK_MSG(s.phoneme == ph::none, "phoneme %s", phoneName(s.phoneme));
    CHECK(!s.slotActive[1]);
}
