// monk_mouth — tests/test_umbrella.cpp
// MonkMouth.h on an arduino core without the teensy core's AudioStream.h (an
// esp32, a daisy): the wrapper is left out, and the core is all there is.
#define ARDUINO 10819
#include "MonkMouth.h"

#include "mm_test.h"

MM_TEST(umbrella_gives_the_core_without_the_teensy_audio_stream) {
    mm::Voice v;
    float x[mm::kBlock * 2];
    v.noteOn(48.0f, 0.8f);
    v.render(x, mm::kBlock * 2);
    CHECK(v.state().breath > 0.0f);
}
