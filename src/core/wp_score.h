// wind_pipe — wp_score.h  (plan §9)
// words on notes. the compiler turns phonemes into a time-sorted list of
// (tick, channel, target, rate); the player pops due events into the same
// target table the live setters write. scripted singing is a scripted hand.
#pragma once

#include "wp_config.h"
#include "wp_vocab.h"

namespace wp {

// phones: ARPABET, space-separated, stress digits accepted; "_" = rest; "" = melisma
struct Syllable { const char* phones; float midi; uint16_t ms; };

// the target table's channels. live setters and score events share them.
enum Chan : uint8_t {
    kChBreath = 0,      // a = level 0..1
    kChPitch,           // a = midi; b > 0 also restarts the vibrato onset clock (a new note)
    kChGlottis,         // a = open 0..1
    kChTenseness,       // a = 0..1
    kChTongue,          // a = tongueX, b = tongueD (raw anatomy units)
    kChLips,            // a = lipD (raw; <= 0 closes)
    kChVelum,           // a = open 0..1
    kChSlot0,           // a = place (or kSlotClear), b = degree (<= 0 closes)
    kChSlot1,
    kChGlide,           // a = per second
    kChVibrato,         // a = rate Hz, b = cents
    kChVibratoOnset,    // a = seconds
    kChWobble,          // a = 0..1
    kChNoteOn,          // restarts the vibrato onset clock
    kChStop,            // abandon the score, close everything down
    kChPhone,           // a score event carries only its phoneme tag
    kChCount
};
constexpr float kSlotClear = -1.0f;

struct Event {
    uint32_t tick;              // samples since the score was committed
    float    a, b, rate;        // rate <= 0: the channel's default
    uint8_t  channel, phone;    // phone: readback tag, 0 = leave, ph::clear = none
};

constexpr int kMaxEvents    = 256;
constexpr int kMaxSyllables = 64;
constexpr int kMaxCluster   = 6;

// ---- timing, tune by ear -----------------------------------------------------------------
constexpr float kVotMs            = 40.0f;   // glottis shuts this long after a voiceless stop's release
constexpr float kAffricateFricMs  = 70.0f;
constexpr float kCodaTailMs       = 30.0f;   // a coda's release gets this long to sound
constexpr float kMinVowelMs       = 80.0f;   // what compression leaves a vowel
constexpr float kHoldFloor        = 0.35f;   // holds compress toward this fraction
constexpr float kBreathLeadMs     = 30.0f;
constexpr float kVowelLeadMs      = 40.0f;   // an onsetless vowel's pose leads its beat
constexpr float kVelumLeadMs      = 30.0f;
constexpr float kVelumRate        = 35.0f;
constexpr float kGlideMoveRate    = 30.0f;   // tongue rate into and out of W / Y
constexpr float kGlideLeadMs      = 50.0f;
constexpr float kAspirateLeadMs   = 20.0f;
constexpr float kDiphthongAt      = 0.55f;
constexpr float kScoreBreathFall  = 25.0f;   // per second: rests and endings
constexpr float kSayVowelMs       = 150.0f;  // say(): default vowel lengths
constexpr float kSayDiphthongMs   = 210.0f;
constexpr float kSayRestMs        = 250.0f;

struct ScoreOptions {
    float fs = kDefaultFs;
    float breath = kScoreBreath;
    bool  breathEvents = true;       // false for syllable(): the player owns the breath
    bool  pitchEvents = true;        // false for syllable(): "at the current note"
    float diction = 1.0f;            // the tuning's: scales every consonant's close and release rate
};

struct ScoreInfo {
    int      events = 0;
    int      syllables = 0;                  // parsed syllables, rests and melismas
    uint32_t beat[kMaxSyllables] = {};       // each one's beat
    uint32_t length = 0;                     // tick of the end
};

// each returns the event count, or -1 on a bad phoneme or an overflow — in
// which case `out` holds nothing usable and nothing else was touched.
int compileSing(const Syllable* syls, int n, const ScoreOptions& opt,
                Event* out, int maxEvents, ScoreInfo* info = nullptr);
int compileSay(const char* phones, float midi, const ScoreOptions& opt,
               Event* out, int maxEvents, ScoreInfo* info = nullptr);
int compileSyllable(const char* phones, const ScoreOptions& opt,
                    Event* out, int maxEvents, ScoreInfo* info = nullptr);

// ---- the player ------------------------------------------------------------------------------
// two buffers: the audio side plays one while the caller fills the other;
// commit is an index flip at the top of a block. single-core discipline: the
// caller may be preempted by the audio side, never the reverse.
class ScorePlayer {
public:
    // caller side. seq orders this commit among the caller's other writes.
    void commit(const Event* events, int count, uint32_t seq = 0);
    void withdraw() { pending_ = false; }            // a commit not yet taken is dropped
    bool busy() const { return busy_; }

    // audio side, once per block, in this order
    bool commitWaiting() const { return pending_; }  // beginBlock() is about to start a new score
    uint32_t waitingSeq() const { return seq_[1 - active_]; }
    bool beginBlock();                               // true: a new score starts this block
    const Event* next();                             // due events, one by one; null when none
    void endBlock();                                 // advance time by one block
    void abandon();                                  // stop playing (kChStop)

private:
    Event    buf_[2][kMaxEvents];
    int      count_[2] = {0, 0};
    uint32_t seq_[2] = {0, 0};
    volatile uint8_t active_ = 0;
    volatile bool    pending_ = false;
    volatile bool    busy_ = false;
    int      cursor_ = 0;
    uint32_t now_ = 0;
};

}  // namespace wp
