#!/usr/bin/env python3
"""Actual x64 ATA PIO saves/reboots plus independently checked snapshot recovery."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
from test_boot import VM, ROOT, READY
from test_shell import command


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def inspect(data, bank):
    base = (1 + bank * 64) * 512
    header = data[base:base + 512]
    assert header[:8] == b'MOS64FS1'
    version, length, sequence, digest, header_digest = struct.unpack_from('<IIQII', header, 8)
    assert version == 1 and length == 8 * 2088
    assert checksum(header[:28]) == header_digest
    payload = data[base + 512:base + 512 + length]
    assert checksum(payload) == digest
    entries = {}
    for i in range(8):
        offset = i * 2088
        used, size = struct.unpack_from('<II', payload, offset)
        if used:
            name = payload[offset+8:offset+40].split(b'\0')[0].decode()
            entries[name] = payload[offset+40:offset+40+size]
    return sequence, entries


def guest(qemu, image):
    return VM(qemu, ROOT / 'build/kernel.bin', extra=('-drive', f'file={image},format=raw,if=ide,index=0'))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--qemu', default='qemu-system-x86_64')
    a = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='minios64-store-') as folder:
        disk = Path(folder) / 'test.img'
        subprocess.run(['python3', str(ROOT/'tools/mkdisk.py'), str(disk)], check=True)
        with guest(a.qemu, disk) as vm:
            vm.wait_for(READY); vm.connect()
            command(vm, 'disk', b'Disk ready; snapshot=0')
            command(vm, 'write persistent first', b'Wrote bytes=5')
            command(vm, 'save', b'Saved snapshot=1')
            command(vm, 'write persistent second', b'Wrote bytes=6')
            command(vm, 'save', b'Saved snapshot=2')
        original = disk.read_bytes()
        assert inspect(original, 0)[0] == 1 and inspect(original, 0)[1]['persistent'] == b'first'
        assert inspect(original, 1)[0] == 2 and inspect(original, 1)[1]['persistent'] == b'second'
        subprocess.run(['python3', str(ROOT/'tools/mkdisk.py'), str(disk)], check=True)
        assert disk.read_bytes() == original, 'Formatter overwrote an existing image'
        with guest(a.qemu, disk) as vm:
            vm.wait_for(READY); vm.connect()
            command(vm, 'disk', b'Disk ready; snapshot=2')
            command(vm, 'cat persistent', b'\r\nsecond\r\n')
        corrupt = bytearray(original)
        corrupt[66 * 512 + 100] ^= 0x80 # latest bank payload: interrupted/corrupt write
        disk.write_bytes(corrupt)
        with guest(a.qemu, disk) as vm:
            vm.wait_for(READY); vm.connect()
            command(vm, 'disk', b'Disk ready; snapshot=1')
            command(vm, 'cat persistent', b'\r\nfirst\r\n')
        # Both bank headers damaged: preserve image, expose no writable mount.
        corrupt[1 * 512] ^= 0x80
        disk.write_bytes(corrupt)
        before = hashlib.sha256(corrupt).digest()
        with guest(a.qemu, disk) as vm:
            vm.wait_for(READY); vm.connect()
            command(vm, 'disk', b'Disk unavailable')
            command(vm, 'save', b'Error')
        assert hashlib.sha256(disk.read_bytes()).digest() == before
        # An unrelated raw image must never be formatted by the kernel.
        unknown = Path(folder) / 'unrelated.img'
        unknown.write_bytes(b'USER DATA' + bytes(1024*1024-9))
        before = hashlib.sha256(unknown.read_bytes()).digest()
        with guest(a.qemu, unknown) as vm:
            vm.wait_for(READY); vm.connect()
            command(vm, 'disk', b'Disk unavailable')
            command(vm, 'save', b'Error')
        assert hashlib.sha256(unknown.read_bytes()).digest() == before
    print('PASS x64 storage: ATA save/reboot, independent bank checksums, old-bank recovery, corrupt/unknown disk preservation', flush=True)


if __name__ == '__main__':
    main()
