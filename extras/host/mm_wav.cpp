// monk_mouth — mm_wav.cpp
#include "mm_wav.h"

#include <cstdint>
#include <cstdio>

namespace mm {

static void put32(FILE* f, uint32_t v) { for (int i = 0; i < 4; ++i) fputc((int)((v >> (8 * i)) & 0xFF), f); }
static void put16(FILE* f, uint16_t v) { for (int i = 0; i < 2; ++i) fputc((int)((v >> (8 * i)) & 0xFF), f); }

bool writeWav(const char* path, const float* samples, int count, int sampleRate) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t dataBytes = (uint32_t)count * 2u;
    fwrite("RIFF", 1, 4, f); put32(f, 36u + dataBytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, (uint32_t)sampleRate); put32(f, (uint32_t)sampleRate * 2u);
    put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, dataBytes);
    for (int i = 0; i < count; ++i) {
        float x = samples[i];
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        put16(f, (uint16_t)(int16_t)(x * 32767.0f));
    }
    const bool ok = !ferror(f);
    fclose(f);
    return ok;
}

}  // namespace mm
