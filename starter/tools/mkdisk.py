#!/usr/bin/env python3
"""Create a dedicated MiniOS64 snapshot image exclusively; never overwrite one."""
import argparse
from pathlib import Path
import re
import struct


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def main():
    p = argparse.ArgumentParser()
    p.add_argument('path', type=Path)
    p.add_argument('--file', nargs=2, action='append', default=[], metavar=('NAME', 'PATH'))
    a = p.parse_args()
    if len(a.file) > 8:
        p.error('At most eight files fit in this filesystem')
    payload = bytearray(8 * 2088)
    names = set()
    for i, (name, source) in enumerate(a.file):
        if not re.fullmatch(r'[A-Za-z0-9_.-]{1,31}', name) or name in ('.', '..') or name in names:
            p.error('Names must be unique relative names of 1-31 ASCII characters')
        names.add(name)
        data = Path(source).read_bytes()
        if len(data) > 2048:
            p.error(f'{name}: exceeds the current 2048-byte file limit')
        struct.pack_into('<II32s', payload, i * 2088, 1, len(data), name.encode())
        payload[i * 2088 + 40:i * 2088 + 40 + len(data)] = data
    a.path.parent.mkdir(parents=True, exist_ok=True)
    try:
        stream = a.path.open('xb')
    except FileExistsError:
        print(f'Keeping existing image: {a.path}')
    else:
        with stream:
            stream.write(b'MOS64DSK' + struct.pack('<III', 1, 64, 2))
            stream.truncate(1024 * 1024)
            if a.file:
                stream.seek(2 * 512); stream.write(payload)
                header = b'MOS64FS1' + struct.pack('<IIQI', 1, len(payload), 1, checksum(payload))
                stream.seek(512); stream.write(header + struct.pack('<I', checksum(header)))
        print(f'Created dedicated MiniOS64 image: {a.path}')


if __name__ == '__main__':
    main()
