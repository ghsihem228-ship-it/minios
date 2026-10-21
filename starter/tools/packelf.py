#!/usr/bin/env python3
"""Compact a freestanding ELF64's loadable bytes; no relocation or compression."""
import argparse
from pathlib import Path
import struct


def pack(data):
    if len(data) < 64:
        raise ValueError('Truncated ELF header')
    h = list(struct.unpack_from('<16sHHIQQQIHHHHHH', data))
    if h[0][:7] != b'\x7fELF\x02\x01\x01' or h[1:4] != [2, 62, 1] or h[8:10] != [64, 56]:
        raise ValueError('Expected little-endian ELF64 x86_64 ET_EXEC')
    if not 1 <= h[10] <= 8 or h[5] > len(data) or h[10] > (len(data)-h[5]) // 56:
        raise ValueError('Invalid program-header table')
    segments = []
    for i in range(h[10]):
        ph = list(struct.unpack_from('<IIQQQQQQ', data, h[5] + i*56))
        if ph[0] == 0x6474e551 and ph[1] & 1:
            raise ValueError('Executable stack unsupported')
        if ph[0] in (0, 4, 6, 0x6474e551):
            continue
        if ph[0] != 1 or ph[2] > len(data) or ph[5] > len(data)-ph[2] or ph[5] > ph[6]:
            raise ValueError('Unsupported/truncated load segment')
        segments.append((ph, data[ph[2]:ph[2]+ph[5]]))
    if not segments:
        raise ValueError('No load segments')
    # ET_EXEC addresses stay fixed; p_align=1 permits compact file offsets.
    h[5], h[6], h[10], h[11], h[12], h[13] = 64, 0, len(segments), 0, 0, 0
    payload = bytearray()
    headers = bytearray()
    for ph, content in segments:
        ph[2] = 64 + len(segments)*56 + len(payload)
        ph[7] = 1
        headers += struct.pack('<IIQQQQQQ', *ph)
        payload += content
    return struct.pack('<16sHHIQQQIHHHHHH', *h) + headers + payload


def main():
    p = argparse.ArgumentParser()
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    try:
        output = pack(a.input.read_bytes())
    except ValueError as error:
        p.error(str(error))
    a.output.write_bytes(output)
    print(f'Packed ELF: {len(output)} bytes -> {a.output}')


if __name__ == '__main__':
    main()
