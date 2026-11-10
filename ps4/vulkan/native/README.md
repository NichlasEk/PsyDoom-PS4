# PsyDoom local Vulkan sources

Copied from the existing OpenOrbis Vulkan stack; see LICENSE. Sibling sources and archives are not modified.

Original file SHA256:
- vk_ps4_command.c: 11efffc75e3e17519d4941b05a602ecd93893a9db037eca54a74391f66e633b2
- vk_ps4_queue.c: 91006e8e10c8bb7c1fd84c6952fb8367c9600860dd4c05a3bd50c4fcdb4c8b35
- vk_ps4_image.c: 906b3a224939d0e5e4a6d4e4a0d3aa2d50ad03477d7dd90efb4d75341d110d6e

Local changes: R16/BGRA/A8B8G8R8 transfers; halfword DMA; row-linear R16 backing; render-target clear through the existing meta pipeline; WAIT/SIGNAL GPU completion before epilogue reuse; PM4 stream validation and bounded diagnostics.
The queue fix serializes synchronization packets and may reduce performance. The shader-based blit displays the intro in shadPS4. Main gameplay and input were subsequently tested in shadPS4. Physical PS4 remains unverified.

`blit.frag` is compiled and SPIR-V validated by the CMake native shader target. Each blit snapshots descriptors and coordinates into command-owned scratch, retained until reset after completion. Supported region shape is one 2D color layer at mip zero; unsupported shapes fail explicitly.
