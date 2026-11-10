#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
stack=${PSYDOOM_NATIVE_STACK:-"$root/build/ps4-deps/native"}
mkdir -p "$root/build/ps4-host-check"
clang++ -std=c++17 -I "$root/ps4/vulkan" -I "$stack/Vulkan-Headers/include" \
 "$root/ps4/tests/wsi_contract.cpp" "$root/ps4/vulkan/videoout_surface.cpp" \
 -o "$root/build/ps4-host-check/wsi-contract"
"$root/build/ps4-host-check/wsi-contract"
