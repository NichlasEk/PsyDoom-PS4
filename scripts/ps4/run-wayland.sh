#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
eboot=${1:?Pass the packaged eboot.bin path (see manifest.json)}
test -f "$eboot"
# Zero disables only the time limit; memory and swap containment stay active.
runtime=${PSYDOOM_TEST_RUNTIME:-60}
[[ "$runtime" =~ ^[0-9]+$ ]] || { echo "PSYDOOM_TEST_RUNTIME must be seconds, or 0 for no time limit" >&2; exit 2; }
runtime_args=()
if [[ "$runtime" != 0 ]]; then
  runtime_args=(-p "RuntimeMaxSec=$runtime")
fi
# A transient service isolates the emulator from the terminal and other apps.
# Do not fall back to an unrestricted launch if cgroup setup fails.
exec systemd-run --user --wait --pipe --collect \
  --unit="psydoom-test-$(date +%s)" \
  -p MemoryMax=4G -p MemorySwapMax=0 -p LimitCORE=0 \
  "${runtime_args[@]}" -p TimeoutStopSec=5 -p KillMode=control-group \
  --working-directory="$root" \
  --setenv="WAYLAND_DISPLAY=${WAYLAND_DISPLAY:?}" \
  --setenv="XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:?}" \
  --setenv=SDL_VIDEODRIVER=wayland --setenv=QT_QPA_PLATFORM=wayland \
  --setenv="XDG_DATA_HOME=$root/artifacts/ps4/wayland-run/profile" \
  "${SHADPS4_BIN:-shadps4}" -g "$eboot" -f false
