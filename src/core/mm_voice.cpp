// monk_mouth — mm_voice.cpp
#include "mm_voice.h"

namespace mm {

// the table was fitted at kDefaultSections: every mode of a voice N long
// scales by kDefaultSections / N, so its note sits this far up the table
static float whistleShiftAt(float sections) { return 12.0f * std::log2(sections / (float)kDefaultSections); }

Voice::Voice(float sections, uint32_t seed, float sampleRate)
    : fs_(std::isfinite(sampleRate) && sampleRate > 1000.0f ? sampleRate : kDefaultFs), tube_(sections) {
    rng_.seed(seed);
    glottis_.init(fs_);
    jet_.init(fs_);
    noise_.init(fs_);
    body_.init(tube_.layout().length, fs_);
    pitch_.init(fs_, kDefaultMidi);
    retune(tuning_);                                      // the defaults
    sections_ = sectionsLatched_ = tube_.layout().length;
    whistleShift_ = whistleShiftAt(sections_);
    for (int i = 0; i < kMaxSections; ++i) stateDiameters_[i] = 0.0f;
    publish();
}

// ---- the setters: every call sets a target -----------------------------------------------------
// a number that is not finite sets nothing: write() drops it, and a call that
// clamps it, keeps it or writes more than once checks it first.

void Voice::write(uint8_t ch, float a, float b) {
    if (!std::isfinite(a) || !std::isfinite(b)) return;
    pending_.a[ch] = a;
    pending_.b[ch] = b;
    pending_.seq[ch] = ++seq_;
    pending_.dirty |= 1u << ch;
}

void Voice::setBreath(float p01) {
    if (!std::isfinite(p01)) return;
    if (p01 > 0.0f) scoreBreath_ = p01;
    write(kChBreath, p01);
}
void Voice::setPitch(float midi) {
    if (!std::isfinite(midi)) return;
    midi_ = midi;
    write(kChPitch, midi);
}
void Voice::setGlottis(float open01)        { write(kChGlottis, open01); }
void Voice::setTenseness(float t01)         { write(kChTenseness, t01); }
void Voice::setTongue(float pos01, float height01) {
    if (!std::isfinite(pos01) || !std::isfinite(height01)) return;
    write(kChTongue, lerpf(kTongueXMin, kTongueXMax, clampf(pos01, 0.0f, 1.0f)),
                     lerpf(kTongueDMax, kTongueDMin, clampf(height01, 0.0f, 1.0f)));
}
void Voice::setLips(float aperture01) {
    if (!std::isfinite(aperture01)) return;
    write(kChLips, kLipDMax * clampf(aperture01, 0.0f, 1.0f));
}
// the table's pose in its own units, the lips as a sung vowel's: the tuning's
// lipRound reshapes them. a diphthong poses its first half; a phone that is
// not a vowel sets nothing.
void Voice::setVowel(uint8_t phone) {
    if (!isVowel(phone)) return;
    const VowelPose& v = vowel(phone);
    write(kChTongue, v.tongueX, v.tongueD);
    write(kChLips, v.lipD, kLipsVowel);
}
void Voice::setVelum(float open01)          { write(kChVelum, open01); }
void Voice::setConstriction(int slot, float place01, float degree) {
    if (slot < 0 || slot > 1 || !std::isfinite(place01)) return;
    write((uint8_t)(kChSlot0 + slot), clampf(place01, 0.0f, 1.0f), degree);
}
void Voice::clearConstriction(int slot) {
    if (slot < 0 || slot > 1) return;
    write((uint8_t)(kChSlot0 + slot), kSlotClear, 0.0f);
}
void Voice::setGlide(float perSec)          { write(kChGlide, perSec); }
void Voice::setVibrato(float rateHz, float cents, float onsetSec) {
    if (!std::isfinite(rateHz) || !std::isfinite(cents) || !std::isfinite(onsetSec)) return;
    write(kChVibrato, rateHz, cents);
    write(kChVibratoOnset, onsetSec);
}
void Voice::setWobble(float amount01)       { write(kChWobble, amount01); }

void Voice::setTuning(const Tuning& t) {
    tuning_ = clamped(t, fs_);
    tuningPending_ = true;
}

void Voice::setSections(float sections) {
    if (!std::isfinite(sections)) return;
    sections_ = layoutFor(sections).length;
    sectionsPending_ = true;
    whistleShift_ = whistleShiftAt(sections_);
}

void Voice::noteOn(float midi, float vel01) {
    if (!std::isfinite(midi) || !std::isfinite(vel01)) return;
    setPitch(midi);
    setBreath(vel01);
    write(kChNoteOn, 0.0f);
}
void Voice::noteOff() { write(kChBreath, 0.0f); }

// the table's note for this one: shifted by the voice's
// size, folded into the table's range by octaves, then clamped. the pitch
// channel keeps the asked note; the folds open, the velum shuts. whistle()
// ends any words in flight: a commit not yet taken is dropped, and a score
// playing is abandoned and the phoneme readback cleared, written first so it
// lands ahead of the whistle's own targets.
void Voice::whistle(float midi, float breath) {
    if (!std::isfinite(midi) || !std::isfinite(breath)) return;
    player_.withdraw();
    write(kChAbandon, 0.0f);
    const float lo = (float)kWhistleMidiLo, hi = (float)(kWhistleMidiLo + kWhistleCount - 1);
    float m = midi + whistleShift_;
    if (m < lo) m += 12.0f * std::ceil((lo - m) / 12.0f);
    else if (m > hi) m -= 12.0f * std::ceil((m - hi) / 12.0f);
    const WhistlePose p = whistlePose(m);                 // clamps, and a NaN lands on the first row
    setPitch(midi);
    setGlottis(1.0f);
    setVelum(0.0f);
    write(kChTongue, p.tongueX, p.tongueD);
    write(kChLips, p.lipD);
    if (p.slotPlace >= 0.0f) write(kChSlot0, p.slotPlace, p.slotDegree);
    else clearConstriction(0);
    clearConstriction(1);
    setBreath(breath);
}

void Voice::whistleRange(float& lo, float& hi) const {
    lo = (float)kWhistleMidiLo - whistleShift_;
    hi = (float)(kWhistleMidiLo + kWhistleCount - 1) - whistleShift_;
}

// ---- words -------------------------------------------------------------------------------------------
// the compiler runs here, caller-side, into a scratch list; only a good score
// is handed to the player, so a failure disturbs nothing — not the score that
// is running, not one still waiting to be taken.

bool Voice::sing(const Syllable* syls, int n) { return sing(syls, n, scoreBreath_); }

bool Voice::sing(const Syllable* syls, int n, float breath) {
    if (!std::isfinite(breath)) return false;
    for (int i = 0; syls && i < n; ++i)
        if (!std::isfinite(syls[i].midi)) return false;
    Event scratch[kMaxEvents];
    ScoreOptions opt;
    opt.fs = fs_;
    opt.diction = tuning_.diction;
    opt.breath = breath;
    const int count = compileSing(syls, n, opt, scratch, kMaxEvents);
    if (count < 0) return false;
    player_.commit(scratch, count, ++seq_);
    return true;
}

bool Voice::say(const char* phones) {
    Event scratch[kMaxEvents];
    ScoreOptions opt;
    opt.fs = fs_;
    opt.diction = tuning_.diction;
    opt.breath = scoreBreath_;
    const int count = compileSay(phones, midi_, opt, scratch, kMaxEvents);
    if (count < 0) return false;
    player_.commit(scratch, count, ++seq_);
    return true;
}

bool Voice::speak(const char* phones) { return speak(phones, Speech()); }

// the baseline and the breath default to the voice's own: the note held, the
// level say() would use. the voice's vibrato and glide stay as they were set.
bool Voice::speak(const char* phones, const Speech& s) {
    if (!std::isfinite(s.midi) || !std::isfinite(s.range) || !std::isfinite(s.speed) || !std::isfinite(s.breath)) return false;
    Event scratch[kMaxEvents];
    Speech sp = s;
    if (!(sp.midi > 0.0f)) sp.midi = midi_;
    ScoreOptions opt;
    opt.fs = fs_;
    opt.diction = tuning_.diction;
    opt.breath = s.breath > 0.0f ? s.breath : scoreBreath_;
    const int count = compileSpeak(phones, sp, opt, scratch, kMaxEvents);
    if (count < 0) return false;
    player_.commit(scratch, count, ++seq_);
    return true;
}

bool Voice::syllable(const char* phones) {
    Event scratch[kMaxEvents];
    ScoreOptions opt;
    opt.fs = fs_;
    opt.diction = tuning_.diction;
    opt.breathEvents = false;               // live play: the player owns breath and pitch
    opt.pitchEvents = false;
    const int count = compileSyllable(phones, opt, scratch, kMaxEvents);
    if (count < 0) return false;
    player_.commit(scratch, count, ++seq_);
    return true;
}

void Voice::stop() {
    player_.withdraw();
    write(kChStop, 0.0f);
}

// ---- the audio side -------------------------------------------------------------------------------------

void Voice::latch() {
    if (tuningPending_) {
        tuningLatched_ = tuning_;
        tuningPending_ = false;
        retuneDue_ = true;
    }
    if (sectionsPending_) {
        sectionsLatched_ = sections_;
        sectionsPending_ = false;
        resizeDue_ = true;
    }
    const uint32_t dirty = pending_.dirty;
    if (!dirty) return;
    for (int ch = 0; ch < kChCount; ++ch) {
        if (!(dirty & (1u << ch))) continue;
        latched_.a[ch] = pending_.a[ch];
        latched_.b[ch] = pending_.b[ch];
        latched_.seq[ch] = pending_.seq[ch];
    }
    latched_.dirty |= dirty;
    pending_.dirty = 0;
}

// the one place a target is written. live setters and score events both end here.
void Voice::apply(uint8_t ch, float a, float b, float rate) {
    switch (ch) {
        case kChBreath:       body_.setBreath(a, rate); break;
        case kChPitch:
            if (b < 0.0f) pitch_.speakTarget(a, rate);
            else { pitch_.setTarget(a); if (b > 0.0f) pitch_.noteOn(); }
            break;
        case kChGlottis:      body_.setGlottis(a, rate); break;
        case kChTenseness:    body_.setTenseness(a, rate); break;
        case kChTongue:       body_.setTongue(a, b, rate); break;
        case kChLips:         body_.setLips(a, rate, b > 0.0f); break;
        case kChVelum:        body_.setVelum(a, rate); break;
        case kChSlot0:
        case kChSlot1: {
            const int slot = ch - kChSlot0;
            if (a < 0.0f) body_.clearConstriction(slot, rate);
            else body_.setConstriction(slot, a, b, rate);
            break;
        }
        case kChGlide:        pitch_.setGlide(a); break;
        case kChVibrato:      pitch_.setVibrato(a, b); break;
        case kChVibratoOnset: pitch_.setVibratoOnset(a); break;
        case kChWobble:       pitch_.setWobble(a); break;
        case kChNoteOn:       pitch_.noteOn(); break;
        case kChStop:
            player_.abandon();
            body_.clearConstriction(0);
            body_.clearConstriction(1);
            body_.setVelum(0.0f);
            body_.setGlottis(0.0f);
            body_.setBreath(0.0f);
            phoneme_ = ph::none;
            break;
        case kChPhone:        phoneme_ = ph::none; break;  // a live write clears the readback; a score's tag lands after
        case kChAbandon:      player_.abandon(); phoneme_ = ph::none; break;
        default: break;
    }
}

// the live targets waiting in the latch. onlyBefore: just those the caller
// wrote before seq — before it committed the score that starts this block.
// applied oldest write first: some channels write others' targets (a stop
// shuts the breath), so a noteOn called after a stop() must land after it.
void Voice::applyLive(bool onlyBefore, uint32_t seq) {
    for (;;) {
        int pick = -1;
        for (int ch = 0; ch < kChCount; ++ch) {
            if (!(latched_.dirty & (1u << ch))) continue;
            if (onlyBefore && (int32_t)(latched_.seq[ch] - seq) > 0) continue;
            if (pick < 0 || (int32_t)(latched_.seq[ch] - latched_.seq[pick]) < 0) pick = ch;
        }
        if (pick < 0) return;
        apply((uint8_t)pick, latched_.a[pick], latched_.b[pick], 0.0f);
        latched_.dirty &= ~(1u << pick);
    }
}

// another size under the same pose: the body shapes the pose at the new
// length, and the tube stretches what it holds over it.
void Voice::resize(float sections) {
    body_.resize(sections);
    tube_.resize(body_.layout(), body_.diameters(), body_.pose().velumD, body_.ends());
}

// who is singing: every module takes its part of the tuning. block-rate, and
// only when one was set: all the libm this costs stays off the sample path.
void Voice::retune(const Tuning& t) {
    tube_.retune(t);
    noise_.retune(fs_, t);
    body_.retune(t);
    pitch_.retune(t);
    glottis_.setGrowl(t.growl);
    jet_.setGain(t.jetGain);
    noiseFloor_ = t.noiseVoicedFloor;
    noisePulse_ = t.noiseVoicedPulse;
    noiseUnvoiced_ = t.noiseUnvoiced;
}

void Voice::control() {
    if (autoLatch_) latch();
    if (retuneDue_) {                                     // ahead of the targets: they aim at the new rates
        retune(tuningLatched_);
        retuneDue_ = false;
    }
    if (resizeDue_) {                                     // after the tuning: the new shape wears its losses
        resize(sectionsLatched_);
        resizeDue_ = false;
    }

    // ---- targets: the last writer wins, in the caller's own order --------------------------------
    if (player_.commitWaiting()) {
        // written before sing(): the score is the later writer. these go first —
        // before the flip, so a stop() meant for the old score cannot end the new one.
        applyLive(true, player_.waitingSeq());
    }
    if (player_.beginBlock()) {
        // committing clears both slots and rests velum and glottis first
        body_.clearConstriction(0);
        body_.clearConstriction(1);
        body_.setVelum(0.0f);
        body_.setGlottis(0.0f);
    }
    while (const Event* e = player_.next()) {
        apply(e->channel, e->a, e->b, e->rate);
        if (e->phone) phoneme_ = e->phone == ph::clear ? (uint8_t)ph::none : e->phone;
    }
    applyLive(false, 0);                                  // everything else: live hands are later

    // ---- pitch axis, body, anatomy, watchers ----------------------------------------------------
    const float f0 = pitch_.tick(rng_, body_.breath() <= 0.0f);   // out of silence, the note jumps
    body_.tick(tube_);
    glottis_.setTargets(f0, body_.tenseness());
    if (silent_ && body_.breath() > 0.0f) {
        silent_ = false;
        glottis_.reset();                                 // start on a period boundary, at this f0
    }

    // ---- this block's gains, approached per sample ------------------------------------------------
    const BodyFrame& f = body_.frame();
    const int last = tube_.sections() - 1;
    voicing_ = voicingT_; voicingT_ = f.voicing; dVoicing_ = (voicingT_ - voicing_) * kInvBlock;
    asp_ = aspT_;         aspT_ = f.aspiration;  dAsp_ = (aspT_ - asp_) * kInvBlock;
    out_ = outT_;         outT_ = kOutputGain * tuningLatched_.gain;  dOut_ = (outT_ - out_) * kInvBlock;
    for (int j = 0; j < 3; ++j) {
        turb_[j] = turbT_[j]; turbT_[j] = f.turb[j]; dTurb_[j] = (turbT_[j] - turb_[j]) * kInvBlock;
        turbOn_[j] = turb_[j] > 0.0f || turbT_[j] > 0.0f;
        turbA_[j] = f.turbIdx[j] < last ? f.turbIdx[j] : last;
        turbB_[j] = turbA_[j] < last ? turbA_[j] + 1 : last;
        turbFrac_[j] = f.turbFrac[j];
    }
    voicedness_ = f.voicedness;
    jet_.set(f.jetFlow, f.jetDelay, f.jetHalfWidth, f.lipArea);
}

void Voice::renderRun(float* out, int n) {
    const int last = tube_.sections() - 1;
    for (int i = 0; i < n; ++i) {
        voicing_ += dVoicing_;
        asp_ += dAsp_;
        out_ += dOut_;

        float aspNoise, fricNoise;
        noise_.step(rng_, aspNoise, fricNoise);
        const float m = noiseModulator(glottis_.phase(), voicedness_, noiseFloor_, noisePulse_, noiseUnvoiced_);

        // glottis + aspiration
        const float source = glottis_.step() * voicing_ + aspNoise * asp_ * m;

        // turbulence: half into R and half into L of the two sections downstream
        const float fm = fricNoise * m;
        for (int j = 0; j < 3; ++j) {
            turb_[j] += dTurb_[j];
            if (!turbOn_[j]) continue;
            const float v = fm * turb_[j];
            tube_.inject(turbA_[j], v * (1.0f - turbFrac_[j]));
            tube_.inject(turbB_[j], v * turbFrac_[j]);
        }
        body_.stepTransients(tube_);                      // bursts
        tube_.injectFlow(last, jet_.step(tube_.flow(last)));   // the jet, fed by the lips' own flow

        float y = tube_.step(source);
        if (useHalfband_) y = halfband_.process(y);
        out[i] = clampf(y * out_, -1.0f, 1.0f);
    }
}

void Voice::endBlock() {
    player_.endBlock();
    jet_.endBlock();

    // the silence gate: no breath, nothing ringing -> zero the wave state.
    // real silence, and no denormals.
    if (!silent_ && body_.breath() <= 0.0f && body_.breathTarget() <= 0.0f &&
        !body_.transientsAlive() && tube_.energy() < kSilenceEnergy) {
        silent_ = true;
        tube_.reset();
        halfband_.reset();
        noise_.reset();
        jet_.reset();
        body_.killTransients();
    }
    publish();
}

void Voice::publish() {
    const Pose& p = body_.pose();
    state_.breath = body_.breath();
    state_.glottis = body_.glottisOpen();
    state_.tenseness = body_.tenseness();
    state_.tonguePos = (p.tongueX - kTongueXMin) / (kTongueXMax - kTongueXMin);
    state_.tongueHeight = (kTongueDMax - p.tongueD) / (kTongueDMax - kTongueDMin);
    state_.lips = p.lipD / kLipDMax;
    state_.velum = (p.velumD - kVelumShut) / (kVelumOpen - kVelumShut);
    for (int j = 0; j < 2; ++j) {
        state_.slotActive[j] = p.slot[j].active;
        state_.slotPlace[j] = p.slot[j].place;
        state_.slotDegree[j] = p.slot[j].degree;
    }
    state_.midi = pitch_.midi();
    state_.f0 = pitch_.f0();
    state_.vibrato = pitch_.vibrato();
    state_.voicing = body_.frame().voicing;
    state_.hiss = body_.hiss();
    state_.pressure = body_.pressure();
    state_.closed = body_.closed();
    state_.busy = player_.busy();
    state_.phoneme = phoneme_;
    state_.whistle = jet_.whistle();
    state_.whistleHz = jet_.whistleHz();
    const float* d = body_.diameters();
    stateSections_ = body_.sections();
    for (int i = 0; i < stateSections_; ++i) stateDiameters_[i] = d[i];
}

int Voice::diameters(float* out, int max) const {
    const int n = stateSections_ < max ? stateSections_ : max;
    for (int i = 0; i < n; ++i) out[i] = stateDiameters_[i];
    return n;
}

// control runs once per kBlock samples whatever size the caller renders in,
// so motion is the same at every buffer size.
void Voice::render(float* out, int n) {
    int done = 0;
    while (done < n) {
        if (blockPos_ == 0) control();
        int run = kBlock - blockPos_;
        if (run > n - done) run = n - done;
        if (silent_) for (int i = 0; i < run; ++i) out[done + i] = 0.0f;
        else renderRun(out + done, run);
        done += run;
        blockPos_ += run;
        if (blockPos_ >= kBlock) { blockPos_ = 0; endBlock(); }
    }
}

}  // namespace mm
