#!/usr/bin/env python3
"""Actual concurrent ring-3 execution, isolated CR3 roots and shell lifecycle."""
import argparse
import re
import socket
import struct
import threading
import time
from test_boot import VM, ROOT, READY, symbols, reg, ADDRESS_MASK, NX


def run(qemu, ram):
    vm = VM(qemu, ROOT / 'build/kernel.bin', ram=ram, serial_input=True)
    uart = socket.socket(socket.AF_UNIX)
    try:
        deadline = time.monotonic() + 5
        while not (vm.folder / 'uart').exists():
            assert time.monotonic() < deadline
            time.sleep(.01)
        uart.connect(str(vm.folder / 'uart'))
        def drain():
            try:
                while uart.recv(4096):
                    pass
            except OSError:
                pass
        threading.Thread(target=drain, daemon=True).start()
        vm.wait_for(READY)
        vm.connect()
        def command(text, expected):
            start = len(vm.output())
            uart.sendall((text + '\n').encode())
            deadline = time.monotonic() + 9
            while True:
                output = vm.output()[start:]
                if expected in output and b'x64> ' in output:
                    return output
                assert time.monotonic() < deadline, output
                time.sleep(.01)
        def spawn(name):
            out = command('spawn ' + name, b'Spawned pid=')
            return int(re.search(rb'Spawned pid=(\d+)', out)[1])
        def processes():
            out = command('ps', b' ready slices=')
            return {int(pid): int(count) for pid, count in re.findall(rb'pid=(\d+) ready slices=(\d+)', out)}
        def free():
            return int(re.search(rb'Frames free=(\d+)', command('mem', b'Frames free='))[1])
        baseline = free()
        a, b = spawn('probe-a'), spawn('probe-b')
        before = processes()
        time.sleep(.15)
        after = processes()
        assert after[a] > before[a] and after[b] > before[b], (before, after)
        # Independently walk live hardware CR3 roots and inspect physical backing.
        names = symbols(ROOT / 'build/kernel.bin')
        kernel_root, = struct.unpack('<Q', vm.memory(names['root_physical'], 8))
        observed = {}
        def word(address):
            return struct.unpack('<Q', vm.memory(address, 8))[0]
        def leaf(root, va):
            physical = root
            for shift in (39, 30, 21, 12):
                entry = word(physical + ((va >> shift) & 511) * 8)
                assert entry & 1 and not entry & 0x80
                if va >= 0x40000000:
                    assert entry & 4, 'Missing user access in private branch'
                physical = entry & ADDRESS_MASK
            return entry
        for _ in range(100):
            vm.qmp('stop')
            registers = vm.registers()
            if re.search(r'CS =002b', registers):
                root = reg(registers, 'CR3')
                assert root != kernel_root
                text = leaf(root, 0x40000000)
                data = leaf(root, 0x40001000)
                stack = leaf(root, 0x4000f000)
                assert not text & (2 | NX)
                assert data & (2 | NX) == 2 | NX and stack & (2 | NX) == 2 | NX
                kernel = leaf(root, names['__text_start'])
                assert not kernel & 4, 'Kernel text exposed to user'
                count, marker = struct.unpack('<2Q', vm.memory(data & ADDRESS_MASK, 16))
                assert marker in (9, 10) and count > 0
                observed[marker] = (root, data & ADDRESS_MASK, count)
            vm.qmp('cont')
            if len(observed) == 2:
                break
            time.sleep(.003)
        assert len(observed) == 2 and observed[9][0] != observed[10][0] and observed[9][1] != observed[10][1], observed
        c, d = spawn('spin'), spawn('spin')
        command('spawn spin', b'capacity exhausted')
        assert set(processes()) == {a, b, c, d}
        command('kill 0', b'file not found')
        command('kill 18446744073709551616', b'Invalid process ID')
        command('kill ' + str(c), b'Killed process')
        command('kill ' + str(c), b'file not found')
        # An unprivileged fault in a reused slot must leave the other CPU-bound jobs alive.
        command('spawn fault', b'Spawned pid=')
        vm.wait_for(b'exit=-1014')
        assert {a, b, d}.issubset(processes())
        prior = processes()
        command('run demo', b'[exit] 0')
        assert all(processes()[pid] > prior[pid] for pid in (a, b, d))
        command('cat demo', b'\r\ncommit\r\n')
        spawn('txspin')
        vm.wait_for(b'] exit=-2000')
        command('cat demo', b'\r\ncommit\r\n')
        assert {a, b, d}.issubset(processes()), 'Transaction watchdog damaged another process'
        for pid in (a, b, d):
            command('kill ' + str(pid), b'Killed process')
        command('ps', b'(no user processes)')
        # Repeated reuse, exit/fault and transaction abort must release all table frames.
        for name in ('empty', 'abort', 'readonly', 'demo'):
            expected = b'[exit] -1014' if name == 'readonly' else b'[exit] 0'
            command('run ' + name, expected)
        assert free() == baseline, 'User frames or private table frames leaked'
        print(f'PASS processes QEMU: {ram} MiB; live CR3/physical isolation, CPU progress, capacity/reuse, kill/fault recovery, transaction cleanup', flush=True)
    finally:
        try:
            uart.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        uart.close()
        vm.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    args = parser.parse_args()
    for ram in (16, 64):
        run(args.qemu, ram)


if __name__ == '__main__':
    main()
