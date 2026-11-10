#!/usr/bin/env python3
"""Compile the local native blit shader and validate its SPIR-V."""
import pathlib
import struct
import subprocess
import tempfile
import sys
source, output = map(pathlib.Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='psydoom-shader-') as tmp:
    binary = pathlib.Path(tmp) / 'blit.spv'
    subprocess.run(['glslc', '--target-env=vulkan1.0', '-O', str(source), '-o', str(binary)], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.0', str(binary)], check=True)
    data = binary.read_bytes()
words = struct.unpack('<' + 'I' * (len(data) // 4), data)
text = '// Generated from blit.frag; do not edit.\nstatic const uint32_t psydoom_blit_frag[] = {\n'
text += '\n'.join('    ' + ','.join(f'0x{x:08x}' for x in words[i:i+8]) + ',' for i in range(0, len(words), 8))
text += '\n};\n'
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != text:
    output.write_text(text)
