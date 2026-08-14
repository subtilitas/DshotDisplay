# Host tests for DshotDisplay.
#
#   make          syntax-check, build and run for the default board
#   make BOARD=BOARD_RP2350_TOUCH_LCD_2_8
#   make both     build and run for every supported board
#   make build    build only
#   make syntax   typecheck the device-only sources, both boards
#   make bbcheck  round-trip a log through the upstream blackbox_decode
#   make previews rebuild the screenshots in docs/ from this run's frames
#   make clean
#
# Needs nothing but a C++17 compiler: the firmware is compiled against the
# stubs in test/stubs/, which mirror the Pico SDK signatures.
#
# ui_am32.cpp is deliberately absent from SRCS -- test_ui.cpp #includes it so
# the tests can reach its file-static state. Compiling it here as well would
# be a duplicate-symbol error.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g
WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter
INCLUDES := -Istubs -I../src -I.

# BOARD reaches src/board.h as a -D, which its #ifndef honours ahead of its own
# default. Only the pin map and the splash board name differ on the host, but
# the splash is length-checked and the 2.8" name is the longer one.
BOARD  ?= BOARD_RP2350_TOUCH_LCD_2
BOARDS := BOARD_RP2350_TOUCH_LCD_2 BOARD_RP2350_TOUCH_LCD_2_8
DEFINES := -DBOARD=$(BOARD)

SRCS := test_main.cpp test_am32.cpp test_ui.cpp test_gfx.cpp test_kiss.cpp \
        test_blackbox.cpp test_log_ring.cpp test_settings.cpp fakes.cpp \
        ../src/gfx.cpp ../src/ui.cpp ../src/ui_setup.cpp ../src/theme.cpp \
        ../src/settings.cpp ../src/am32_eeprom.cpp \
        ../src/kiss_telem.cpp ../src/esc_merge.cpp ../src/blackbox_encode.cpp \
        ../src/log_ring.cpp

# Sources the tests cannot link -- they drive real hardware -- but which still
# have to compile. CI's "Config permutations" job already builds all of src/
# with -Werror, so this is not new coverage; it moves the feedback from a CI
# round-trip to the same `make` you were going to run anyway.
#
# The touch drivers matter most here: each compiles to nothing on the board it
# is not for, so building only one board would leave the other untouched.
DEVICE_SRCS := ../src/esc_task.cpp ../src/st7789.cpp ../src/cst816.cpp \
               ../src/cst328.cpp ../src/am32_bl.cpp ../src/sd_log.cpp \
               ../src/settings_flash.cpp ../src/main.cpp

BIN := dshot_tests

.PHONY: all build run both syntax bbcheck previews clean

all: syntax run bbcheck

build: $(BIN)

# The binary depends on BOARD, which make cannot see in a timestamp. Rebuilding
# unconditionally costs a couple of seconds and removes a whole class of "the
# tests passed for the other board" confusion.
.PHONY: $(BIN)
$(BIN): $(SRCS) $(wildcard ../src/*.h) $(wildcard *.h)
	$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $(DEFINES) -o $@ $(SRCS)

run: $(BIN)
	./$(BIN)

both:
	@for b in $(BOARDS); do \
		echo "=== $$b ==="; \
		$(MAKE) --no-print-directory BOARD=$$b run || exit 1; \
	done

# Every device source, on every board, plus both settings of the two feature
# switches so the disabled paths cannot rot unnoticed.
syntax:
	@for b in $(BOARDS); do \
		for f in $(DEVICE_SRCS); do \
			$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) -DBOARD=$$b \
				-fsyntax-only $$f || exit 1; \
		done; \
	done
	@$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $(DEFINES) -DKISS_TELEM_ENABLE=0 \
		-fsyntax-only ../src/esc_task.cpp || exit 1
	@$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $(DEFINES) -DSD_LOG_ENABLE=0 \
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
		echo "  git clone https://github.com/betaflight/blackbox-tools"; \
		echo "  make -C blackbox-tools obj/blackbox_decode   # not plain 'make':"; \
		echo "  the default target also builds blackbox_render, which needs cairo"; \
	else \
		$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -o gen_blackbox_log \
			gen_blackbox_log.cpp ../src/blackbox_encode.cpp -lm && \
		python3 check_blackbox.py "$(BBDECODE)" ./gen_blackbox_log; \
	fi

# Restitch docs/*.png from the frames the run above just wrote. Separate target
# rather than part of `all`, so a test run does not leave the working tree
# dirty -- but one command away, because the manual version of this step is
# what let the published screenshots go stale for several releases.
previews: run
	python3 ../tools/make_previews.py

clean:
	rm -f $(BIN) gen_blackbox_log *.ppm *.BFL
