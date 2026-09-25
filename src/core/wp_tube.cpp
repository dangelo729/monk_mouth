// wind_pipe — wp_tube.cpp  (plan §1)
#include "wp_tube.h"

namespace wp {

Tube::Tube(int sections) {
    n_ = sections < kMinSections ? kMinSections : (sections > kMaxSections ? kMaxSections : sections);
    m_ = (int)std::lround(n_ * 28.0 / 44.0);
    ns_ = (int)std::lround(n_ * 17.0 / 44.0);

    // the reference's fixed nose profile. section 0 is the velum; the ladder's
    // reflections are computed once with it open, exactly as the reference does.
    float noseA[kMaxNose];
    for (int i = 0; i < m_; ++i) {
        const float d = 2.0f * ((float)i / (float)m_);
        float diameter = d < 1.0f ? 0.4f + 1.6f * d : 0.5f + 1.5f * (2.0f - d);
        if (diameter > 1.9f) diameter = 1.9f;
        noseA[i] = diameter * diameter;
    }
    noseK_[0] = 0.0f;
    for (int i = 1; i < m_; ++i)
        noseK_[i] = (noseA[i - 1] - noseA[i]) / (noseA[i - 1] + noseA[i]);
    noseK_[m_] = 0.0f;

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
}

void Tube::setShape(const float* diameters, float velumDiameter) {
    float A[kMaxSections];
    for (int i = 0; i < n_; ++i) {
        const float d = diameters[i] > 0.0f ? diameters[i] : 0.0f;
        const float a = d * d;                            // ignoring pi etc.
        A[i] = a > kMinArea ? a : kMinArea;
        damp_[i] = lerpf(kDampClosed, dampOpen_, clampf(a / kNarrowArea, 0.0f, 1.0f));
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
}

void Tube::snap() {
    for (int i = 1; i < n_; ++i) { k_[i] = kT_[i]; dk_[i] = 0.0f; }
    for (int j = 0; j < 3; ++j) { r3_[j] = r3T_[j]; dr3_[j] = 0.0f; }
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

}  // namespace wp
