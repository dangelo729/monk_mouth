// monk_mouth — mm_wav.h
// a WAV writer: mono, 16-bit PCM. host only.
#pragma once

#include <vector>

namespace mm {

// samples in [-1, 1]; clamps. returns false if the file could not be written.
bool writeWav(const char* path, const float* samples, int count, int sampleRate);
inline bool writeWav(const char* path, const std::vector<float>& samples, int sampleRate) {
    return writeWav(path, samples.data(), (int)samples.size(), sampleRate);
}

}  // namespace mm
