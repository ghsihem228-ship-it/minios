#!/usr/bin/env python3
"""Run a disk-backed compact ELF, reboot, and reject corrupt executable files."""
import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path
from test_boot import ROOT, READY
from test_store import guest, inspect
from test_shell import command


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--qemu', default='qemu-system-x86_64')
    a = p.parse_args()
    elf = (ROOT / 'build/disk-demo.elf').read_bytes()
    assert len(elf) <= 2048 and elf[:7] == b'\x7fELF\x02\x01\x01'
    with tempfile.TemporaryDirectory(prefix='minios64-exec-') as folder:
        disk = Path(folder) / 'exec.img'
        bad = bytearray(elf)
        struct.pack_into('<I', bad, 64 + 4, 7)  # W+X code must never launch.
        bad_path = Path(folder) / 'bad.elf'; bad_path.write_bytes(bad)
        subprocess.run(['python3', str(ROOT/'tools/mkdisk.py'), str(disk),
                        '--file', 'hello', str(ROOT/'build/disk-demo.elf'),
                        '--file', 'bad', str(bad_path)], check=True)
        assert inspect(disk.read_bytes(), 0)[1]['hello'] == elf
        original = disk.read_bytes()
        subprocess.run(['python3', str(ROOT/'tools/mkdisk.py'), str(disk), '--file', 'other', str(bad_path)], check=True)
        assert disk.read_bytes() == original
        with guest(a.qemu, disk) as vm:
            vm.wait_for(READY); vm.connect()
            baseline = int(re.search(rb'Frames free=(\d+)', command(vm, 'mem', b'Frames free='))[1])
            command(vm, 'runfile hello', b'[exit] 42')
            command(vm, 'cat disk-note', b'\r\nfrom-disk\r\n')
            command(vm, 'runfile bad', b'[exit] -12')
            command(vm, 'runfile missing', b'file not found')
            command(vm, 'write text invalid ELF', b'Wrote bytes=11')
            command(vm, 'runfile text', b'[exit] -12')
            assert int(re.search(rb'Frames free=(\d+)', command(vm, 'mem', b'Frames free='))[1]) == baseline
            command(vm, 'save', b'Saved snapshot=2')
        assert inspect(disk.read_bytes(), 1)[1]['disk-note'] == b'from-disk'
        with guest(a.qemu, disk) as vm:
            vm.wait_for(READY); vm.connect()
            command(vm, 'cat disk-note', b'\r\nfrom-disk\r\n')
            command(vm, 'runfile hello', b'[exit] 42')
        print('PASS disk exec QEMU: compact ELF64, actual file write, W+X/malformed rejection, frame cleanup and reboot persistence', flush=True)


if __name__ == '__main__':
    main()
