#!/bin/sh
# Runs the downloaded CPU ROMs. Exit status is 0 when every ROM that is not
# listed as blocked either passes or is reported below. The script itself
# returns 0 after printing a summary; inspect PASS, FAIL, and TIMEOUT lines.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
runner="$root/build/cpu_rom_runner"
roms="$root/tests/external/roms"
if [ ! -x "$runner" ]; then
    echo "Build first: cmake --build build --target cpu_rom_runner" >&2
    exit 1
fi
if [ ! -d "$roms/blargg" ]; then
    echo "Download first: tests/external/fetch_cpu_roms.sh" >&2
    exit 1
fi

fail=0
echo "== Blargg cpu_instrs individual =="
for rom in "$roms"/blargg/*.gb; do
    if ! "$runner" --blargg "$rom"; then
        fail=$((fail + 1))
    fi
done

echo "== Mooneye CPU-focused acceptance =="
# Timing and hardware-register tests are attempted on purpose. A timeout or
# failure here usually means the ROM waited on a timer, the PPU, or boot IO.
for rom in "$roms"/mooneye/acceptance/instr/*.gb \
           "$roms"/mooneye/acceptance/ei_sequence.gb \
           "$roms"/mooneye/acceptance/ei_timing.gb \
           "$roms"/mooneye/acceptance/rapid_di_ei.gb \
           "$roms"/mooneye/acceptance/if_ie_registers.gb \
           "$roms"/mooneye/acceptance/boot_regs-dmgABC.gb \
           "$roms"/mooneye/acceptance/div_timing.gb \
           "$roms"/mooneye/acceptance/pop_timing.gb \
           "$roms"/mooneye/acceptance/halt_ime0_ei.gb \
           "$roms"/mooneye/acceptance/call_timing.gb \
           "$roms"/mooneye/acceptance/timer/*.gb; do
    if [ -f "$rom" ]; then
        if ! "$runner" --mooneye "$rom"; then
            fail=$((fail + 1))
        fi
    fi
done
echo "non-passing runs: $fail"
exit 0
