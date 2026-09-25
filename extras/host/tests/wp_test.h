// wind_pipe — wp_test.h
// plain-assert tests: a registry, CHECK, and nothing else.
#pragma once

#include <cstdio>

namespace wptest {

struct Case { const char* name; void (*fn)(); Case* next; };
Case*& head();
int& failures();
int& checks();

struct Register {
    Register(const char* name, void (*fn)()) {
        static Case pool[256];
        static int used = 0;
        Case& c = pool[used++];
        c.name = name; c.fn = fn; c.next = nullptr;
        Case** tail = &head();
        while (*tail) tail = &(*tail)->next;
        *tail = &c;
    }
};

}  // namespace wptest

// the sizes the acoustic claims are tested over. a voice takes any size from
// wp::kMinSections to wp::kMaxSections; these are the ones the analysis sees
// whole: its formant search runs 120 .. 5000 Hz, and past either end of this
// range a vowel's F1 or F2 scales out of that window.
constexpr int kTestedSizeMin = 14, kTestedSizeMax = 48;

#define WP_TEST(name)                                              \
    static void name();                                            \
    static wptest::Register wp_reg_##name(#name, name);            \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++wptest::checks();                                                           \
        if (!(cond)) {                                                                \
            ++wptest::failures();                                                     \
            printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                             \
    } while (0)

// CHECK with a printf-style explanation of the numbers involved
#define CHECK_MSG(cond, ...)                                                          \
    do {                                                                              \
        ++wptest::checks();                                                           \
        if (!(cond)) {                                                                \
            ++wptest::failures();                                                     \
            printf("    FAIL %s:%d  %s  -- ", __FILE__, __LINE__, #cond);             \
            printf(__VA_ARGS__);                                                      \
            printf("\n");                                                             \
        }                                                                             \
    } while (0)
