# Host tests for DshotDisplay.
#
#   make                      build and run for the default board
#   make BOARD=BOARD_RP2350_TOUCH_LCD_2_8
#   make both                 build and run for every supported board
#   make build                build only
#   make clean
#
# Needs nothing but a C++17 compiler: the firmware is compiled against the
# stubs in test/stubs/, which mirror the Arduino and Pico SDK signatures.
#
# BOARD is passed as -DBOARD=..., which src/board.h honours ahead of its own
# default. Firmware builds cannot do that -- arduino-cli profiles have no way to
# set a compiler flag -- so they use test/select_board.sh instead.
#
# ui_am32.cpp is deliberately absent from SRCS -- test_ui.cpp #includes it so
# the tests can reach its file-static state. Compiling it here as well would
# be a duplicate-symbol error.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g
WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter
INCLUDES := -Istubs -I../src -I.

BOARD  ?= BOARD_RP2350_TOUCH_LCD_2
BOARDS := BOARD_RP2350_TOUCH_LCD_2 BOARD_RP2350_TOUCH_LCD_2_8

# stubs/core_defines.h carries the -D flags arduino-pico puts on every real
# compile line. Without them the host build is a strictly easier target than the
# device build, and a macro that collides with the core's (BOARD_NAME did)
# passes here while warning on every translation unit there.
DEFINES := -DBOARD=$(BOARD) -include stubs/core_defines.h

SRCS := test_main.cpp test_am32.cpp test_ui.cpp fakes.cpp \
        ../src/gfx.cpp ../src/ui.cpp ../src/am32_eeprom.cpp

BIN := dshot_tests

.PHONY: all build run both clean

all: run

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

clean:
	rm -f $(BIN) *.ppm
