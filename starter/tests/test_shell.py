#!/usr/bin/env python3
"""Real PS/2 and UART input, VGA output, command safety and user fault recovery."""
import argparse
import re
import socket
import time
import threading
from test_boot import VM, ROOT, READY


def keys(vm, text):
    codes = {' ': 'spc', '\n': 'ret', '\b': 'backspace', '/': 'slash', '.': 'dot', '-': 'minus'}
    for char in text:
        key = codes.get(char, char.lower())
        sequence = [{'type': 'qcode', 'data': key}]
        if char.isupper():
            sequence.insert(0, {'type': 'qcode', 'data': 'shift'})
        vm.qmp('send-key', keys=sequence, **{'hold-time': 10})
        time.sleep(.025)


def command(vm, text, expected, sender=None):
    start = len(vm.output())
    (sender or (lambda data: keys(vm, data)))(text + '\n')
    deadline = time.monotonic() + 8
    while b'x64> ' not in vm.output()[start:]:
        if time.monotonic() > deadline:
            raise AssertionError(vm.output()[start:])
        time.sleep(.01)
    output = vm.output()[start:]
    assert expected in output, output
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    args = parser.parse_args()
    with VM(args.qemu, ROOT / 'build/kernel.bin') as vm:
        vm.wait_for(READY)
        vm.connect()
        command(vm, 'help', b'MiniOS64 commands:')
        command(vm, 'version', b'MiniOS64 x86_64')
        command(vm, 'write note Hello world', b'Wrote bytes=11')
        command(vm, 'cat note', b'\r\nHello world\r\n')
        command(vm, 'ls', b'note bytes=11')
        command(vm, 'cat missing', b'file not found')
        command(vm, 'write ../bad text', b'invalid name')
        command(vm, 'verx\bsion', b'MiniOS64 x86_64')
        command(vm, 'x' * 140, b'Line too long; command discarded')
        first = command(vm, 'mem', b'Frames free=')
        free = int(re.search(rb'Frames free=(\d+)', first)[1])
        command(vm, 'run demo', b'[exit] 0')
        command(vm, 'run fault', b'[exit] -1014')
        command(vm, 'run spin', b'[exit] -2000')
        last = command(vm, 'mem', b'Frames free=')
        assert int(re.search(rb'Frames free=(\d+)', last)[1]) == free, last
        command(vm, 'cat note', b'\r\nHello world\r\n')
        command(vm, 'rm note', b'Removed file')
        command(vm, 'cat note', b'file not found')
        command(vm, 'clear', b'x64> ')
        screen = vm.memory(0xb8000, 4000)[::2]
        assert screen.startswith(b'x64> '), screen
        # Scroll past a full display, then require the prompt to remain visible.
        for _ in range(4):
            command(vm, 'help', b'MiniOS64 commands:')
        assert b'x64> ' in vm.memory(0xb8000, 4000)[::2]
        print('PASS x64 PS/2/VGA shell: files, editing, overflow, scrolling, ELF, fault/timeout recovery, frame cleanup', flush=True)
    vm = VM(args.qemu, ROOT / 'build/kernel.bin', serial_input=True)
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
        reader = threading.Thread(target=drain, daemon=True)
        reader.start()
        vm.wait_for(b'x64> ')
        command(vm, 'version', b'MiniOS64 x86_64', sender=lambda data: uart.sendall(data.encode()))
        command(vm, 'write serial uart works', b'Wrote bytes=10', sender=lambda data: uart.sendall(data.encode()))
        command(vm, 'cat serial', b'\r\nuart works\r\n', sender=lambda data: uart.sendall(data.encode()))
        print('PASS x64 UART shell: input and file commands through emulated serial hardware', flush=True)
    finally:
        uart.shutdown(socket.SHUT_RDWR)
        uart.close()
        vm.close()


if __name__ == '__main__':
    main()
