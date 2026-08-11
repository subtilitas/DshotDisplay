# DshotDisplay — native Pico SDK build.
#
#   cmake -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s .
#   ninja -C build
#
# Produces build/DshotDisplay.uf2.
#
# PICO_PLATFORM selects the core: rp2350-arm-s for Cortex-M33, rp2350-riscv for
# the Hazard3 cores. Both are built in CI — the RISC-V build catches anything
# that quietly assumes Cortex-M.
#
# Dependencies are fetched at configure time rather than vendored, so the tree
# stays free of third-party source, and pinned to exact tags so a build here
# matches a build anywhere. That pinning is the whole point: an unpinned
# FetchContent is a lock file that silently rewrites itself.

cmake_minimum_required(VERSION 3.13)

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

# Warnings that the Arduino build had on via --warnings all. Not -Werror on the
# fetched dependencies, only on our own sources.
target_compile_options(DshotDisplay PRIVATE
    -Wall -Wextra -Wshadow -Wno-unused-parameter
)
