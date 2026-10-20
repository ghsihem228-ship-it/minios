#!/usr/bin/env python3
"""Boot the actual flat Multiboot image; check serial and CPU mode via QMP."""
import argparse
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def image_check():
    data = (ROOT / 'build/kernel.bin').read_bytes()
    offset = next((n for n in range(0, min(8192, len(data)) - 31, 4)
                   if struct.unpack_from('<I', data, n)[0] == 0x1BADB002), None)
    assert offset is not None, 'Missing early, aligned Multiboot header'
    magic, flags, check, address, start, end, bss_end, entry = struct.unpack_from('<8I', data, offset)
    assert (magic + flags + check) & 0xffffffff == 0, 'Bad header checksum'
    assert flags == 0x10003 and address == start + offset
    assert start == 0x100000 and end - start == len(data), 'Flat image/load bounds differ'
    assert start <= entry < end < bss_end <= 0x200000
    assert not bss_end % 4096


def boot(qemu, ram, cpu, marker, long_mode):
    with tempfile.TemporaryDirectory(prefix='kernel-step1-') as folder:
        folder = Path(folder)
        serial = folder / 'serial.log'
        endpoint = folder / 'qmp'
        with (folder / 'qemu.log').open('w+') as log:
            p = subprocess.Popen([qemu, '-machine', 'pc', '-cpu', cpu, '-m', str(ram),
                '-accel', 'tcg', '-display', 'none', '-serial', f'file:{serial}',
                '-monitor', 'none', '-nic', 'none', '-no-reboot', '-no-shutdown',
                '-qmp', f'unix:{endpoint},server=on,wait=off',
                '-kernel', str(ROOT / 'build/kernel.bin')], stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 10
                while True:
                    output = serial.read_bytes() if serial.exists() else b''
                    if marker in output and endpoint.exists():
                        break
                    if p.poll() is not None or time.monotonic() >= deadline:
                        log.seek(0)
                        raise AssertionError(f'Boot failed: serial={output!r}; {log.read()}')
                    time.sleep(.02)
                with socket.socket(socket.AF_UNIX) as s:
                    s.settimeout(5)
                    s.connect(str(endpoint))
                    with s.makefile('rwb', buffering=0) as stream:
                        assert 'QMP' in json.loads(stream.readline())
                        def qmp(name, **args):
                            stream.write((json.dumps({'execute': name, 'arguments': args}) + '\n').encode())
                            while True:
                                reply = json.loads(stream.readline())
                                assert 'error' not in reply, reply
                                if 'return' in reply:
                                    return reply['return']
                        qmp('qmp_capabilities')
                        registers = qmp('human-monitor-command', **{'command-line': 'info registers'})
                        flags = re.search(r'\b(?:RFL|EFL)=([0-9a-fA-F]+)', registers)
                        assert flags and not int(flags.group(1), 16) & (1 << 9), registers
                        efer = re.search(r'\bEFER=([0-9a-fA-F]+)', registers)
                        if long_mode:
                            assert efer and int(efer.group(1), 16) & 0x500 == 0x500, registers
                            cr0 = int(re.search(r'\bCR0=([0-9a-fA-F]+)', registers).group(1), 16)
                            cr3 = int(re.search(r'\bCR3=([0-9a-fA-F]+)', registers).group(1), 16)
                            cr4 = int(re.search(r'\bCR4=([0-9a-fA-F]+)', registers).group(1), 16)
                            assert cr0 & (1 << 31) and cr4 & (1 << 5), registers
                            assert cr3 and not cr3 % 4096, registers
                        else:
                            assert not efer or not int(efer.group(1), 16) & (1 << 10), registers
                        # Catch immediate reset/fault loops after the first characters appeared.
                        time.sleep(.05)
                        assert serial.read_bytes() == marker, serial.read_bytes()
                        assert qmp('query-status')['running'], 'VM stopped after serial output'
            finally:
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        p.kill(); p.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    args = parser.parse_args()
    image_check()
    for ram in (16, 64, 128):
        boot(args.qemu, ram, 'qemu64', b'Hello\r\n', True)
        print(f'PASS {ram} MiB: Hello on COM1, long mode, PAE, paging and interrupts disabled')
    boot(args.qemu, 64, 'pentium3', b'Boot error: long mode unsupported\r\n', False)
    print('PASS unsupported CPU: explicit serial error before 64-bit execution')


if __name__ == '__main__':
    main()
