# Building the PS4 port

## Prerequisites

Linux x86-64; OpenOrbis PS4 Toolchain; Clang/LLVM (clang, clang++, lld,
llvm-ar, llvm-nm, llvm-objdump), CMake, Ninja, GNU make, Python 3.11+ with Mako,
`glslc`, `spirv-val`, and the .NET runtime required by your OpenOrbis PkgTool.
The system C++ compiler is used for host-side regression tests.

Set `OO_PS4_TOOLCHAIN` to your SDK directory. The SDK provides its static C/C++
and SDL2 libraries, link stubs, crt1 and packaging tools. Matching SDL source
headers and all locally modified graphics library sources are supplied here.
`toolchain-tested.json` records hashes of the external SDK used for validation;
it is provenance, not a claim that every SDK release is interchangeable.

## Build and package

```sh
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
BUILD_JOBS=2 bash scripts/ps4/build.sh
python3 scripts/ps4/package.py --output-dir dist/local
```

The dependency builder verifies `third_party_ps4/sources.lock.json`, copies
sources into `build/ps4-deps/native`, and builds OpenGNM, PSBC and Vulkan-PS4.
Source snapshots are not modified by compilation. Dependencies are cached;
changing their source lock or compiler invalidates the cache. To force a fresh
build, remove `build/ps4-deps/build-id`.

The app build uses local SDL video/joystick overrides and local Vulkan command,
image and queue overrides. It compiles and validates the native blit shader.
The package builder checks initialized ELF data and validates/extracts the PKG,
then verifies each packaged payload. Private game data is never added.

```sh
python3 ps4/tests/check_disc_search.py
python3 ps4/tests/check_dma.py
```

## Optional shadPS4 test

This requires a Wayland desktop and a user systemd manager. Set `SHADPS4_BIN` to
your emulator executable, then pass the packaged staging eboot path listed in
`dist/local/manifest.json`:

```sh
export SHADPS4_BIN=/path/to/shadps4
bash scripts/ps4/run-wayland.sh /path/to/package-stage/eboot.bin
```

The runner uses a 4 GiB memory limit, no swap, and a default 60-second timeout.
Set `PSYDOOM_TEST_RUNTIME=0` only for an interactive session without a timeout.
The isolated emulator profile is under `artifacts/ps4/wayland-run/profile`.
Physical PS4 installation and playback must be tested separately.
