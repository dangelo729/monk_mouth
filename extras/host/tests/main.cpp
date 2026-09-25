// wind_pipe — tests/main.cpp
// runs every registered test, or only those whose name contains argv[1].
#include <cstring>

#include "wp_test.h"

namespace wptest {
Case*& head() { static Case* h = nullptr; return h; }
int& failures() { static int f = 0; return f; }
int& checks() { static int c = 0; return c; }
}  // namespace wptest

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedCases = 0;
    for (wptest::Case* c = wptest::head(); c; c = c->next) {
        if (filter && !strstr(c->name, filter)) continue;
        const int before = wptest::failures();
        printf("  %s\n", c->name);
        c->fn();
        ++ran;
        if (wptest::failures() != before) ++failedCases;
    }
    printf("%d tests, %d checks, %d failures\n", ran, wptest::checks(), wptest::failures());
    return failedCases ? 1 : 0;
}
