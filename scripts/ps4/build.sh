#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/artifacts/ps4"
exec 9>"$root/artifacts/ps4/build.lock"
flock -n 9 || { echo "A PS4 build is already running" >&2; exit 1; }
python3 "$root/scripts/ps4/build-deps.py"
(cd "$root/vulkan_shaders" && python3 compile_all.py --ps4)
cmake -S "$root" -B "$root/build/ps4" -G Ninja \
 -DCMAKE_TOOLCHAIN_FILE="$root/cmake/PS4Toolchain.cmake" \
 -DOO_PS4_TOOLCHAIN="${OO_PS4_TOOLCHAIN:-/opt/openorbis/OpenOrbis/PS4Toolchain}" \
 -DPSYDOOM_NATIVE_STACK="$root/build/ps4-deps/native" \
 -DPSYDOOM_SDL_SOURCE="$root/third_party_ps4/SDL-PS4" \
 -DPSYDOOM_SDL_ARCHIVE="${OO_PS4_TOOLCHAIN:-/opt/openorbis/OpenOrbis/PS4Toolchain}/lib/libSDL2.a" \
 -DCMAKE_BUILD_TYPE=Release -DPSYDOOM_INCLUDE_VULKAN_RENDERER=ON \
 -DPSYDOOM_INCLUDE_LAUNCHER=OFF "$@" 2>&1 | tee "$root/artifacts/ps4/configure.log"
cmake --build "$root/build/ps4" -j "${BUILD_JOBS:-2}" 2>&1 | tee "$root/artifacts/ps4/build.log"
