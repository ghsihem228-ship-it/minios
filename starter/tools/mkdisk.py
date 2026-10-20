#!/usr/bin/env python3
"""Create a dedicated MiniOS64 snapshot image exclusively; never overwrite one."""
import argparse
from pathlib import Path
import struct
p = argparse.ArgumentParser()
p.add_argument('path', type=Path)
a = p.parse_args()
a.path.parent.mkdir(parents=True, exist_ok=True)
try:
    stream = a.path.open('xb')
except FileExistsError:
    print(f'Keeping existing image: {a.path}')
else:
    with stream:
        header = b'MOS64DSK' + struct.pack('<III', 1, 64, 2)
        stream.write(header)
        stream.truncate(1024 * 1024)
    print(f'Created dedicated MiniOS64 image: {a.path}')
