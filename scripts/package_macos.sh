#!/bin/sh
# Build an arm64 macOS folder you can copy off the source tree.
# SDL3 is bundled next to the executable. Homebrew is still required to compile.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
sdl=/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib
if [ ! -f "$sdl" ]; then
    echo "Install SDL3 first: brew install sdl3" >&2
    exit 1
fi
cmake_bin=cmake
if [ -x /opt/homebrew/bin/cmake ]; then
    cmake_bin=/opt/homebrew/bin/cmake
fi
"$cmake_bin" -S "$root" -B "$root/build" -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_OSX_ARCHITECTURES=arm64
"$cmake_bin" --build "$root/build" --target pocketglass
python3 "$root/examples/make_play_game.py"
out="$root/dist/pocketglass-macos-arm64"
rm -rf "$out"
mkdir -p "$out"
cp "$root/build/pocketglass" "$out/pocketglass"
cp "$root/examples/catch.gb" "$out/catch.gb"
cp "$sdl" "$out/libSDL3.0.dylib"
cp /opt/homebrew/opt/sdl3/share/licenses/SDL3/LICENSE.txt "$out/SDL3-LICENSE.txt"
install_name_tool -id @executable_path/libSDL3.0.dylib "$out/libSDL3.0.dylib"
install_name_tool -change /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib @executable_path/libSDL3.0.dylib "$out/pocketglass"
codesign -s - --force "$out/libSDL3.0.dylib" "$out/pocketglass"
cat > "$out/README.txt" << 'EOF'
Pocketglass for Apple Silicon (arm64)

This folder is meant to be copied somewhere outside the source tree.
The executable loads libSDL3.0.dylib from the same folder. SDL3 is
the zlib license in SDL3-LICENSE.txt. Pocketglass itself is the source
tree that produced this build; this package does not add a new license.

Run Catch:

    ./pocketglass catch.gb

Controls: arrows move, Z is A, X is B, Enter is Start, Backspace or
Right Shift is Select. P pauses, H shows those controls in the window,
M mutes. Close the window to stop. A battery cartridge writes game.sav
beside the ROM only after the window closes cleanly.

A .nes, .sfc, .smc, .gba, or Game Boy Color-only file is refused with
an error and no window. This is not a complete Game Boy emulator.
EOF
echo "Package is $out"
