// Host-test stub. The macros arduino-pico puts on every real compile line,
// straight from recipe.cpp.o.pattern in its platform.txt. Force-included with
// -include, so they land before any project header exactly as the -D flags do.
//
// WHY THIS EXISTS
//
// The rest of test/stubs/ imitates the headers the firmware includes, which is
// most of the platform but not all of it: a device build also arrives carrying
// a pile of -D flags that no header declares. Those were invisible here, so a
// board header could define BOARD_NAME -- a name the core already owns -- and
// every host check stayed green while every real compile emitted
//
//     warning: 'BOARD_NAME' redefined
//
// on every translation unit. The permutation job compiles with -Werror, so with
// this file in the picture the next such collision is an error there instead.
//
// Values are the rpipico2 ones. Only the names matter for collision detection;
// nothing here should ever be read by firmware code.
#pragma once

#define BOARD_NAME      "RPIPICO2"
#define ARDUINO_VARIANT "rpipico2"
#define ARDUINO_RASPBERRY_PI_PICO_2 1
#define ARDUINO_ARCH_RP2040 1
#define ARDUINO         10607
#define F_CPU           150000000
#define PICO_FLASH_SIZE_BYTES 4194304
