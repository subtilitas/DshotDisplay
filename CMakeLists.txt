# DshotDisplay — native Pico SDK build.
#
#   cmake -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s .
#   ninja -C build
#
# Produces build/DshotDisplay.uf2.
#
# PICO_PLATFORM selects the core. Only rp2350-arm-s (Cortex-M33) is supported and
# built: rp2350-riscv would work -- the DShot library supports the Hazard3 cores
# -- but nothing here is built or tested for it.
#
# Dependencies are fetched at configure time rather than vendored, so the tree
# stays free of third-party source, and pinned to exact tags so a build here
# matches a build anywhere. That pinning is the whole point: an unpinned
# FetchContent is a lock file that silently rewrites itself.

# 3.18, not 3.13: SOURCE_SUBDIR inside FetchContent_Declare is only honoured by
# FetchContent_MakeAvailable from 3.18 onwards. On 3.13-3.17 it is silently
# ignored, add_subdirectory runs against the dependency's repo root, no targets
# are defined, and the failure surfaces much later as a missing ff.h -- which is
# precisely the confusion the SOURCE_SUBDIR comment below exists to prevent.
cmake_minimum_required(VERSION 3.18)

# SD_DEBUG=ON turns on the FatFs driver's own tracing, which prints to USB
# serial. Off by default: it is noisy and costs flash, but when a card is not
# detected it is the difference between reading the driver's account of what
# happened and guessing.
option(SD_DEBUG "Enable verbose SD driver logging over USB serial" OFF)

# Which board to build for. Unlike the Arduino build -- which had no way to pass
# a -D and so needed select_board.sh to rewrite board.h in place -- CMake can
# just define it, and board.h's #ifndef gets out of the way.
#
#   cmake -B build -DBOARD=BOARD_RP2350_TOUCH_LCD_2_8 ...
#
# The two boards differ in more than pins: different SPI instance for the panel,
# different I2C, a different touch controller, and a different SD interface
# entirely -- SPI on the 2.0", PIO SDIO on the 2.8".
set(BOARD "BOARD_RP2350_TOUCH_LCD_2" CACHE STRING "Target board")
set_property(CACHE BOARD PROPERTY STRINGS
    BOARD_RP2350_TOUCH_LCD_2 BOARD_RP2350_TOUCH_LCD_2_8 BOARD_UNIFIED)

# Which GPIO the ESC signal wire lands on. Empty means "whatever board_pins.h
# defaults to" -- GP4 on the 2.0", GP29 on the 2.8". Set it when the wire is
# somewhere else:
#
#   cmake -B build -DBOARD=BOARD_RP2350_TOUCH_LCD_2_8 -DDSHOT_PIN=28 ...
#
# The 2.8" brings out GP28 (J4 pin 11) and GP29 (J4 pin 12) and nothing else, so
# those are the only two choices there.
set(DSHOT_PIN "" CACHE STRING "GPIO for the ESC signal wire; blank = board default")


# Pinned by commit, not by tag. A tag is a movable reference, and README.md
# calls an unpinned FetchContent "a lock file that silently rewrites itself" --
# which is just as true of one that pins to something upstream can move.
# The comment records which tag each SHA was, so a bump is still readable.
set(PICO_SDK_FETCH_FROM_GIT_TAG bddd20f928ce76142793bef434d4f75f4af6e433)  # 2.1.1
include(pico_sdk_import.cmake)

project(DshotDisplay C CXX ASM)

# An empty CMAKE_BUILD_TYPE is not a default build: it is *no* per-config flags
# at all -- no optimisation, no -g -- and neither CMake nor the pinned SDK
# fills it in. Every image this project ever shipped that way was unoptimised,
# and CI's board-identity assert (which reads the DWARF line table) can never
# pass against it. Release opts in to both: the SDK's set_flags adds -O3 and,
# via PICO_DEBUG_INFO_IN_RELEASE (default ON), keeps the debug info in the ELF.
# The UF2 is unaffected by debug info; only the ELF carries it.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# RP2350 only: the pin maps, the powman-era SD driver choice and the SDIO PIO
# constraints all assume it. Without this a bare `cmake -B build .` takes the
# SDK's defaults -- pico / rp2040 -- and builds something that cannot work, with
# no diagnostic anywhere until it fails on hardware.
#
# After pico_sdk_init() rather than before: PICO_RP2350 is one of the variables
# that call defines, so checking earlier fails for every configuration.
if(NOT PICO_RP2350)
    message(FATAL_ERROR
        "DshotDisplay targets RP2350 only, and this build is configured for "
        "'${PICO_PLATFORM}'. Configure with:\n"
        "  cmake -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s .")
endif()

include(FetchContent)

# Bidirectional DShot over PIO. Distributed as an Arduino library, but the code
# itself is plain Pico SDK -- dshot_common.h keys off ARDUINO being undefined
# and takes the SDK path. GPL-3.0; see the licence note in README.
FetchContent_Declare(pico_bidir_dshot
    GIT_REPOSITORY https://github.com/bastian2001/pico-bidir-dshot.git
    GIT_TAG        b46f54333abc30712d3ed6d0fd29ed1e686d7165  # 1.0.2
)
FetchContent_MakeAvailable(pico_bidir_dshot)

# FatFs over SPI for the microSD slot. Apache-2.0.
#
# The SDIO-SPI repo, not the older SPI-only one: that one's rtc.c includes
# hardware/rtc.h, which does not exist on RP2350 -- the RP2040 RTC was replaced
# by powman. It builds fine for RP2040 and fails outright here.
FetchContent_Declare(no_os_fatfs
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.git
    GIT_TAG        6c644cfc3ab03c161fee2dd7be4877e5b832fa71  # v3.6.2
    # No CMakeLists.txt at the repo root -- the build lives in src/. Without
    # this, FetchContent downloads the source and defines no targets at all,
    # and the failure surfaces much later as a missing ff.h.
    SOURCE_SUBDIR  src
)
FetchContent_MakeAvailable(no_os_fatfs)

add_executable(DshotDisplay
    src/main.cpp
    src/ui.cpp
    src/ui_am32.cpp
    src/ui_setup.cpp
    src/ui_input.cpp
    src/theme.cpp
    src/settings.cpp
    src/settings_flash.cpp
    src/gfx.cpp
    src/st7789.cpp
    src/cst816.cpp
    src/cst328.cpp
    src/touch.cpp
    src/board_desc.cpp
    src/board_desc_lcd2.cpp
    src/board_desc_lcd2_8.cpp
    src/board_probe.cpp
    src/esc_task.cpp
    src/am32_bl.cpp
    src/am32_eeprom.cpp
    src/kiss_telem.cpp
    src/pio_uart_rx.cpp
    src/esc_merge.cpp
    src/blackbox_encode.cpp
    src/log_ring.cpp
    src/sd_log.cpp
    src/sd_hw_config.c
)

# The KISS receiver is a PIO program (see pio_uart_rx.h for why it is not one
# of the two hardware UARTs). pioasm assembles it into the build tree, and this
# also puts that directory on the include path so `#include "pio_uart_rx.pio.h"`
# resolves.
pico_generate_pio_header(DshotDisplay ${CMAKE_CURRENT_LIST_DIR}/src/pio_uart_rx.pio)

target_include_directories(DshotDisplay PRIVATE src)
target_compile_definitions(DshotDisplay PRIVATE BOARD=${BOARD})
# STREQUAL against the empty string, not if(DSHOT_PIN): CMake's if() tests the
# *value*, and "0" is one of its false constants -- so -DDSHOT_PIN=0 took the
# else branch and silently built for the board default instead. GP0 is a real
# pin on both boards, and the symptom of getting it is an ESC that never
# answers with nothing reported anywhere.
if(NOT DSHOT_PIN STREQUAL "")
    target_compile_definitions(DshotDisplay PRIVATE DSHOT_PIN=${DSHOT_PIN})
    message(STATUS "Board: ${BOARD}, DShot default GP${DSHOT_PIN}")
else()
    message(STATUS "Board: ${BOARD}, DShot default from board_pins.h")
endif()

target_link_libraries(DshotDisplay
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_adc
    hardware_clocks
    hardware_flash
    hardware_sync
    hardware_watchdog
    Pico_Bidir_DShot
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)

# Serial telemetry goes over USB CDC, not a UART: the board has no spare pins
# for a debug UART, and the USB port is already there for flashing.
pico_enable_stdio_usb(DshotDisplay 1)
pico_enable_stdio_uart(DshotDisplay 0)

# The board goes in the filename. Both builds used to produce
# build/DshotDisplay.uf2, so the artifact from one board was indistinguishable
# from the other's after the fact -- and the 2.8" image carries a power latch
# the 2.0" does not, so flashing the wrong one is a real outcome. CI and the
# release workflow were already renaming these by hand, which was the tell.
if(BOARD STREQUAL "BOARD_RP2350_TOUCH_LCD_2_8")
    set(BOARD_SLUG "touch-lcd-2.8")
elseif(BOARD STREQUAL "BOARD_UNIFIED")
    set(BOARD_SLUG "unified")
else()
    set(BOARD_SLUG "touch-lcd-2")
endif()
set_target_properties(DshotDisplay PROPERTIES OUTPUT_NAME "DshotDisplay-${BOARD_SLUG}")

# .uf2 for drag-and-drop flashing, plus .elf/.map/.dis for debugging.
pico_add_extra_outputs(DshotDisplay)

# Warnings on our own sources only. The fetched dependencies are not ours to
# keep warning-clean, and -Werror on them would make an upstream bump a build
# break rather than a review.
target_compile_options(DshotDisplay PRIVATE
    -Wall -Wextra -Wshadow -Wno-unused-parameter
)

# STRICT=ON adds -Werror, to this target only.
#
# Worth having as a switch rather than always-on: the only build that uses the
# real arm-none-eabi toolchain had no -Werror at all, so any warning that only
# the cross compiler produces -- the newlib %lu/uint32_t class of thing this
# firmware has already been bitten by once -- was printed by CI and ignored.
# The host permutation job cannot see those; it is a different compiler.
#
# Not the default, because a toolchain bump turning a new warning on should be
# something you read, not something that breaks everyone's build.
option(STRICT "Treat warnings as errors in this project's own sources" OFF)
if(STRICT)
    target_compile_options(DshotDisplay PRIVATE -Werror)
endif()

# Standalone SD bring-up test: nothing but the card, sweeping the SPI clock.
# Built alongside the firmware because the whole value of it is being able to
# flash one, then the other, without changing anything else.
#
# Single-board only: its pin-integrity sweep reads PIN_SD_* from board_pins.h,
# which a unified build deliberately does not define -- "which pins" is exactly
# the question a unified image answers at run time, and a bring-up tool that
# guessed would be worse than one that does not exist. Flash the board-specific
# sdtest instead; the card wiring it exercises is identical.
if(NOT BOARD STREQUAL "BOARD_UNIFIED")
add_executable(sdtest
    tools/sdtest/main.c
    tools/sdtest/touch_stubs.cpp
    src/sd_hw_config.c
    src/board_desc.cpp
    src/board_desc_lcd2.cpp
    src/board_desc_lcd2_8.cpp
)
target_include_directories(sdtest PRIVATE src)
target_compile_definitions(sdtest PRIVATE BOARD=${BOARD})
target_link_libraries(sdtest
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_clocks
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)
target_compile_definitions(sdtest PRIVATE USE_PRINTF=1 USE_DBG_PRINTF=1)
pico_enable_stdio_usb(sdtest 1)
pico_enable_stdio_uart(sdtest 0)
pico_add_extra_outputs(sdtest)
endif()

if(SD_DEBUG)
    # USE_PRINTF routes the driver's messages to stdio; USE_DBG_PRINTF adds the
    # per-command tracing that shows exactly which step of card init failed.
    target_compile_definitions(DshotDisplay PRIVATE USE_PRINTF=1 USE_DBG_PRINTF=1)
endif()
