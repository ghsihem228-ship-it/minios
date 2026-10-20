#!/usr/bin/env python3
"""Create a sparse FAT32 superfloppy. Existing images are never overwritten."""
import argparse
import os
from pathlib import Path
import struct


def create(path, mib=64):
    path = Path(path)
    if not 64 <= mib <= 512:
        raise ValueError('Image size must be between 64 and 512 MiB')
    path.parent.mkdir(parents=True, exist_ok=True)
    total = mib * 2048
    reserved, fats, spc = 32, 2, 1
    fat_size = (total + 127) // 128  # conservatively includes room for reserved entries
    clusters = total - reserved - fats * fat_size
    boot = bytearray(512)
    boot[:11] = b'\xeb\x58\x90MINIOS  '
    struct.pack_into('<HBHBHHBHHHII', boot, 11,
                     512, spc, reserved, fats, 0, 0, 0xF8, 0, 63, 255, 0, total)
    struct.pack_into('<IHHIHH', boot, 36, fat_size, 0, 0, 2, 1, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into('<I', boot, 67, 0x4D4F5307)
    boot[71:82] = b'MINIOS DATA'
    boot[82:90] = b'FAT32   '
    boot[510:] = b'\x55\xaa'
    info = bytearray(512)
    struct.pack_into('<I', info, 0, 0x41615252)
    struct.pack_into('<III', info, 484, 0x61417272, clusters - 1, 3)
    struct.pack_into('<I', info, 508, 0xAA550000)
    # O_EXCL protects both pre-existing files and a concurrent creator.
    with path.open('xb') as f:
        f.truncate(total * 512)
        for sector, data in [(0, boot), (1, info), (6, boot), (7, info)]:
            f.seek(sector * 512); f.write(data)
        initial = struct.pack('<III', 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF)
        for copy in range(fats):
            f.seek((reserved + copy * fat_size) * 512); f.write(initial)
        label = bytearray(32)
        label[:11] = b'MINIOS DATA'
        label[11] = 8
        for offset in (16, 18, 24): struct.pack_into('<H', label, offset, 0x21)
        f.seek((reserved + fats * fat_size) * 512); f.write(label)
        f.flush(); os.fsync(f.fileno())
    return clusters


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', nargs='?', default='disks/minios.img')
    parser.add_argument('--mib', type=int, default=64)
    args = parser.parse_args()
    try:
        clusters = create(args.path, args.mib)
    except FileExistsError:
        print(f'Preserving existing disk: {args.path}')
        return
    print(f'Created {args.path}: FAT32, {args.mib} MiB, {clusters} data clusters')


if __name__ == '__main__':
    main()
