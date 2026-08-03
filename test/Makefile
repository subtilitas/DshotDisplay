# Host tests for DshotDisplay.
#
#   make          build and run
#   make build    build only
#   make clean
#
# Needs nothing but a C++17 compiler: the firmware is compiled against the
# stubs in test/stubs/, which mirror the Arduino and Pico SDK signatures.
#
# ui_am32.cpp, usb_bridge.cpp and bridge_wire.cpp are deliberately absent from
# SRCS: test_ui.cpp and test_bridge.cpp #include them, so the tests can reach
# their file-static state and substitute the one-wire layer. Compiling them here
# as well would be a duplicate-symbol error.
#
# usb_bridge.cpp and bridge_wire.cpp run on different cores on hardware but land
# in one translation unit here, which is why their file-statics are named apart.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g
WARN     := -Wall -Wextra -Wshadow -Wno-unused-parameter
# The shipped firmware has the bridge log off; the tests keep it on so the
# facility stays covered rather than quietly rotting.
DEFINES  := -DAM32_BRIDGE_LOG=1

INCLUDES := -Istubs -I../src -I.

SRCS := test_main.cpp test_am32.cpp test_ui.cpp test_bridge.cpp test_dump.cpp fakes.cpp \
        ../src/gfx.cpp ../src/ui.cpp ../src/am32_eeprom.cpp ../src/bridge_log.cpp

BIN := dshot_tests

.PHONY: all build run clean

all: run

build: $(BIN)

$(BIN): $(SRCS) $(wildcard ../src/*.h) $(wildcard *.h)
	$(CXX) $(CXXFLAGS) $(WARN) $(DEFINES) $(INCLUDES) -o $@ $(SRCS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) *.ppm
