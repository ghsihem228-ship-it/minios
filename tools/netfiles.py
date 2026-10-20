#!/usr/bin/env python3
"""Prepare QEMU TFTP examples, preserving existing host files."""
import argparse
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory')
p.add_argument('elf')
a = p.parse_args()
root = Path(a.directory)
root.mkdir(parents=True, exist_ok=True)
for name, data in [('hello.elf', Path(a.elf).read_bytes()),
                   ('welcome.txt', b'Hello from the MiniOS network!\n')]:
    try:
        with (root / name).open('xb') as f:
            f.write(data)
    except FileExistsError:
        pass
