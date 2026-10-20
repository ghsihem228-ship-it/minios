#!/usr/bin/env python3
"""Actual ATA/FAT32 integration tests, including new QEMU processes per reboot.

Only temporary disk images are modified. The host FAT reader checks on-disk
chains, mirrors, ownership and bytes independently of the guest implementation.
"""
import argparse
import array
import hashlib
from pathlib import Path
import shutil
import struct
import sys
import tempfile

from smoke import Guest, ROOT
sys.path.insert(0, str(ROOT / 'tools'))
from mkdisk import create


class FatImage:
    def __init__(self, path):
        self.path = Path(path)
        with self.path.open('rb') as f:
            boot = f.read(512)
            self.start = 0
            if struct.unpack_from('<H', boot, 11)[0] != 512:
                self.start = struct.unpack_from('<I', boot, 454)[0]
                f.seek(self.start * 512); boot = f.read(512)
            self.spc = boot[13]
            self.reserved = struct.unpack_from('<H', boot, 14)[0]
            self.copies = boot[16]
            self.total = struct.unpack_from('<I', boot, 32)[0]
            self.fat_size = struct.unpack_from('<I', boot, 36)[0]
            self.root = struct.unpack_from('<I', boot, 44)[0]
            self.info = struct.unpack_from('<H', boot, 48)[0]
            self.backup = struct.unpack_from('<H', boot, 50)[0]
            self.fat_lba = self.start + self.reserved
            self.data = self.fat_lba + self.copies * self.fat_size
            self.clusters = (self.start + self.total - self.data) // self.spc
            f.seek(self.fat_lba * 512)
            raw = f.read(self.fat_size * 512)
            self.raw_fat = raw
            self.fat = array.array('I'); self.fat.frombytes(raw)
            if sys.byteorder != 'little': self.fat.byteswap()
            for i in range(1, self.copies):
                f.seek((self.fat_lba + i * self.fat_size) * 512)
                assert f.read(len(raw)) == raw, 'FAT mirrors differ'

    def chain(self, first):
        result, seen = [], set()
        while first:
            assert 2 <= first <= self.clusters + 1 and first not in seen, 'Invalid/cyclic chain'
            seen.add(first); result.append(first)
            entry = self.fat[first] & 0x0FFFFFFF
            first = 0 if entry >= 0x0FFFFFF8 else entry
            assert first != 0 or entry >= 0x0FFFFFF8, 'Unallocated cluster in chain'
        return result

    def data_for(self, chain):
        result = bytearray()
        with self.path.open('rb') as f:
            for c in chain:
                f.seek((self.data + (c - 2) * self.spc) * 512)
                result.extend(f.read(self.spc * 512))
        return result

    def files(self):
        result = {}
        raw = self.data_for(self.chain(self.root))
        for offset in range(0, len(raw), 32):
            e = raw[offset:offset + 32]
            if e[0] == 0: break
            if e[0] == 0xE5 or e[11] & 8: continue
            assert not e[11] & 16, 'Unexpected subdirectory in generated test volume'
            name = e[:8].decode('ascii').rstrip()
            extension = e[8:11].decode('ascii').rstrip()
            if extension: name += '.' + extension
            first = (struct.unpack_from('<H', e, 20)[0] << 16) | struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            assert name not in result, f'Duplicate directory name {name}'
            result[name] = (first, size)
        return result

    def read(self, name):
        first, size = self.files()[name]
        return self.data_for(self.chain(first))[:size]

    def check(self):
        assert self.fat[1] & (1 << 27), 'Volume still marked dirty after successful operation'
        owned = set(self.chain(self.root))
        for name, (first, size) in self.files().items():
            chain = self.chain(first)
            assert len(chain) == (size + self.spc * 512 - 1) // (self.spc * 512), name
            assert not owned.intersection(chain), f'Cross-linked file {name}'
            owned.update(chain)
        allocated = {c for c in range(2, self.clusters + 2) if self.fat[c] & 0x0FFFFFFF}
        assert allocated == owned, f'Leaked or unreferenced clusters: {allocated ^ owned}'
        with self.path.open('rb') as f:
            for sector in (self.info, self.backup + self.info):
                f.seek((self.start + sector) * 512)
                info = f.read(512)
                assert struct.unpack_from('<I', info, 488)[0] == self.clusters - len(allocated), 'FSInfo free count'

    def set_entries(self, updates):
        with self.path.open('r+b') as f:
            for copy in range(self.copies):
                for c, value in updates.items():
                    f.seek((self.fat_lba + copy * self.fat_size) * 512 + c * 4)
                    f.write(struct.pack('<I', value))


def digest(path):
    with Path(path).open('rb') as f:
        result = hashlib.sha256()
        for data in iter(lambda: f.read(1024 * 1024), b''): result.update(data)
        return result.hexdigest()


def program(g, path, status=0, marker=None):
    text = g.command('run ' + path, '[exit]')
    assert f'status={status}\n' in text, text
    if marker: assert marker in text, text


def persistent(qemu, folder):
    disk = folder / 'persistent.img'
    create(disk)
    with Guest(qemu, 64, disk) as g:
        assert 'FAT32 mounted at /disk' in g.screen()
        g.command('write /disk/note.txt hello', 'Wrote bytes=5')
        g.command('append /disk/note.txt world', 'Wrote bytes=5')
        g.command('cat /disk/note.txt', '\nhelloworld\n')
        g.command('cp /disk/note.txt /disk/NOTE.TXT', 'Error errno=17')
        g.command('write /disk/too-long-name.txt x', 'Error errno=36')
        g.command('write /disk/../bad x', 'Error errno=22')
        g.command('cp /bin/hello /disk/hello.elf', 'Copied bytes=')
        program(g, '/disk/hello.elf', marker='Hello from Ring 3!')
        program(g, '/bin/diskdemo', marker='[user] persistent disk roundtrip passed')
        for i in range(20): g.command(f'write /disk/f{i}.txt data{i}', 'Wrote bytes=')
        for i in range(0, 20, 2): g.command(f'rm /disk/f{i}.txt', 'Removed file.')
        g.command('write /disk/reuse.txt reused', 'Wrote bytes=6')
        g.command('sync', 'Disk synchronized.')
    image = FatImage(disk); image.check()
    assert len(image.chain(image.root)) >= 2, 'Root directory did not grow'
    assert image.read('NOTE.TXT') == b'helloworld'
    assert image.read('HELLO.ELF') == (ROOT / 'build/user/hello.elf').read_bytes()
    expected = bytearray(bytes(range(256)) * 48)
    expected[510:518] = b'BOUNDARY'
    expected.extend(b'\0' * (20000 - len(expected)) + b'END')
    assert image.read('USER.DAT') == expected, 'Cross-sector, cluster or sparse data corruption'
    # A distinct QEMU process with the same image proves persisted metadata/data.
    with Guest(qemu, 64, disk) as g:
        g.command('cat /disk/note.txt', '\nhelloworld\n')
        g.command('stat /disk/user.dat', 'Size bytes=20003')
        program(g, '/disk/hello.elf', marker='Hello from Ring 3!')
        g.command('append /disk/note.txt again', 'Wrote bytes=5')
        g.command('rm /disk/user.dat', 'Removed file.')
        g.command('write /disk/note.txt x', 'Wrote bytes=1')
        g.command('sync', 'Disk synchronized.')
    image = FatImage(disk); image.check()
    assert image.read('NOTE.TXT') == b'x'
    assert 'USER.DAT' not in image.files()
    with Guest(qemu, 2, disk) as g:
        g.command('cat /disk/note.txt', '\nx\n')
        program(g, '/disk/hello.elf', marker='Hello from Ring 3!')
    print('PASS storage: ATA, VFS, multi-cluster I/O, sparse writes, directory growth/reuse, '
          'truncation/deletion, Ring 3, disk ELF, three fresh boots (including 2 MiB)', flush=True)
    return disk


def malformed(qemu, folder):
    for case in ('dirty', 'mirror', 'cycle', 'geometry', 'unformatted'):
        disk = folder / (case + '.img'); create(disk)
        image = FatImage(disk)
        if case == 'dirty': image.set_entries({1: image.fat[1] & ~(1 << 27)})
        elif case == 'cycle': image.set_entries({2: 2})
        elif case == 'mirror':
            with disk.open('r+b') as f:
                f.seek((image.fat_lba + image.fat_size) * 512 + 8)
                f.write(struct.pack('<I', 0x0FFFFFF8))
        elif case == 'geometry':
            with disk.open('r+b') as f: f.seek(13); f.write(b'\0')
        else:
            with disk.open('r+b') as f: f.write(bytes(512))
        before = digest(disk)
        with Guest(qemu, 64, disk) as g:
            if case == 'dirty':
                g.command('df', 'FAT32 read-only')
                g.command('write /disk/no.txt x', 'Error errno=30')
            else:
                g.command('df', '/disk: not mounted')
                g.command('write /disk/no.txt x', 'Error errno=19')
            g.command('mem', 'Paging: ON')
        assert digest(disk) == before, f'{case}: rejected disk was modified'
    print('PASS storage: dirty read-only, mirror mismatch, cyclic root, invalid geometry, '
          'unformatted images; RAMFS remains usable and images unchanged', flush=True)


def full_volume(qemu, folder):
    disk = folder / 'full.img'; create(disk)
    image = FatImage(disk)
    # Synthetic occupancy leaves exactly three available clusters. The first
    # 1 KiB write needs two; the next must reserve one then roll it back.
    fat = array.array('I', image.fat)
    for c in range(6, image.clusters + 2): fat[c] = 0x0FFFFFFF
    raw = fat.tobytes()
    if sys.byteorder != 'little': fat.byteswap(); raw = fat.tobytes()
    with disk.open('r+b') as f:
        for copy in range(image.copies):
            f.seek((image.fat_lba + copy * image.fat_size) * 512); f.write(raw)
    with Guest(qemu, 64, disk) as g:
        program(g, '/bin/diskdemo', status=1)
        g.command('stat /disk/user.dat', 'Size bytes=1024')
        g.command('df', 'free-clusters=1')
        g.command('rm /disk/user.dat', 'Removed file.')
        g.command('df', 'free-clusters=3')
    after = FatImage(disk)
    assert after.fat[1] & (1 << 27)
    assert all(after.fat[c] == 0 for c in (3, 4, 5)), 'Failed reservation leaked a cluster'
    print('PASS storage: ENOSPC rollback, previous data length preserved, freed space reusable', flush=True)


def partitioned(qemu, folder):
    source = folder / 'source.img'; create(source)
    disk = folder / 'partitioned.img'
    start = 2048
    with disk.open('xb') as out, source.open('rb') as src:
        out.truncate(source.stat().st_size + start * 512)
        mbr = bytearray(512); mbr[450] = 0x0C
        struct.pack_into('<II', mbr, 454, start, source.stat().st_size // 512)
        mbr[510:] = b'\x55\xaa'; out.write(mbr)
        out.seek(start * 512); shutil.copyfileobj(src, out)
        for sector in (0, 6):
            out.seek((start + sector) * 512 + 28); out.write(struct.pack('<I', start))
    with Guest(qemu, 64, disk) as g:
        g.command('write /disk/mbr.txt partitioned', 'Wrote bytes=11')
    image = FatImage(disk); image.check()
    assert image.read('MBR.TXT') == b'partitioned'
    with Guest(qemu, 64, disk) as g:
        g.command('cat /disk/mbr.txt', '\npartitioned\n')
    print('PASS storage: MBR partition offsets and reboot persistence', flush=True)


def io_failure(qemu, folder, good):
    disk = folder / 'io-error.img'; shutil.copyfile(good, disk)
    conf = folder / 'fault.conf'
    conf.write_text('[inject-error]\nevent = "write_aio"\niotype = "write"\nerrno = "5"\nonce = "off"\n')
    drive = ['-drive', f'file=blkdebug:{conf}:{disk},format=raw,if=ide,index=0,werror=report']
    before = digest(disk)
    with Guest(qemu, 64, drive_args=drive) as g:
        g.command('cat /disk/note.txt', '\nx\n')
        g.command('write /disk/fail.txt x', 'Error errno=5')
        g.command('write /disk/again.txt x', 'Error errno=30')
        g.command('df', 'FAT32 read-only')
        g.command('cat /disk/note.txt', '\nx\n')
    assert digest(disk) == before, 'Injected first-write failure modified image'
    print('PASS storage: injected ATA write failure surfaces as EIO and disables further writes', flush=True)

    disk = folder / 'flush-error.img'; shutil.copyfile(good, disk)
    conf = folder / 'flush.conf'
    conf.write_text('[inject-error]\nevent = "flush_to_disk"\niotype = "flush"\nerrno = "5"\nonce = "off"\n')
    drive = ['-drive', f'file=blkdebug:{conf}:{disk},format=raw,if=ide,index=0,werror=report']
    with Guest(qemu, 64, drive_args=drive) as g:
        g.command('write /disk/fail.txt x', 'Error errno=5')
        g.command('write /disk/again.txt x', 'Error errno=30')
    after = FatImage(disk)
    assert not after.fat[1] & (1 << 27), 'Flush failure incorrectly marked volume clean'
    before = digest(disk)
    with Guest(qemu, 64, disk) as g:
        g.command('df', 'FAT32 read-only')
        g.command('cat /disk/note.txt', '\nx\n')
        g.command('write /disk/again.txt x', 'Error errno=30')
    assert digest(disk) == before
    print('PASS storage: injected flush failure leaves dirty state; next boot mounts read-only', flush=True)


def foreign_entries(qemu, folder):
    disk = folder / 'end-marker.img'; create(disk)
    image = FatImage(disk)
    image.set_entries({2: 3, 3: 0x0FFFFFFF})
    with disk.open('r+b') as f:
        for i in range(14):
            e = bytearray(32)
            e[:11] = f'F{i:02}'.ljust(8).encode() + b'TXT'; e[11] = 0x20
            f.seek(image.data * 512 + (i + 1) * 32); f.write(e)
        ghost = bytearray(32); ghost[:11] = b'PHANTOM TXT'; ghost[11] = 0x20
        f.seek((image.data + 1) * 512); f.write(ghost)
    with Guest(qemu, 64, disk) as g:
        g.command('write /disk/new.txt x', 'Wrote bytes=1')
        text = g.command('ls /disk', 'NEW.TXT')
        assert 'PHANTOM' not in text, text
    image = FatImage(disk); image.check()
    assert 'PHANTOM.TXT' not in image.files()

    disk = folder / 'lfn.img'; create(disk)
    image = FatImage(disk); image.set_entries({3: 0x0FFFFFFF})
    alias = b'ALIAS   TXT'
    checksum = 0
    for byte in alias: checksum = (((checksum & 1) << 7) | (checksum >> 1)) + byte; checksum &= 255
    lfn = bytearray(b'\xff' * 32)
    lfn[0] = 0x41; lfn[11] = 0x0F; lfn[12] = 0; lfn[13] = checksum
    struct.pack_into('<H', lfn, 26, 0)
    positions = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
    name = 'LongName.txt\0'
    for pos, char in zip(positions, name): struct.pack_into('<H', lfn, pos, ord(char))
    e = bytearray(32); e[:11] = alias; e[11] = 0x20
    struct.pack_into('<H', e, 26, 3); struct.pack_into('<I', e, 28, 5)
    with disk.open('r+b') as f:
        f.seek(image.data * 512 + 32); f.write(lfn); f.write(e)
        f.seek((image.data + 1) * 512); f.write(b'alias')
    before = digest(disk)
    with Guest(qemu, 64, disk) as g:
        g.command('cat /disk/alias.txt', '\nalias\n')
        g.command('write /disk/alias.txt x', 'Error errno=95')
        g.command('rm /disk/alias.txt', 'Error errno=95')
    assert digest(disk) == before

    disk = folder / 'file-cycle.img'; create(disk)
    image = FatImage(disk); image.set_entries({3: 4, 4: 3})
    e = bytearray(32); e[:11] = b'BAD     TXT'; e[11] = 0x20
    struct.pack_into('<H', e, 26, 3); struct.pack_into('<I', e, 28, 1536)
    with disk.open('r+b') as f: f.seek(image.data * 512 + 32); f.write(e)
    before = digest(disk)
    with Guest(qemu, 64, disk) as g:
        g.command('cat /disk/bad.txt', 'Error errno=5')
        g.command('write /disk/no.txt x', 'Error errno=30')
    assert digest(disk) == before
    disk = folder / 'root-alias.img'; create(disk)
    image = FatImage(disk)
    e = bytearray(32); e[:11] = b'ROOTREF TXT'; e[11] = 0x20
    struct.pack_into('<H', e, 26, image.root); struct.pack_into('<I', e, 28, 512)
    with disk.open('r+b') as f: f.seek(image.data * 512 + 32); f.write(e)
    before = digest(disk)
    with Guest(qemu, 64, disk) as g:
        g.command('cat /disk/rootref.txt', 'Error errno=5')
        g.command('rm /disk/rootref.txt', 'Error errno=30')
    assert digest(disk) == before
    print('PASS storage: directory end markers, LFN alias protection, cyclic file/root alias rejection', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-i386')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='minios-storage-') as tmp:
        folder = Path(tmp)
        good = persistent(args.qemu, folder)
        malformed(args.qemu, folder)
        full_volume(args.qemu, folder)
        partitioned(args.qemu, folder)
        io_failure(args.qemu, folder, good)
        foreign_entries(args.qemu, folder)
        fsck = shutil.which('fsck.fat')
        if fsck:
            import subprocess
            subprocess.run([fsck, '-n', str(good)], check=True)
            print('PASS independent fsck.fat -n', flush=True)
        else:
            print('Optional fsck.fat not installed; host chain/mirror/ownership validation passed', flush=True)


if __name__ == '__main__':
    main()
