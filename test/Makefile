# Host tests for DshotDisplay.
#
#   make          syntax-check, build and run
#   make build    build only
#   make syntax   typecheck the device-only sources
#   make clean
#
# Needs nothing but a C++17 compiler: the firmware is compiled against the
# stubs in test/stubs/, which mirror the Arduino and Pico SDK signatures.
#
# ui_am32.cpp is deliberately absent from SRCS -- test_ui.cpp #includes it so
# the tests can reach its file-static state. Compiling it here as well would
# be a duplicate-symbol error.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g
WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter
INCLUDES := -Istubs -I../src -I.

SRCS := test_main.cpp test_am32.cpp test_ui.cpp test_gfx.cpp test_kiss.cpp \
        fakes.cpp \
        ../src/gfx.cpp ../src/ui.cpp ../src/am32_eeprom.cpp \
        ../src/kiss_telem.cpp ../src/esc_merge.cpp

# Sources the tests cannot link -- they drive real hardware -- but which still
# have to compile. CI's "Config permutations" job already builds all of src/
# with -Werror, so this is not new coverage; it moves the feedback from a CI
# round-trip to the same `make` you were going to run anyway.
DEVICE_SRCS := ../src/esc_task.cpp ../src/st7789.cpp ../src/cst816.cpp \
               ../src/am32_bl.cpp

BIN := dshot_tests

.PHONY: all build run syntax clean

all: syntax run

build: $(BIN)

$(BIN): $(SRCS) $(wildcard ../src/*.h) $(wildcard *.h)
	$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) -o $@ $(SRCS)

run: $(BIN)
	./$(BIN)

# Both settings of KISS_TELEM_ENABLE, so the disabled path cannot rot unnoticed.
syntax:
	@for f in $(DEVICE_SRCS); do \
		$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) -fsyntax-only $$f || exit 1; \
	done
	@$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) -DKISS_TELEM_ENABLE=0 \
		-fsyntax-only ../src/esc_task.cpp || exit 1
	@echo "device sources typecheck"

clean:
	rm -f $(BIN) *.ppm
