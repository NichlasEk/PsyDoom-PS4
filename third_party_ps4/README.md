# PS4 dependency source snapshots

These are source-only snapshots of the exact local dependencies used by the
port, including modifications made during PS4 development. No sibling checkout
or precompiled graphics archive is needed. The build writes to build/ only.

`sources.lock.json` records every supplied dependency file hash and each
component's upstream URL/base commit. `upstream-diff.patch` records tracked
changes relative to that commit; the snapshot itself is authoritative and also
includes required generated sources and locally added source files.

- OpenGNM: MIT; includes AMD-derived GpuAddr sources with their own notices.
- Vulkan-PS4: MIT.
- PSBC: MIT project wrapper, vendored Mesa/NIR/ACO and utility sources retain
  their individual copyright and license notices.
- Vulkan-Headers and SPIRV-Headers: see each LICENSE/ LICENSES directory.
- SDL-PS4: see COPYING.txt and per-file notices. Snapshot base is the OpenOrbis
  SDL revision recorded in the lock. PS4-specific application overrides live in
  ps4/platform, with corresponding source notices retained.

OpenOrbis itself is an external toolchain, not included here. Its source is at
https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain; its linked runtime components
have their own upstream licenses. Tested SDK hashes are in ps4/toolchain-tested.json.

Do not replace a pinned snapshot silently. Review source changes and update
hashes deliberately, then repeat clean-build and runtime validation.
