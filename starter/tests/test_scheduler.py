#!/usr/bin/env python3
"""Boot real scheduler on QEMU; reuse bounded QMP VM lifecycle helpers."""
import argparse
from test_boot import VM, ROOT, READY, image_check

parser = argparse.ArgumentParser()
parser.add_argument('--qemu', default='qemu-system-x86_64')
args = parser.parse_args()
image = ROOT / 'build/kernel.bin'
image_check(image)
for ram in (16, 64, 128):
    vm = VM(args.qemu, image, ram=ram)
    try:
        vm.wait_for(b'READY: interrupts enabled; idle\r\n')
        assert vm.output() == READY, vm.output()
        assert vm.process.poll() is None, 'QEMU stopped unexpectedly'
        print(f'PASS scheduler QEMU: {ram} MiB')
    finally:
        vm.close()
