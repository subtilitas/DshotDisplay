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

cmake_minimum_required(VERSION 3.13)

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
    BOARD_RP2350_TOUCH_LCD_2 BOARD_RP2350_TOUCH_LCD_2_8)

# Which GPIO the ESC signal wire lands on. Empty means "whatever board_pins.h
# defaults to" -- GP4 on the 2.0", GP29 on the 2.8". Set it when the wire is
# somewhere else:
#
#   cmake -B build -DBOARD=BOARD_RP2350_TOUCH_LCD_2_8 -DDSHOT_PIN=28 ...
#
# The 2.8" brings out GP28 (J4 pin 11) and GP29 (J4 pin 12) and nothing else, so
# those are the only two choices there.
set(DSHOT_PIN "" CACHE STRING "GPIO for the ESC signal wire; blank = board default")


set(PICO_SDK_FETCH_FROM_GIT_TAG 2.1.1)
include(pico_sdk_import.cmake)

project(DshotDisplay C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

include(FetchContent)

# Bidirectional DShot over PIO. Distributed as an Arduino library, but the code
# itself is plain Pico SDK -- dshot_common.h keys off ARDUINO being undefined
# and takes the SDK path. GPL-3.0; see the licence note in README.
FetchContent_Declare(pico_bidir_dshot
    GIT_REPOSITORY https://github.com/bastian2001/pico-bidir-dshot.git
    GIT_TAG        1.0.2
)
FetchContent_MakeAvailable(pico_bidir_dshot)

# FatFs over SPI for the microSD slot. Apache-2.0.
#
# The SDIO-SPI repo, not the older SPI-only one: that one's rtc.c includes
# hardware/rtc.h, which does not exist on RP2350 -- the RP2040 RTC was replaced
# by powman. It builds fine for RP2040 and fails outright here.
FetchContent_Declare(no_os_fatfs
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.git
    GIT_TAG        v3.6.2
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
    src/gfx.cpp
    src/st7789.cpp
    src/cst816.cpp
    src/cst328.cpp
    src/esc_task.cpp
    src/am32_bl.cpp
    src/am32_eeprom.cpp
    src/kiss_telem.cpp
    src/esc_merge.cpp
    src/blackbox_encode.cpp
    src/log_ring.cpp
    src/sd_log.cpp
    src/sd_hw_config.c
)

target_include_directories(DshotDisplay PRIVATE src)
target_compile_definitions(DshotDisplay PRIVATE BOARD=${BOARD})
if(DSHOT_PIN)
    target_compile_definitions(DshotDisplay PRIVATE DSHOT_PIN=${DSHOT_PIN})
    message(STATUS "Board: ${BOARD}, DShot on GP${DSHOT_PIN}")
else()
    message(STATUS "Board: ${BOARD}, DShot on the board default")
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
    Pico_Bidir_DShot
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)

# Serial telemetry goes over USB CDC, not a UART: the board has no spare pins
# for a debug UART, and the USB port is already there for flashing.
pico_enable_stdio_usb(DshotDisplay 1)
pico_enable_stdio_uart(DshotDisplay 0)

# .uf2 for drag-and-drop flashing, plus .elf/.map/.dis for debugging.
pico_add_extra_outputs(DshotDisplay)

# Warnings on our own sources only. The fetched dependencies are not ours to
# keep warning-clean, and -Werror on them would make an upstream bump a build
# break rather than a review.
target_compile_options(DshotDisplay PRIVATE
    -Wall -Wextra -Wshadow -Wno-unused-parameter
)

# Standalone SD bring-up test: nothing but the card, sweeping the SPI clock.
# Built alongside the firmware because the whole value of it is being able to
# flash one, then the other, without changing anything else.
add_executable(sdtest
    tools/sdtest/main.c
    src/sd_hw_config.c
)
target_include_directories(sdtest PRIVATE src)
target_compile_definitions(sdtest PRIVATE BOARD=${BOARD})
target_link_libraries(sdtest
    pico_stdlib
    hardware_spi
    hardware_clocks
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)
target_compile_definitions(sdtest PRIVATE USE_PRINTF=1 USE_DBG_PRINTF=1)
pico_enable_stdio_usb(sdtest 1)
pico_enable_stdio_uart(sdtest 0)
pico_add_extra_outputs(sdtest)

if(SD_DEBUG)
    # USE_PRINTF routes the driver's messages to stdio; USE_DBG_PRINTF adds the
    # per-command tracing that shows exactly which step of card init failed.
    target_compile_definitions(DshotDisplay PRIVATE USE_PRINTF=1 USE_DBG_PRINTF=1)
endif()
