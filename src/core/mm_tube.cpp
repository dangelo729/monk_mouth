// monk_mouth — mm_tube.cpp  (plan §1)
#include "mm_tube.h"

namespace mm {

namespace {

// a wave's value at a place between the samples it holds, one section apart:
// linear, and past either end the end's own.
float at(const float* w, int count, float x) {
    if (!(x > 0.0f)) return w[0];
    const int i = (int)x;
    return i >= count - 1 ? w[count - 1] : lerpf(w[i], w[i + 1], x - (float)i);
}

}  // namespace

Layout layoutFor(float length) {
    const float lo = (float)kMinSections, hi = (float)kMaxSections;
    Layout at;
    at.length = !(length >= lo) ? lo : (length > hi ? hi : length);   // a NaN lands on the smallest
    const int whole = (int)at.length;
    const int up = whole < kMaxSections ? whole + 1 : whole;
    const float t = at.length - (float)whole;
    const float join = lerpf((float)std::lround(whole * 17.0 / 44.0), (float)std::lround(up * 17.0 / 44.0), t);
    const float nose = lerpf((float)std::lround(whole * 28.0 / 44.0), (float)std::lround(up * 28.0 / 44.0), t);
    at.ns = (int)join;
    at.glottis = join - (float)at.ns;
    const float oral = at.length - join;
    const int front = (int)oral;
    at.lips = oral - (float)front;
    at.n = at.ns + front;
    at.m = (int)nose;
    at.nostrils = nose - (float)at.m;
    return at;
}

Tube::Tube(float sections) {
    setLayout(layoutFor(sections));

    retune(Tuning());
    for (int i = 0; i <= kMaxSections; ++i) k_[i] = dk_[i] = kT_[i] = 0.0f;
    for (int i = 0; i < kMaxSections; ++i) damp_[i] = dampOpen_;
    for (int j = 0; j < 3; ++j) r3_[j] = dr3_[j] = r3T_[j] = 0.0f;

    // start as the neutral tube, velum shut
    float d[kMaxSections];
    for (int i = 0; i < n_; ++i) d[i] = 1.5f;
    setShape(d, kVelumShut);
    snap();
    reset();
}

// a layout, and what follows from it: the whole sections, where the nose
// joins, the ends' next round trips, and the reference's fixed nose profile
// over the nose's own length, the nostril end's piece at its own place.
// section 0 of the nose is the velum; the ladder's reflections are computed
// with it open, exactly as the reference does.
void Tube::setLayout(const Layout& at) {
    at_ = at;
    n_ = at.n;
    m_ = at.m;
    ns_ = at.ns;
    glottisEnd_.next = 2.0f * at.glottis;
    lipsEnd_.next = 2.0f * at.lips;
    nostrilsEnd_.next = 2.0f * at.nostrils;

    const float length = at.noseLength();
    float noseA[kMaxNose + 1];
    for (int i = 0; i <= m_; ++i) {
        const float d = 2.0f * ((float)i / length);
        float diameter = d < 1.0f ? 0.4f + 1.6f * d : 0.5f + 1.5f * (2.0f - d);
        if (diameter > 1.9f) diameter = 1.9f;
        noseA[i] = diameter * diameter;
    }
    noseK_[0] = 0.0f;
    for (int i = 1; i < m_; ++i)
        noseK_[i] = (noseA[i - 1] - noseA[i]) / (noseA[i - 1] + noseA[i]);
    noseK_[m_] = 0.0f;
    nostrilsEnd_.kNext = at.nostrils * (noseA[m_ - 1] - noseA[m_]) / (noseA[m_ - 1] + noseA[m_]);
}

void Tube::resize(const Layout& at, const float* diameters, float velumDiameter, const float* ends) {
    if (at.n == n_ && at.ns == ns_ && at.m == m_) {
        setLayout(at);
        return;
    }
    remap(at);
    setLayout(at);
    setShape(diameters, velumDiameter, ends);
    snap();
}

// the waves under another layout. each end's leftover counts as one section
// more of the pipe — the wave that went in a step ago going out, the one that
// came back two steps ago coming in, which is exactly a whole section's worth
// — laid along the pipe at its place. every place is stretched by the ratio
// of the lengths and read back at the new layout's places, so a change of
// whole sections under the same length moves nothing, and a new length
// stretches what is ringing over it. the sections' own loss is left out: it
// is a part in a thousand.
void Tube::remap(const Layout& to) {
    // the tract: the glottis end, sections 0 .. n - 1, the lip end, one section
    // apart, the first half a section short of section 0's inner face
    float fromR[kMaxSections + 2], fromL[kMaxSections + 2];
    fromR[0] = glottisEnd_.r2;
    fromL[0] = glottisEnd_.in1;
    for (int i = 0; i < n_; ++i) { fromR[i + 1] = R_[i]; fromL[i + 1] = L_[i]; }
    fromR[n_ + 1] = lipsEnd_.in1;
    fromL[n_ + 1] = lipsEnd_.r2;

    const float scale = at_.length / to.length;          // a new place, in old sections
    float toR[kMaxSections + 2], toL[kMaxSections + 2];
    for (int j = 0; j < to.n + 2; ++j) {
        const float x = (to.glottis - 0.5f + (float)j) * scale - (at_.glottis - 0.5f);
        toR[j] = at(fromR, n_ + 2, x);
        toL[j] = at(fromL, n_ + 2, x);
    }
    for (int i = 0; i < to.n; ++i) { R_[i] = toR[i + 1]; L_[i] = toL[i + 1]; }
    glottisEnd_.in1 = toL[0];
    glottisEnd_.r2 = toR[0];
    glottisEnd_.r1 = glottalReflection_ * toL[0];
    lipsEnd_.in1 = toR[to.n + 1];
    lipsEnd_.r2 = toL[to.n + 1];
    lipsEnd_.r1 = lipReflection_ * toR[to.n + 1];

    // the nose, from the junction: sections 0 .. m - 1, then the nostril end
    float fromNR[kMaxNose + 1], fromNL[kMaxNose + 1];
    for (int i = 0; i < m_; ++i) { fromNR[i] = noseR_[i]; fromNL[i] = noseL_[i]; }
    fromNR[m_] = nostrilsEnd_.in1;
    fromNL[m_] = nostrilsEnd_.r2;
    const float noseScale = at_.noseLength() / to.noseLength();
    float toNR[kMaxNose + 1], toNL[kMaxNose + 1];
    for (int j = 0; j < to.m + 1; ++j) {
        const float x = ((float)j + 0.5f) * noseScale - 0.5f;
        toNR[j] = at(fromNR, m_ + 1, x);
        toNL[j] = at(fromNL, m_ + 1, x);
    }
    for (int i = 0; i < to.m; ++i) { noseR_[i] = toNR[i]; noseL_[i] = toNL[i]; }
    nostrilsEnd_.in1 = toNR[to.m];
    nostrilsEnd_.r2 = toNL[to.m];
    nostrilsEnd_.r1 = lipReflection_ * toNR[to.m];
}

// the sections' own damping follows at the next setShape, a block away at most.
void Tube::retune(const Tuning& t) {
    glottalReflection_ = t.glottalReflection;
    lipReflection_ = t.lipReflection;
    dampOpen_ = t.damp;
}

void Tube::reset() {
    for (int i = 0; i < kMaxSections; ++i) R_[i] = L_[i] = 0.0f;
    for (int i = 0; i <= kMaxSections; ++i) jR_[i] = jL_[i] = 0.0f;
    for (int i = 0; i < kMaxNose; ++i) noseR_[i] = noseL_[i] = 0.0f;
    for (int i = 0; i <= kMaxNose; ++i) noseJR_[i] = noseJL_[i] = 0.0f;
    End* const ends[] = {&glottisEnd_, &lipsEnd_, &nostrilsEnd_};
    for (End* e : ends) e->in1 = e->r1 = e->r2 = 0.0f;
}

void Tube::setShape(const float* diameters, float velumDiameter, const float* ends) {
    float A[kMaxSections];
    for (int i = 0; i < n_; ++i) {
        const float d = diameters[i] > 0.0f ? diameters[i] : 0.0f;
        const float a = d * d;                            // ignoring pi etc.
        A[i] = a > kMinArea ? a : kMinArea;
        damp_[i] = lerpf(kDampClosed, dampOpen_, clampf(a / kNarrowArea, 0.0f, 1.0f));
    }

    // the ends' pieces, each through a junction as strong as the piece is long
    {
        float a[2] = {A[0], A[n_ - 1]};
        float loss[2] = {damp_[0], damp_[n_ - 1]};
        if (ends) {
            for (int j = 0; j < 2; ++j) {
                const float d = ends[j] > 0.0f ? ends[j] : 0.0f;
                a[j] = d * d > kMinArea ? d * d : kMinArea;
                loss[j] = lerpf(kDampClosed, dampOpen_, clampf(d * d / kNarrowArea, 0.0f, 1.0f));
            }
        }
        glottisEnd_.kNext = at_.glottis * (A[0] - a[0]) / (A[0] + a[0]);
        lipsEnd_.kNext = at_.lips * (A[n_ - 1] - a[1]) / (A[n_ - 1] + a[1]);
        glottisEnd_.damp = loss[0];
        lipsEnd_.damp = loss[1];
        nostrilsEnd_.damp = dampOpen_;
    }

    // land exactly on the last targets, then aim at the new ones
    for (int i = 1; i < n_; ++i) {
        float k = (A[i - 1] - A[i]) / (A[i - 1] + A[i]);
        k = clampf(k, -kMaxReflection, kMaxReflection);
        k_[i] = kT_[i];
        kT_[i] = k;
        dk_[i] = (k - k_[i]) * kInvBlock;
    }

    const float vd = velumDiameter > 0.0f ? velumDiameter : 0.0f;
    velumArea_ = vd * vd;
    const float an = velumArea_ > kMinArea ? velumArea_ : kMinArea;
    const float sum = A[ns_ - 1] + A[ns_] + an;
    const float r[3] = { (2.0f * A[ns_ - 1] - sum) / sum,
                         (2.0f * A[ns_] - sum) / sum,
                         (2.0f * an - sum) / sum };
    for (int j = 0; j < 3; ++j) {
        r3_[j] = r3T_[j];
        r3T_[j] = r[j];
        dr3_[j] = (r[j] - r3_[j]) * kInvBlock;
    }

    glottisEnd_.land();
    lipsEnd_.land();
    nostrilsEnd_.land();
}

void Tube::snap() {
    for (int i = 1; i < n_; ++i) { k_[i] = kT_[i]; dk_[i] = 0.0f; }
    for (int j = 0; j < 3; ++j) { r3_[j] = r3T_[j]; dr3_[j] = 0.0f; }
    glottisEnd_.snap();
    lipsEnd_.snap();
    nostrilsEnd_.snap();
}

float Tube::energy() const {
    float e = 0.0f;
    for (int i = 0; i < n_; ++i) e += R_[i] * R_[i] + L_[i] * L_[i];
    for (int i = 0; i < m_; ++i) e += noseR_[i] * noseR_[i] + noseL_[i] * noseL_[i];
    return e;
}

Halfband::Halfband() {
    // windowed-sinc halfband: even taps are zero, the centre is 0.5, and the
    // odd taps on each side sum to 0.25 so DC gain is exactly 1.
    const float pi = 3.14159265358979f;
    float sum = 0.0f;
    for (int j = 0; j < 4; ++j) {
        const int k = 2 * j + 1;
        const float sinc = ((j & 1) ? -1.0f : 1.0f) / (pi * (float)k);
        const float window = 0.54f + 0.46f * std::cos(pi * (float)k / 7.0f);
        h_[j] = sinc * window;
        sum += h_[j];
    }
    for (int j = 0; j < 4; ++j) h_[j] *= 0.25f / sum;
    reset();
}

void Halfband::reset() {
    for (int i = 0; i < 16; ++i) z_[i] = 0.0f;
    pos_ = 0;
}

}  // namespace mm
