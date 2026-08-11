# Host tests for DshotDisplay.
#
#   make          syntax-check, build and run
#   make build    build only
#   make syntax   typecheck the device-only sources
#   make bbcheck  round-trip a log through the upstream blackbox_decode
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

SRCS := test_main.cpp test_am32.cpp test_ui.cpp test_gfx.cpp test_kiss.cpp test_blackbox.cpp test_log_ring.cpp \
        fakes.cpp \
        ../src/gfx.cpp ../src/ui.cpp ../src/am32_eeprom.cpp \
        ../src/kiss_telem.cpp ../src/esc_merge.cpp ../src/blackbox_encode.cpp ../src/log_ring.cpp

# Sources the tests cannot link -- they drive real hardware -- but which still
# have to compile. CI's "Config permutations" job already builds all of src/
# with -Werror, so this is not new coverage; it moves the feedback from a CI
# round-trip to the same `make` you were going to run anyway.
DEVICE_SRCS := ../src/esc_task.cpp ../src/st7789.cpp ../src/cst816.cpp \
               ../src/am32_bl.cpp ../src/sd_log.cpp

BIN := dshot_tests

.PHONY: all build run syntax bbcheck clean

all: syntax run bbcheck

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
	@$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) -DSD_LOG_ENABLE=0 \
		-fsyntax-only ../src/sd_log.cpp || exit 1
	@echo "device sources typecheck"

# Round-trip a generated log through the upstream decoder.
#
# The unit tests only prove the encoder agrees with my reading of the format.
# This proves it agrees with the tool people will open the log in -- and those
# came apart once already: the I-frame history-rotation bug passed every unit
# test and produced a file that decoded without complaint, but whose time column
# stalled every 32 frames.
#
# Needs blackbox-tools. Skipped with a notice when absent rather than failing,
# so a bare checkout still runs `make`. CI builds it and runs this for real.
BBDECODE ?= $(shell command -v blackbox_decode 2>/dev/null)

bbcheck:
	@if [ -z "$(BBDECODE)" ]; then \
		echo "bbcheck: blackbox_decode not found, skipping"; \
		echo "  build it from https://github.com/betaflight/blackbox-tools"; \
	else \
		$(CXX) $(CXXFLAGS) $(INCLUDES) -o gen_blackbox_log \
			gen_blackbox_log.cpp ../src/blackbox_encode.cpp -lm && \
		python3 check_blackbox.py "$(BBDECODE)" ./gen_blackbox_log; \
	fi

clean:
	rm -f $(BIN) gen_blackbox_log *.ppm *.BFL
