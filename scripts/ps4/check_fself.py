#!/usr/bin/env python3
"""Check that create-fself preserved initialized application data (ELF64 LE)."""
import struct
import sys
from pathlib import Path


def check_data(source, converted):
    original, output = Path(source).read_bytes(), Path(converted).read_bytes()
    for data in (original, output):
        if data[:6] != b'\x7fELF\x02\x01':
            raise ValueError('Expected a little-endian ELF64 file')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', original)
    sections = [struct.unpack_from('<IIQQQQIIQQ', original, header[6] + i * header[11])
                for i in range(header[12])]
    names = sections[header[13]]
    strings = original[names[4]:names[4] + names[5]]
    target = struct.unpack_from('<16sHHIQQQIHHHHHH', output)
    loads = [struct.unpack_from('<IIQQQQQQ', output, target[5] + i * target[9])
             for i in range(target[10])]
    checked = 0
    for section in sections:
        name = strings[section[0]:].split(b'\0', 1)[0].decode()
        if not (name == '.data' or name.startswith('.data.')):
            continue
        if name.startswith(('.data.rel.ro', '.data.sce_')) or not section[5]:
            continue
        address, offset, size = section[3:6]
        segment = next((p for p in loads if p[0] == 1 and p[1] & 2
                        and p[3] <= address and address + size <= p[3] + p[5]), None)
        if segment is None:
            raise ValueError(f'{name}: initialized data missing from FSELF payload')
        target_offset = segment[2] + address - segment[3]
        if original[offset:offset + size] != output[target_offset:target_offset + size]:
            raise ValueError(f'{name}: initialized data changed during conversion')
        checked += 1
    if not checked:
        raise ValueError('No initialized data sections found')
    return checked


if __name__ == '__main__':
    print(f'FSELF initialized data verified: {check_data(*sys.argv[1:])} sections')
