# Host tests for DshotDisplay.
#
#   make          syntax-check, build and run for the default board
#   make BOARD=BOARD_RP2350_TOUCH_LCD_2_8
#   make both     build and run for every supported board
#   make build    build only
#   make syntax   typecheck the device-only sources, both boards
#   make bbcheck  round-trip a log through the upstream blackbox_decode
#   make previews rebuild the screenshots in docs/ from this run's frames
#   make wiki     rebuild the wiki's screenshots and check its links
#   make coverage line coverage of src/ from an instrumented build in cov/
#   make clean
#
# Needs nothing but a C++17 compiler and the C compiler beside it: the firmware
# is compiled against the stubs in test/stubs/, which mirror the Pico SDK
# signatures.
#
# ui_am32.cpp is deliberately absent from SRCS -- test_ui.cpp #includes it so
# the tests can reach its file-static state. Compiling it here as well would
# be a duplicate-symbol error.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g
CFLAGS   ?= -std=c11 -O1 -g
WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter
INCLUDES := -Istubs -I../src -I.

# BOARD reaches src/board.h as a -D, which its #ifndef honours ahead of its own
# default. Only the pin map and the splash board name differ on the host, but
# the splash is length-checked and the 2.8" name is the longer one.
BOARD  ?= BOARD_RP2350_TOUCH_LCD_2
BOARDS := BOARD_RP2350_TOUCH_LCD_2 BOARD_RP2350_TOUCH_LCD_2_8 BOARD_UNIFIED
# EXTRA_DEFINES lets CI rebuild the suite with the per-board pin macros
# overridden, which is what makes test_settings.cpp's testPinDefaults() more
# than a tautology: a descriptor that went back to carrying literals passes at
# the defaults and fails the moment a macro says something else.
#
#   make BOARD=BOARD_UNIFIED EXTRA_DEFINES=-DDSHOT_PIN_LCD_2=9 run
DEFINES := -DBOARD=$(BOARD) $(EXTRA_DEFINES)

SRCS := test_main.cpp test_am32.cpp test_ui.cpp test_gfx.cpp test_kiss.cpp \
        test_blackbox.cpp test_log_ring.cpp test_settings.cpp \
        test_usb_msc.cpp test_dshot_frame.cpp test_dshot_telem.cpp \
        test_dshot_edt.cpp test_sdio.cpp fakes.cpp \
        ../src/gfx.cpp ../src/ui.cpp ../src/ui_setup.cpp ../src/theme.cpp \
        ../src/ui_input.cpp ../src/ui_widgets.cpp ../src/board_desc.cpp \
        ../src/board_desc_lcd2.cpp ../src/board_desc_lcd2_8.cpp \
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
# touch.cpp is here rather than in SRCS: it dispatches to a real driver, and the
# host suite fakes touchInit/touchPoll outright so that tests can drive presses.
DEVICE_SRCS := ../src/esc_task.cpp ../src/st7789.cpp ../src/cst816.cpp \
               ../src/usb_dev.cpp ../src/usb_msc.cpp ../src/usb_descriptors.c \
               ../src/touch.cpp ../src/board_probe.cpp \
               ../src/cst328.cpp ../src/am32_bl.cpp ../src/sd_log.cpp \
               ../src/pio_uart_rx.cpp \
               ../src/settings_flash.cpp ../src/main.cpp

# The DShot core in src/dshot/ and the SD card protocol layers in src/sdio/
# are C11, and are compiled here as C, by $(CC), into objects the suite links.
# The firmware compiles them as C, so the suite tests the same language: C and
# C++ differ in the implicit conversions this code leans on (integer to enum,
# int to bool), and a C++ build could pass code the C build reads differently.
# Their headers carry extern "C", so the C++ tests call them as the C++
# firmware does. The SDIO hardware layer (sd_pio.c, sd_card_glue.c) drives
# PIO and DMA and is not here.
CSRCS := ../src/dshot/dshot_frame.c ../src/dshot/dshot_telem.c \
         ../src/dshot/dshot_edt.c \
         ../src/sdio/sd_proto.c ../src/sdio/sd_host.c
OBJ_DIR := obj
COBJS := $(addprefix $(OBJ_DIR)/,$(notdir $(CSRCS:.c=.o)))

BIN := dshot_tests

.PHONY: all build run both syntax bbcheck previews wiki coverage clean

all: syntax run bbcheck

build: $(BIN)

# The binary depends on BOARD, which make cannot see in a timestamp. Rebuilding
# unconditionally costs a couple of seconds and removes a whole class of "the
# tests passed for the other board" confusion.
.PHONY: $(BIN)
$(BIN): $(SRCS) $(COBJS) $(wildcard ../src/*.h) $(wildcard ../src/sdio/*.h) $(wildcard *.h)
	$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $(DEFINES) -o $@ $(SRCS) $(COBJS)

$(OBJ_DIR)/%.o: ../src/dshot/%.c ../src/dshot/dshot.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(WARN) -Werror -c -o $@ $<

$(OBJ_DIR)/%.o: ../src/sdio/%.c ../src/sdio/sd_proto.h ../src/sdio/sd_host.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(WARN) -Werror -c -o $@ $<

run: $(BIN)
	./$(BIN)

both:
	@for b in $(BOARDS); do \
		echo "=== $$b ==="; \
		$(MAKE) --no-print-directory BOARD=$$b run || exit 1; \
	done

# Every device source, on every board, plus both settings of the two feature
# switches so the disabled paths cannot rot unnoticed.
# -Werror, matching the firmware build. Without it this target was strictly
# weaker than CI: usb_dev.cpp typechecked here and failed the real build twice
# over -- once on a struct the stub defined too loosely, and once on a partial
# designated initialiser that only -Wextra -Werror objects to. A local check
# that is easier to pass than the one that matters is a check that sends you to
# CI to find out.
syntax:
	@for b in $(BOARDS); do \
		for f in $(DEVICE_SRCS); do \
			$(CXX) $(CXXFLAGS) $(WARN) -Werror $(INCLUDES) -DBOARD=$$b \
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

# Render the wiki's screenshots and check every page still resolves. Same
# relationship to wiki/ that `previews` has to docs/, and the same reason for
# existing: the manual claims its pictures come from the real UI code, and a
# claim nobody can re-run in one command is a claim that goes stale.
wiki: run
	python3 ../tools/make_previews.py --wiki
	python3 ../tools/check_wiki.py

# Line coverage of the src/ files the suite compiles. The instrumented copy
# builds in cov/ at -O0, so each counter maps to one source line, and leaves
# $(BIN) and `make run` unchanged. The run starts from an empty cov/: counters
# from an earlier run or a removed source would otherwise add to this one.
# `python3 ../tools/coverage.py` writes the figure into README.md; --check is
# what CI runs. The published figure is the default BOARD's.
COV_DIR := cov

coverage:
	rm -rf $(COV_DIR) && mkdir -p $(COV_DIR)
	for f in $(CSRCS); do \
		$(CC) -std=c11 -O0 -g --coverage $(WARN) \
			-c -o $(COV_DIR)/$$(basename $$f .c).o $$f || exit 1; \
	done
	$(CXX) -std=gnu++17 -O0 -g --coverage $(WARN) $(INCLUDES) $(DEFINES) \
		-o $(COV_DIR)/$(BIN) $(SRCS) $(addprefix $(COV_DIR)/,$(notdir $(CSRCS:.c=.o)))
	./$(COV_DIR)/$(BIN)
	python3 ../tools/coverage.py --print

clean:
	rm -f $(BIN) gen_blackbox_log *.ppm *.BFL
	rm -rf $(COV_DIR) $(OBJ_DIR)
