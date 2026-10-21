#!/usr/bin/env python3
"""Parent/child capability RPC in QEMU, with pool pressure and repeated cleanup."""
import argparse
import re
import socket
import threading
import time
from test_boot import VM, ROOT, READY


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
        def command(text, expected):
            start = len(vm.output())
            uart.sendall((text + '\n').encode())
            deadline = time.monotonic() + 12
            while True:
                output = vm.output()[start:]
                if expected in output and b'x64> ' in output:
                    return output
                assert time.monotonic() < deadline, output
                time.sleep(.01)
        def free():
            return int(re.search(rb'Frames free=(\d+)', command('mem', b'Frames free='))[1])
        initial = free()
        for _ in range(3):
            command('run ipc', b'[exit] 0')
            command('ps', b'(no user processes)')
            assert free() == initial, 'A grant, endpoint, process handle or wait pin leaked frames'
        # Two unrelated non-yielding jobs leave exactly enough slots for RPC's
        # parent and child; both must keep progressing across the blocking calls.
        pids = []
        for name in ('probe-a', 'probe-b'):
            out = command('spawn ' + name, b'Spawned pid=')
            pids.append(int(re.search(rb'Spawned pid=(\d+)', out)[1]))
        before = {int(pid): int(n) for pid, n in re.findall(rb'pid=(\d+) ready slices=(\d+)', command('ps', b' ready slices='))}
        command('run ipc', b'[exit] 0')
        after = {int(pid): int(n) for pid, n in re.findall(rb'pid=(\d+) ready slices=(\d+)', command('ps', b' ready slices='))}
        assert all(after[pid] > before[pid] for pid in pids), (before, after)
        for pid in pids:
            command('kill ' + str(pid), b'Killed process')
        command('ps', b'(no user processes)')
        assert free() == initial
        print(f'PASS IPC QEMU: {ram} MiB; 32 typed RPCs/run, attenuated startup grants, wait/timeout/EOF, process controls, killed waiters, orphan cleanup and CPU competition', flush=True)
    finally:
        try:
            uart.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        uart.close(); vm.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    args = parser.parse_args()
    for ram in (2, 16, 64):
        run(args.qemu, ram)


if __name__ == '__main__':
    main()
