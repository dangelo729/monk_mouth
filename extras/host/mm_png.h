// monk_mouth — mm_png.h
// a spectrogram as a PNG, with nothing but the standard library: stored
// (uncompressed) deflate blocks, CRC-32, Adler-32. for eyes, when ears are
// not to hand. host only.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "mm_analysis.h"

namespace mm {

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// rgb: width * height * 3 bytes, top row first
inline bool writePng(const char* path, const std::vector<uint8_t>& rgb, int width, int height) {
    std::vector<uint8_t> raw;                                  // each scanline: filter byte 0 + pixels
    raw.reserve((size_t)height * ((size_t)width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * width * 3, rgb.begin() + (size_t)(y + 1) * width * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};                     // zlib header, then stored blocks
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
    for (size_t at = 0; at < raw.size();) {
        const size_t len = raw.size() - at < 65535 ? raw.size() - at : 65535;
        z.push_back(at + len == raw.size() ? 1 : 0);
        z.push_back((uint8_t)(len & 0xFF)); z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 0xFF)); z.push_back((uint8_t)((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + (long)at, raw.begin() + (long)(at + len));
        at += len;
    }
    const uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((uint8_t)(adler >> s));

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> c(type, type + 4);
        c.insert(c.end(), data.begin(), data.end());
        const uint32_t len = (uint32_t)data.size(), crc = crc32(c.data(), c.size());
        for (int s = 24; s >= 0; s -= 8) fputc((int)((len >> s) & 0xFF), f);
        fwrite(c.data(), 1, c.size(), f);
        for (int s = 24; s >= 0; s -= 8) fputc((int)((crc >> s) & 0xFF), f);
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    std::vector<uint8_t> ihdr;
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back((uint8_t)((uint32_t)width >> s));
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back((uint8_t)((uint32_t)height >> s));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});                  // 8-bit RGB
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    const bool ok = !ferror(f);
    fclose(f);
    return ok;
}

// 0..topHz, 70 dB of range, 1024-point Hann frames. a tick every 100 ms along
// the bottom (taller every second) and a faint line every 1 kHz.
inline bool writeSpectrogram(const char* path, const std::vector<float>& x, double fs, double topHz = 8000.0) {
    const int win = 1024, rowsPerBin = 2;
    int hop = (int)(x.size() / 1500) + 1;
    if (hop < 128) hop = 128;
    const int bins = (int)(topHz / (fs / win));
    const int width = (int)(x.size() / (size_t)hop), height = bins * rowsPerBin + 8;
    if (width < 2) return false;
    std::vector<float> db((size_t)width * (size_t)bins);
    float top = -200.0f;
    std::vector<float> frame((size_t)win);
    for (int c = 0; c < width; ++c) {
        for (int i = 0; i < win; ++i) {
            const long at = (long)c * hop + i - win / 2;
            frame[(size_t)i] = at >= 0 && at < (long)x.size() ? x[(size_t)at] : 0.0f;
        }
        const std::vector<double> mag = magnitudeSpectrum(frame.data(), win, win, true);
        for (int k = 0; k < bins; ++k) {
            const float v = (float)toDb(mag[(size_t)k]);
            db[(size_t)c * bins + k] = v;
            if (v > top) top = v;
        }
    }
    std::vector<uint8_t> rgb((size_t)width * height * 3, 0);
    for (int c = 0; c < width; ++c) {
        for (int k = 0; k < bins; ++k) {
            float t = (db[(size_t)c * bins + k] - (top - 70.0f)) / 70.0f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const bool khz = ((int)(k * fs / win) / 1000) != ((int)((k + 1) * fs / win) / 1000);
            const uint8_t r = (uint8_t)(255.0f * (t < 0.5f ? 1.6f * t : 0.8f + 0.4f * (t - 0.5f)));   // black - purple - orange - white
            const uint8_t g = (uint8_t)(255.0f * t * t);
            const uint8_t bl = (uint8_t)(255.0f * (t < 0.5f ? 1.2f * t : 0.6f - 1.2f * (t - 0.5f) + (t > 0.85f ? 4.0f * (t - 0.85f) : 0.0f)));
            for (int rr = 0; rr < rowsPerBin; ++rr) {
                const size_t o = (((size_t)(bins - 1 - k) * rowsPerBin + rr) * width + c) * 3;
                rgb[o] = r; rgb[o + 1] = g; rgb[o + 2] = bl;
                if (khz && rr == 0 && (c & 3) == 0) { rgb[o] = rgb[o + 1] = rgb[o + 2] = 90; }
            }
        }
        const double ms = (double)c * hop * 1000.0 / fs, prev = (double)(c - 1) * hop * 1000.0 / fs;
        const bool tick = (int)(ms / 100.0) != (int)(prev / 100.0), second = (int)(ms / 1000.0) != (int)(prev / 1000.0);
        for (int y = bins * rowsPerBin + (second ? 0 : 4); tick && y < height; ++y) {
            const size_t o = ((size_t)y * width + c) * 3;
            rgb[o] = rgb[o + 1] = rgb[o + 2] = 255;
        }
    }
    return writePng(path, rgb, width, height);
}

}  // namespace mm
