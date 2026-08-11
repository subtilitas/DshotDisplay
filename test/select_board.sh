#!/usr/bin/env bash
# Rewrite the default board in src/board.h, in place.
#
#   test/select_board.sh BOARD_RP2350_TOUCH_LCD_2_8
#
# Neither the Arduino IDE nor an arduino-cli sketch profile can pass a -D flag
# to the compiler, so for a firmware build the board has to be a literal in the
# source. This script makes that edit and then reads it back, because a sed that
# quietly matches nothing is exactly how a CI matrix ends up building the same
# board twice and reporting green.
#
# Host builds do not need this -- test/Makefile passes -DBOARD=... instead.
set -eu

board="${1:-}"
file="${2:-src/board.h}"

case "$board" in
	BOARD_RP2350_TOUCH_LCD_2|BOARD_RP2350_TOUCH_LCD_2_8) ;;
	*)
		echo "usage: $0 <BOARD_RP2350_TOUCH_LCD_2|BOARD_RP2350_TOUCH_LCD_2_8> [board.h]" >&2
		exit 2
		;;
esac

[ -f "$file" ] || { echo "select_board.sh: no such file: $file" >&2; exit 1; }

sed -i "s|^#define BOARD .*|#define BOARD $board|" "$file"

# Read it back. Anchored, because BOARD_RP2350_TOUCH_LCD_2 is a prefix of
# BOARD_RP2350_TOUCH_LCD_2_8 and a loose grep would accept the wrong one.
grep -qx "#define BOARD $board" "$file" || {
	echo "select_board.sh: $file was not rewritten -- has the #define moved?" >&2
	exit 1
}

echo "board: $board"
