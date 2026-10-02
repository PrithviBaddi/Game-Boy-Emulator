#!/bin/sh
# Downloads CPU test ROMs into tests/external/roms. The binaries are not
# committed. Blargg's individual cpu_instrs images come from the retrio
# mirror of Shay Green's public test ROMs. Mooneye images are the current
# published build of the MIT-licensed Mooneye Test Suite.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
dest="$root/tests/external/roms"
mkdir -p "$dest/blargg" "$dest/mooneye"

blargg_base=https://raw.githubusercontent.com/retrio/gb-test-roms/c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15/cpu_instrs/individual
for name in \
    "01-special.gb" \
    "02-interrupts.gb" \
    "03-op sp,hl.gb" \
    "04-op r,imm.gb" \
    "05-op rp.gb" \
    "06-ld r,r.gb" \
    "07-jr,jp,call,ret,rst.gb" \
    "08-misc instrs.gb" \
    "09-op r,r.gb" \
    "10-bit ops.gb" \
    "11-op a,(hl).gb"
do
    curl -fsSL "$blargg_base/$(printf '%s' "$name" | sed 's/ /%20/g; s/,/%2C/g')" -o "$dest/blargg/$name"
done
curl -fsSL https://raw.githubusercontent.com/retrio/gb-test-roms/c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15/cpu_instrs/readme.txt \
    -o "$dest/blargg/readme.txt"

mooneye_zip=https://gekkio.fi/files/mooneye-test-suite/mts-20260714-0944-31510e1/mts-20260714-0944-31510e1.zip
tmp=$(mktemp -d)
curl -fsSL "$mooneye_zip" -o "$tmp/mts.zip"
mkdir -p "$dest/mooneye/acceptance/instr"
# The archive also contains multi-megabyte cartridge images. Extract only the
# CPU-oriented acceptance ROMs.
unzip -q -o -j "$tmp/mts.zip" \
    '*/acceptance/instr/*.gb' \
    -d "$dest/mooneye/acceptance/instr"
for name in ei_sequence ei_timing halt_ime0_ei rapid_di_ei if_ie_registers \
    boot_regs-dmgABC div_timing pop_timing call_timing; do
    unzip -q -o -j "$tmp/mts.zip" "*/acceptance/${name}.gb" -d "$dest/mooneye/acceptance"
done
unzip -q -o -j "$tmp/mts.zip" '*/acceptance/timer/*.gb' -d "$dest/mooneye/acceptance/timer"
unzip -q -o -j "$tmp/mts.zip" '*/LICENSE' -d "$dest/mooneye"
rm -rf "$tmp"
echo "ROMs are in $dest"
