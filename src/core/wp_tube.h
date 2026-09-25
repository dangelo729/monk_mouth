// wind_pipe — wp_tube.h  (plan §1)
// a Kelly-Lochbaum waveguide ported from Pink Trombone's Tract.runStep:
// right- and left-going waves, one reflection coefficient per junction, a
// fixed-shape nose branch joined by a three-port junction at x = 17/44.
// one step per output sample; N sections of c/Fs = 0.794 cm each.
//
// the waves are area-weighted pressure (P = A*p), which is why the reference's
// junction transmits (1 - k) into a constriction and its three-port is exact.
#pragma once

#include "wp_config.h"
#include "wp_tuning.h"

namespace wp {

class Tube {
public:
    explicit Tube(int sections = kDefaultSections);

    // the pipe's losses: both end reflections and the wall damping. block-rate.
    void  retune(const Tuning& t);

    int sections() const     { return n_; }
    int noseSections() const { return m_; }
    int noseJunction() const { return ns_; }

    void  reset();                                        // zero the wave state
    // per block: diameters[n] + the velum (nose section 0) -> reflection
    // targets and their per-sample deltas across the next kBlock samples.
    void  setShape(const float* diameters, float velumDiameter);
    void  snap();                                         // jump to the targets (static shapes)
    float energy() const;                                 // sum of squared waves
    float noseArea() const { return velumArea_; }

    // half into R and half into L of one section (noise, transients)
    inline void inject(int section, float amount) {
        R_[section] += 0.5f * amount;
        L_[section] += 0.5f * amount;
    }

    inline float step(float source);                      // one sample

private:
    int n_, m_, ns_;
    float velumArea_ = kVelumShut * kVelumShut;
    float glottalReflection_, lipReflection_, dampOpen_;  // the tuning's

    float R_[kMaxSections], L_[kMaxSections];
    float jR_[kMaxSections + 1], jL_[kMaxSections + 1];
    float k_[kMaxSections + 1], dk_[kMaxSections + 1], kT_[kMaxSections + 1];
    float damp_[kMaxSections];                            // per section: narrow ones lose more

    // three-port at the velum junction: left, right, nose
    float r3_[3], dr3_[3], r3T_[3];

    float noseR_[kMaxNose], noseL_[kMaxNose];
    float noseJR_[kMaxNose + 1], noseJL_[kMaxNose + 1];
    float noseK_[kMaxNose + 1];
};

inline float Tube::step(float source) {
    const int n = n_;
    const float lipReflection = lipReflection_, dampOpen = dampOpen_;

    jR_[0] = L_[0] * glottalReflection_ + source;         // glottal reflection
    jL_[n] = R_[n - 1] * lipReflection;                   // lip reflection

    for (int i = 1; i < n; ++i) {
        k_[i] += dk_[i];                                  // per-sample interpolation
        const float w = k_[i] * (R_[i - 1] + L_[i]);
        jR_[i] = R_[i - 1] - w;
        jL_[i] = L_[i] + w;
    }

    {   // now at junction with nose
        const int i = ns_;
        r3_[0] += dr3_[0]; r3_[1] += dr3_[1]; r3_[2] += dr3_[2];
        const float inL = R_[i - 1], inR = L_[i], inN = noseL_[0];
        jL_[i]     = r3_[0] * inL + (1.0f + r3_[0]) * (inN + inR);
        jR_[i]     = r3_[1] * inR + (1.0f + r3_[1]) * (inL + inN);
        noseJR_[0] = r3_[2] * inN + (1.0f + r3_[2]) * (inR + inL);
    }

    for (int i = 0; i < n; ++i) {
        R_[i] = jR_[i] * damp_[i];
        L_[i] = jL_[i + 1] * damp_[i];
    }

    // nose ladder: same loop and damp, fixed k, its own open end
    const int m = m_;
    noseJL_[m] = noseR_[m - 1] * lipReflection;
    for (int i = 1; i < m; ++i) {
        const float w = noseK_[i] * (noseR_[i - 1] + noseL_[i]);
        noseJR_[i] = noseR_[i - 1] - w;
        noseJL_[i] = noseL_[i] + w;
    }
    for (int i = 0; i < m; ++i) {
        noseR_[i] = noseJR_[i] * dampOpen;
        noseL_[i] = noseJL_[i + 1] * dampOpen;
    }

    return R_[n - 1] + noseR_[m - 1];
}

// a 15-tap halfband lowpass, cutoff exactly Fs/4: removes the mirror image
// the unit-delay-each-way tube produces above Fs/4. four multiplies.
class Halfband {
public:
    Halfband();
    void reset();
    inline float process(float x) {
        z_[pos_ & 15] = x;
        const unsigned c = pos_ - 7;                      // centre tap
        float y = 0.5f * z_[c & 15];
        y += h_[0] * (z_[(c - 1) & 15] + z_[(c + 1) & 15]);
        y += h_[1] * (z_[(c - 3) & 15] + z_[(c + 3) & 15]);
        y += h_[2] * (z_[(c - 5) & 15] + z_[(c + 5) & 15]);
        y += h_[3] * (z_[(c - 7) & 15] + z_[(c + 7) & 15]);
        ++pos_;
        return y;
    }
private:
    float h_[4];
    float z_[16];
    unsigned pos_ = 0;
};

}  // namespace wp
