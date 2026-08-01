#!/usr/bin/env bash
#
# Validate a UF2 firmware image.
#
# Exists as a script rather than inline workflow YAML so it can be tested. The
# first version of this check lived in the workflow, was never run against a
# real UF2, and rejected every valid one: it expected magic bytes 55463200 when
# the correct value is 5546320a. A guard that has only ever been read is not a
# guard.
#
#   usage: check_uf2.sh <file.uf2>
#
# Structure of a UF2 block (512 bytes, little-endian on disk), per
# https://github.com/microsoft/uf2 :
#
#   0x000  magicStart0  0x0A324655  "UF2\n"   -> 55 46 32 0a
#   0x004  magicStart1  0x9E5D5157            -> 57 51 5d 9e
#   0x008  flags
#   0x00C  targetAddr
#   0x010  payloadSize
#   0x014  blockNo
#   0x018  numBlocks
#   0x01C  fileSize / familyID
#   0x020  data[476]
#   0x1FC  magicEnd     0x0AB16F30            -> 30 6f b1 0a

set -u

file="${1:-}"
if [ -z "$file" ]; then
	echo "usage: $0 <file.uf2>" >&2
	exit 2
fi
if [ ! -f "$file" ]; then
	echo "error: $file does not exist" >&2
	exit 1
fi

fail() { echo "error: $file: $*" >&2; exit 1; }

# Read `len` bytes at `off` as lowercase hex.
at() { od -An -tx1 -j "$1" -N "$2" "$file" | tr -d ' \n'; }

size=$(wc -c < "$file" | tr -d ' ')

[ "$size" -ge 512 ]        || fail "too small to hold one block ($size bytes)"
[ $((size % 512)) -eq 0 ]  || fail "size $size is not a multiple of the 512-byte block"

# All three magics, not just the first. Checking only magicStart0 would accept a
# file that merely begins with the right four bytes.
m0=$(at 0 4);   [ "$m0" = "5546320a" ] || fail "bad magicStart0: $m0 (want 5546320a)"
m1=$(at 4 4);   [ "$m1" = "57515d9e" ] || fail "bad magicStart1: $m1 (want 57515d9e)"
me=$(at 508 4); [ "$me" = "306fb10a" ] || fail "bad magicEnd: $me (want 306fb10a)"

blocks=$((size / 512))
echo "$file: OK — $size bytes, $blocks block(s)"
