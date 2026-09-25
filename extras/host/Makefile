# wind_pipe — extras/host/Makefile
# the host rig (plan §11). everything but the Teensy wrapper compiles here with
# clang and nothing else.
#
#   make test     build and run every test in tests/
#   make tool     build/wptool, the one CLI
#   make corpus   render every phoneme in three vowel contexts plus the sung
#                 lines in corpus/ into build/corpus/ — for the ear
#   make clean
#
# GNU make 3.81 compatible (the macOS default).

CXX      := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I../../src -I.

BUILD := build

CORE_SRC := $(wildcard ../../src/core/*.cpp)
TEST_SRC := $(wildcard tests/*.cpp)

CORE_OBJ := $(patsubst ../../src/core/%.cpp,$(BUILD)/core_%.o,$(CORE_SRC))
TEST_OBJ := $(patsubst tests/%.cpp,$(BUILD)/test_%.o,$(TEST_SRC))
HOST_OBJ := $(BUILD)/wp_wav.o

.PHONY: all test tool corpus clean

all: $(BUILD)/run_tests $(BUILD)/wptool

test: $(BUILD)/run_tests
	./$(BUILD)/run_tests

tool: $(BUILD)/wptool

corpus: $(BUILD)/wptool
	./$(BUILD)/wptool corpus corpus $(BUILD)/corpus

clean:
	rm -rf $(BUILD)

$(BUILD)/run_tests: $(TEST_OBJ) $(CORE_OBJ) $(HOST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/wptool: $(BUILD)/wptool.o $(CORE_OBJ) $(HOST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

# -MMD -MP emits header dependency files (build/*.d), included at the bottom.
$(BUILD)/core_%.o: ../../src/core/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/test_%.o: tests/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(wildcard $(BUILD)/*.d)
