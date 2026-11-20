// monk_mouth — mm_tube.h
// a Kelly-Lochbaum waveguide ported from Pink Trombone's Tract.runStep:
// right- and left-going waves, one reflection coefficient per junction, a
// fixed-shape nose branch joined by a three-port junction at x = 17/44.
// one step per output sample; sections of c/Fs = 0.794 cm each.
//
// the waves are area-weighted pressure (P = A*p), which is why the reference's
// junction transmits (1 - k) into a constriction and its three-port is exact.
//
// a length is any number of sections, not only a whole one (Layout): whole
// sections carry the shape, and what is left over lives at the three open
// ends — the glottis, the lips, the nostrils — each a fraction of a section
// more of its end section, a round trip read between samples (End).
#pragma once

#include "mm_config.h"
#include "mm_tuning.h"

namespace mm {

// a length as the tube lays it out. the nose joins at 17/44 of the tract and
// runs 28/44 of it, rounded to whole sections at a whole length, and sliding
// from one whole length's to the next's in between — so every part of the
// tube moves continuously with the length, and a whole length lays out the
// same whether the tube was built at it or resized to it.
struct Layout {
    float length = 0.0f;                        // sections, glottis to lips
    int   n = 0;                                // whole sections in the tract
    int   ns = 0;                               // the nose joins between sections ns - 1 and ns
    int   m = 0;                                // whole sections in the nose
    float glottis = 0.0f, lips = 0.0f, nostrils = 0.0f;   // each end's leftover, 0 .. 1 of a section
    float noseLength() const { return (float)m + nostrils; }
    float centre(int i) const { return glottis + (float)i + 0.5f; }   // section i's centre, in sections from the glottis
};
Layout layoutFor(float length);                 // held to kMinSections .. kMaxSections

// an open end's leftover length: a piece of section short of a whole one,
// as wide as the shape is at its place. the wave going in comes back a round
// trip later — 2 x the leftover, in samples — read between the last three
// reflected samples: linear, so it never gains, and at 0 it is the plain
// reflection. whatever enters at the end (the glottis' source) rides the
// reflection. the round trip loses what that much wall would. the piece meets
// its end section through a junction that grows in with the piece: none at no
// length, the whole of a section's at a whole one — so a piece that grows
// whole is exactly the section that appears there. under a round trip shorter
// than a sample, junction and piece are one loop within the sample, solved as
// such. the delay and the junction ramp across a block like a reflection.
struct End {
    float d = 0.0f, dd = 0.0f, aim = 0.0f, next = 0.0f;     // the round trip in samples: now, per sample, this block's aim, the next's
    float k = 0.0f, dk = 0.0f, kAim = 0.0f, kNext = 0.0f;   // the junction into the piece, the same way
    float damp = 1.0f;                                      // the piece's wall loss, per sample
    float in1 = 0.0f, r1 = 0.0f, r2 = 0.0f;                 // what went in a step ago; what came back one and two steps ago

    inline float step(float in, float reflection, float source = 0.0f) {
        float fwd = in, r0 = in * reflection + source, out = r0;
        if (d != 0.0f || dd != 0.0f) {
            d += dd;
            k += dk;
            const float loss = 1.0f + (damp * damp - 1.0f) * 0.5f * d;
            float back;
            if (d < 1.0f) {
                fwd = (in * (1.0f - k) - k * loss * ((1.0f - d) * source + d * r1)) /
                      (1.0f + k * (1.0f - d) * reflection * loss);
                r0 = fwd * reflection + source;
                back = lerpf(r0, r1, d) * loss;
            } else {
                back = lerpf(r1, r2, d - 1.0f) * loss;
                fwd = in - k * (in + back);
                r0 = fwd * reflection + source;
            }
            out = back + k * (in + back);
        }
        r2 = r1; r1 = r0; in1 = fwd;
        return out;
    }

    // what leaves through the end: the end section's wave, `next`, taken
    // across the piece — half the round trip, and that much wall's loss —
    // so the sound comes out where the pipe ends, however long the piece.
    inline float tap(float next) const {
        if (d == 0.0f) return next;
        const float half = 0.5f * d;
        return lerpf(next, in1, half) * lerpf(1.0f, damp, half);
    }

    // land exactly on the last aims, then aim at the next; or jump there
    void land() {
        d = aim; aim = next; dd = (aim - d) * kInvBlock;
        k = kAim; kAim = kNext; dk = (kAim - k) * kInvBlock;
    }
    void snap() { d = aim = next; dd = 0.0f; k = kAim = kNext; dk = 0.0f; }
};

class Tube {
public:
    explicit Tube(float sections = kDefaultSections);

    // the pipe's losses: both end reflections and the wall damping. block-rate.
    void  retune(const Tuning& t);
    // another length, and the shape at it (its n diameters + the velum).
    // the same whole sections under a new length: the ends take up the
    // difference across the next block, and the shape follows at the next
    // setShape. other whole sections: the waves already in the pipe are laid
    // over the new ones, stretched by the ratio of the lengths, and the shape
    // lands at once. block-rate.
    void  resize(const Layout& at, const float* diameters, float velumDiameter, const float* ends = nullptr);

    const Layout& layout() const { return at_; }
    int sections() const     { return n_; }
    int noseSections() const { return m_; }
    int noseJunction() const { return ns_; }

    void  reset();                                        // zero the wave state
    // per block: diameters[n] + the velum (nose section 0) -> reflection
    // targets and their per-sample deltas across the next kBlock samples.
    // ends: the glottis' and the lips' pieces' diameters (shapeTract's);
    // without them each piece is as wide as its end section.
    void  setShape(const float* diameters, float velumDiameter, const float* ends = nullptr);
    void  snap();                                         // jump to the targets (static shapes)
    float energy() const;                                 // sum of squared waves
    float noseArea() const { return velumArea_; }

    // half into R and half into L of one section (noise, transients)
    inline void inject(int section, float amount) {
        R_[section] += 0.5f * amount;
        L_[section] += 0.5f * amount;
    }

    // half into R and minus half into L of one section: adds through-flow
    // (R - L) and leaves the pressure (R + L). that is the wave pair a pressure
    // difference across the section makes: the jet drive's dipole. inject()
    // is the monopole, a volume source, which the open end shorts.
    inline void injectFlow(int section, float amount) {
        R_[section] += 0.5f * amount;
        L_[section] -= 0.5f * amount;
    }

    // in area-weighted waves the difference is volume velocity, independent of area
    float flow(int i) const { return R_[i] - L_[i]; }

    inline float step(float source);                      // one sample

private:
    void setLayout(const Layout& at);
    void remap(const Layout& to);

    Layout at_;
    int n_, m_, ns_;
    End glottisEnd_, lipsEnd_, nostrilsEnd_;
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

    jR_[0] = glottisEnd_.step(L_[0], glottalReflection_, source);   // glottal reflection
    jL_[n] = lipsEnd_.step(R_[n - 1], lipReflection);               // lip reflection

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
    noseJL_[m] = nostrilsEnd_.step(noseR_[m - 1], lipReflection);
    for (int i = 1; i < m; ++i) {
        const float w = noseK_[i] * (noseR_[i - 1] + noseL_[i]);
        noseJR_[i] = noseR_[i - 1] - w;
        noseJL_[i] = noseL_[i] + w;
    }
    for (int i = 0; i < m; ++i) {
        noseR_[i] = noseJR_[i] * dampOpen;
        noseL_[i] = noseJL_[i + 1] * dampOpen;
    }

    return lipsEnd_.tap(R_[n - 1]) + nostrilsEnd_.tap(noseR_[m - 1]);
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

}  // namespace mm
