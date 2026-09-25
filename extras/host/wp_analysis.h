// wind_pipe — wp_analysis.h
// the host's measuring instruments: a radix-2 FFT, a formant picker that
// feeds a unit impulse through a static tube, and a few spectrum helpers.
// no source, no noise, no external analysis tools (plan §11). host only.
#pragma once

#include <cmath>
#include <vector>

#include "core/wp_tube.h"

namespace wp {

// in-place radix-2 FFT. n must be a power of two.
inline void fft(std::vector<double>& re, std::vector<double>& im) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (size_t k = 0; k < len / 2; ++k) {
                const size_t a = i + k, b = i + k + len / 2;
                const double tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

// magnitude spectrum of x (zero-padded or truncated to fftSize), bins 0..fftSize/2.
// hann = true windows the signal first (for noise and steady tones).
inline std::vector<double> magnitudeSpectrum(const float* x, int count, int fftSize, bool hann = false) {
    std::vector<double> re((size_t)fftSize, 0.0), im((size_t)fftSize, 0.0);
    const int n = count < fftSize ? count : fftSize;
    for (int i = 0; i < n; ++i) {
        const double w = hann ? 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1)) : 1.0;
        re[(size_t)i] = x[i] * w;
    }
    fft(re, im);
    std::vector<double> mag((size_t)fftSize / 2 + 1);
    for (size_t i = 0; i < mag.size(); ++i) mag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]);
    return mag;
}

// the impulse response of a static tube shape, velum as given, through a pipe
// with the tuning's losses.
inline std::vector<float> impulseResponse(int sections, const float* diameters, float velumDiameter, int count,
                                          const Tuning& tuning = Tuning()) {
    Tube tube(sections);
    tube.retune(tuning);
    tube.setShape(diameters, velumDiameter);
    tube.snap();
    tube.reset();
    std::vector<float> out((size_t)count);
    for (int i = 0; i < count; ++i) out[(size_t)i] = tube.step(i == 0 ? 1.0f : 0.0f);
    return out;
}

// peak-pick the spectrum between loHz and hiHz; parabolic interpolation on
// log magnitude. returns the peak frequencies in ascending order.
inline std::vector<double> spectralPeaks(const std::vector<double>& mag, double fs, double loHz, double hiHz) {
    std::vector<double> peaks;
    const size_t fftSize = (mag.size() - 1) * 2;
    const double binHz = fs / (double)fftSize;
    const size_t lo = (size_t)std::ceil(loHz / binHz), hi = (size_t)std::floor(hiHz / binHz);
    for (size_t i = lo > 1 ? lo : 1; i <= hi && i + 1 < mag.size(); ++i) {
        if (mag[i] > mag[i - 1] && mag[i] >= mag[i + 1]) {
            const double a = std::log(mag[i - 1] + 1e-30), b = std::log(mag[i] + 1e-30), c = std::log(mag[i + 1] + 1e-30);
            const double denom = a - 2.0 * b + c;
            const double off = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
            peaks.push_back(((double)i + off) * binHz);
        }
    }
    return peaks;
}

// F1..Fn of a static shape: unit impulse through the tube, peak-pick the FFT.
inline std::vector<double> formants(int sections, const float* diameters, float velumDiameter,
                                    double fs = kDefaultFs, double hiHz = 5000.0, const Tuning& tuning = Tuning()) {
    const std::vector<float> ir = impulseResponse(sections, diameters, velumDiameter, 4096, tuning);
    const std::vector<double> mag = magnitudeSpectrum(ir.data(), (int)ir.size(), 8192);
    return spectralPeaks(mag, fs, 120.0, hiHz);
}

// the resonances the jet sees (whistle plan §7.1): a unit impulse injected
// into the last section, the tube's output read there. the driving point at
// the lips, where peak-picking the throat-end response finds every formant.
inline std::vector<float> lipImpulseResponse(int sections, const float* diameters, float velumDiameter, int count,
                                             const Tuning& tuning = Tuning()) {
    Tube tube(sections);
    tube.retune(tuning);
    tube.setShape(diameters, velumDiameter);
    tube.snap();
    tube.reset();
    tube.inject(tube.sections() - 1, 1.0f);
    std::vector<float> out((size_t)count);
    for (int i = 0; i < count; ++i) out[(size_t)i] = tube.step(0.0f);
    return out;
}

// the lip response's peaks over the whistle band, 300 .. 6000 Hz
inline std::vector<double> whistleModes(int sections, const float* diameters, float velumDiameter,
                                        const Tuning& tuning = Tuning(), double fs = kDefaultFs) {
    const std::vector<float> ir = lipImpulseResponse(sections, diameters, velumDiameter, 4096, tuning);
    const std::vector<double> mag = magnitudeSpectrum(ir.data(), (int)ir.size(), 8192);
    return spectralPeaks(mag, fs, 300.0, 6000.0);
}

// the strongest bin within +-width (a fraction) of hz, as a magnitude
inline double peakNear(const std::vector<double>& mag, double fs, double hz, double width) {
    const size_t fftSize = (mag.size() - 1) * 2;
    const double binHz = fs / (double)fftSize;
    size_t lo = (size_t)std::floor(hz * (1.0 - width) / binHz), hi = (size_t)std::ceil(hz * (1.0 + width) / binHz);
    if (hi >= mag.size()) hi = mag.size() - 1;
    double best = 0.0;
    for (size_t i = lo; i <= hi; ++i) if (mag[i] > best) best = mag[i];
    return best;
}

inline double rms(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return n > 0 ? std::sqrt(s / n) : 0.0;
}

inline double toDb(double x) { return 20.0 * std::log10(x + 1e-30); }

// power-weighted mean frequency of a stretch of signal
inline double spectralCentroid(const float* x, int n, double fs) {
    int fftSize = 1;
    while (fftSize < n) fftSize <<= 1;
    const std::vector<double> mag = magnitudeSpectrum(x, n, fftSize, true);
    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < mag.size(); ++i) {
        const double p = mag[i] * mag[i];
        num += p * (double)i * fs / (double)fftSize;
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

// normalized autocorrelation peak in a lag range: near 1 = periodic, near 0 = noise
inline double periodicity(const float* x, int n, int minLag, int maxLag) {
    double e = 0.0;
    for (int i = 0; i < n; ++i) e += (double)x[i] * x[i];
    if (e <= 0.0) return 0.0;
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag && lag < n; ++lag) {
        double s = 0.0, ea = 0.0, eb = 0.0;
        for (int i = 0; i + lag < n; ++i) { s += (double)x[i] * x[i + lag]; ea += (double)x[i] * x[i]; eb += (double)x[i + lag] * x[i + lag]; }
        const double r = s / (std::sqrt(ea * eb) + 1e-30);
        if (r > best) best = r;
    }
    return best;
}

}  // namespace wp
