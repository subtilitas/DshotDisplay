#!/usr/bin/env bash
#
# Self-test for check_uf2.sh.
#
# The UF2 check shipped broken: it demanded magic bytes 55463200 where the
# correct value is 5546320a, and rejected every valid image on its first real
# run. It had been read but never executed. This builds synthetic UF2 images --
# one valid, several broken in specific ways -- and asserts the checker's
# verdict on each, so the guard itself is guarded.

set -u

here="$(cd "$(dirname "$0")" && pwd)"
checker="$here/check_uf2.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

python3 - "$work" <<'PY'
import struct, sys, os
out = sys.argv[1]

def block(no, total):
    b  = struct.pack("<II", 0x0A324655, 0x9E5D5157)
    b += struct.pack("<IIIIII", 0x2000, 0x10000000 + no * 256, 256, no, total,
                     0xE48BFF59)                       # RP2350 family id
    b += bytes(range(256)) + b"\x00" * 220
    b += struct.pack("<I", 0x0AB16F30)
    assert len(b) == 512
    return b

good = b"".join(block(i, 3) for i in range(3))
open(os.path.join(out, "good.uf2"), "wb").write(good)

def variant(name, mutate):
    d = bytearray(good)
    mutate(d)
    open(os.path.join(out, name), "wb").write(d)

# 55463200 is precisely what the original broken check expected.
variant("bad_magic0.uf2",   lambda d: d.__setitem__(slice(0, 4), b"\x55\x46\x32\x00"))
variant("bad_magic1.uf2",   lambda d: d.__setitem__(slice(4, 8), b"\xde\xad\xbe\xef"))
variant("bad_magicend.uf2", lambda d: d.__setitem__(slice(508, 512), b"\x00\x00\x00\x00"))
open(os.path.join(out, "truncated.uf2"), "wb").write(good[:1000])
open(os.path.join(out, "tiny.uf2"), "wb").write(b"UF2\n")
PY

fails=0

expect() {   # expect <should-pass:0|1> <file>
	local want="$1" file="$2" name
	name="$(basename "$file")"
	if bash "$checker" "$file" >/dev/null 2>&1; then got=0; else got=1; fi
	if [ "$got" = "$want" ]; then
		printf '  %-20s %s\n' "$name" "ok"
	else
		printf '  %-20s FAIL (expected %s, got %s)\n' "$name" \
			"$([ "$want" = 0 ] && echo accept || echo reject)" \
			"$([ "$got" = 0 ] && echo accept || echo reject)"
		fails=$((fails + 1))
	fi
}

echo "check_uf2.sh self-test"
expect 0 "$work/good.uf2"
expect 1 "$work/bad_magic0.uf2"
expect 1 "$work/bad_magic1.uf2"
expect 1 "$work/bad_magicend.uf2"
expect 1 "$work/truncated.uf2"
expect 1 "$work/tiny.uf2"
expect 1 "$work/does_not_exist.uf2"

if [ "$fails" -gt 0 ]; then
	echo "$fails self-test failure(s)"
	exit 1
fi
echo "all ok"
