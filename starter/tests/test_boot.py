#!/usr/bin/env python3
"""Exercise real x86_64 exception entry/return and memory protection and PIC/PIT interrupts in QEMU."""
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
BOOT = (b'Hello\r\nIDT ready: 256 gates; TSS/IST ready\r\n'
        b'PASS INT3/IRQ0: 64-bit GPRs, DF/IF, IRETQ; spurious IRQ7/15\r\n'
        b'PASS PMM: memory map, ownership, exhaustion/reuse\r\n'
        b'PASS VMM: 4 KiB, W^X, NULL/stack guards, map/unmap\r\n'
        b'PASS PIT: IRQ0 ticking at ~100 Hz\r\n')
READY = BOOT + b'PASS scheduler: yield, sleep, exit, capacity, reuse\r\n' + b'PASS handles: types, references, stale tokens, capacity, retirement\r\n' + b'PASS syscalls: INT80, registers, ABI, handles, rejection\r\n' + b'PASS capabilities: rights, attenuation, delegation, launch rollback\r\n' + b'PASS extended: errors, RAMFS, ELF64/ring3, POSIX subset, COW undo, typed pipes\r\n' + b'READY: interrupts enabled; idle\r\nMiniOS64 shell. Type help. Use save to keep files.\r\nx64> '


def symbols(image):
    output = subprocess.check_output(['nm', '-n', str(image.with_suffix('.elf'))], text=True)
    return {parts[2]: int(parts[0], 16) for line in output.splitlines()
            if len(parts := line.split()) == 3}


def image_check(image):
    data = image.read_bytes()
    offset = next((n for n in range(0, min(8192, len(data)) - 31, 4)
                   if struct.unpack_from('<I', data, n)[0] == 0x1BADB002), None)
    assert offset is not None, 'Missing early, aligned Multiboot header'
    magic, flags, check, address, start, end, bss_end, entry = struct.unpack_from('<8I', data, offset)
    assert (magic + flags + check) & 0xffffffff == 0, 'Bad header checksum'
    assert flags == 0x10003 and address == start + offset
    assert start == 0x100000 and end - start == len(data), 'Flat image/load bounds differ'
    assert start <= entry < end < bss_end <= 0x200000
    assert not bss_end % 4096


class VM:
    def __init__(self, qemu, image, ram=64, cpu='qemu64', extra=(), serial_input=False):
        self.image = image
        self.folder_handle = tempfile.TemporaryDirectory(prefix='kernel-step3-')
        self.folder = Path(self.folder_handle.name)
        self.serial = self.folder / 'serial.log'
        self.endpoint = self.folder / 'qmp'
        self.log = (self.folder / 'qemu.log').open('w+')
        self.socket = self.stream = self.process = None
        serial_args = ['-serial', f'file:{self.serial}']
        if serial_input:
            serial_args = ['-chardev', f'socket,id=uart,path={self.folder}/uart,server=on,wait=off,logfile={self.serial}', '-serial', 'chardev:uart']
        self.process = subprocess.Popen([
            qemu, '-machine', 'pc', '-cpu', cpu, '-m', str(ram), '-accel', 'tcg',
            '-display', 'none', *serial_args, '-monitor', 'none',
            '-nic', 'none', '-no-reboot', '-no-shutdown',
            '-qmp', f'unix:{self.endpoint},server=on,wait=off', '-kernel', str(image), *extra
        ], stdout=self.log, stderr=self.log)

    def output(self):
        return self.serial.read_bytes() if self.serial.exists() else b''

    def wait_for(self, marker):
        deadline = time.monotonic() + 10
        while marker not in self.output() or not self.endpoint.exists():
            if self.process.poll() is not None or time.monotonic() >= deadline:
                self.log.seek(0)
                raise AssertionError(f'VM failed: serial={self.output()!r}; {self.log.read()}')
            time.sleep(.01)

    def connect(self):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        self.socket.connect(str(self.endpoint))
        self.stream = self.socket.makefile('rwb', buffering=0)
        assert 'QMP' in json.loads(self.stream.readline())
        self.qmp('qmp_capabilities')

    def qmp(self, name, **args):
        self.stream.write((json.dumps({'execute': name, 'arguments': args}) + '\n').encode())
        while True:
            reply = json.loads(self.stream.readline())
            assert 'error' not in reply, reply
            if 'return' in reply:
                return reply['return']

    def registers(self):
        return self.qmp('human-monitor-command', **{'command-line': 'info registers'})

    def memory(self, address, size):
        path = self.folder / 'memory.bin'
        self.qmp('pmemsave', val=address, size=size, filename=str(path))
        data = path.read_bytes()
        assert len(data) == size
        return data

    def close(self):
        if self.stream:
            self.stream.close()
        if self.socket:
            self.socket.close()
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.log.close()
        self.folder_handle.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def reg(text, name):
    match = re.search(r'\b' + name + r'=([0-9a-fA-F]+)', text)
    assert match, text
    return int(match[1], 16)


def check_long_mode(registers):
    assert reg(registers, 'EFER') & 0x500 == 0x500, registers
    assert reg(registers, 'CR0') & (1 << 31), registers
    assert reg(registers, 'CR4') & (1 << 5), registers
    assert reg(registers, 'CR3') and not reg(registers, 'CR3') % 4096, registers


def check_tables(vm, names, registers):
    # Validate all runtime gates, not merely the source's table initializer.
    gates = vm.memory(names['idt'], 4096)
    for n in range(256):
        low, cs, ist, attr, middle, high, reserved = struct.unpack_from('<HHBBHII', gates, n * 16)
        assert low | (middle << 16) | (high << 32) == names[f'isr_{n}'], n
        assert cs == 8 and attr == 0x8e and reserved == 0, n
        assert ist == {8: 1, 2: 2, 18: 3}.get(n, 0), n
    assert re.search(rf'IDT=\s*{names["idt"]:016x}\s+00000fff', registers), registers
    assert re.search(rf'TR =0018\s+{names["tss"]:016x}\s+00000067', registers), registers
    tss = vm.memory(names['tss'], 104)
    for i, stack in enumerate(('df_stack', 'nmi_stack', 'mc_stack')):
        assert struct.unpack_from('<Q', tss, 36 + i * 8)[0] == names[stack] + 16384
    assert struct.unpack_from('<H', tss, 102)[0] == 104
    assert struct.unpack_from('<Q', tss, 4)[0] == 0, 'RSP0 left enabled after user cleanup'
    pic = vm.qmp('human-monitor-command', **{'command-line': 'info pic'})
    assert re.search(r'pic0:.*imr=fe', pic) and re.search(r'pic1:.*imr=ff', pic), pic


ADDRESS_MASK = 0x000ffffffffff000
ARENA = 0xffff800000000000
NX = 1 << 63


def check_memory(vm, names, registers):
    assert reg(registers, 'CR0') & (1 << 16), 'Supervisor write protection disabled'
    assert reg(registers, 'EFER') & (1 << 11), 'NXE disabled'
    root, = struct.unpack('<Q', vm.memory(names['root_physical'], 8))
    assert reg(registers, 'CR3') == root
    count, = struct.unpack('<I', vm.memory(names['frame_count'], 4))
    states = vm.memory(names['frames'], count)
    free, = struct.unpack('<Q', vm.memory(names['free_frames'], 8))
    assert states.count(1) == free and free > 0
    assert states.count(2) == 0 and states.count(4) == 1
    assert count <= 0x40000000 // 4096
    tables = set()
    mappings = {}
    def walk(physical, shift, prefix):
        assert physical not in tables, 'Cyclic or aliased page table'
        tables.add(physical)
        assert states[physical // 4096] == 3, 'Unowned page table'
        entries = struct.unpack('<512Q', vm.memory(physical, 4096))
        for index, entry in enumerate(entries):
            if not entry & 1:
                continue
            assert shift != 12 or not entry & 4, 'User leaf leaked after process cleanup'
            assert not entry & 0x80, 'Unexpected huge page / PAT bit'
            target = entry & ADDRESS_MASK
            virtual = prefix | (index << shift)
            if shift > 12:
                walk(target, shift - 9, virtual)
            else:
                if virtual & (1 << 47):
                    virtual |= 0xffff000000000000
                assert target < 0x40000000 and (virtual == target or virtual == ARENA)
                assert entry & NX or not entry & 2, 'Writable executable leaf'
                mappings[virtual] = entry
    walk(root, 39, 0)
    assert len(tables) == states.count(3)
    for guard in ('__boot_guard', '__df_guard', '__nmi_guard', '__mc_guard', '__syscall_guard'):
        assert names[guard] not in mappings
    assert 0 not in mappings and 0xa0000 not in mappings
    assert 0x40000000 not in mappings
    assert mappings[names['__text_start']] & (NX | 2) == 0
    assert mappings[names['__rodata_start']] & (NX | 2) == NX
    assert mappings[names['frames'] & ~4095] & (NX | 2) == NX | 2
    assert mappings[ARENA] & (NX | 2) == NX | 2
    page = mappings[ARENA] & ADDRESS_MASK
    assert states[page // 4096] == 4
    assert mappings[page] & (NX | 2) == NX | 2
    # The only executable pages must be the linked text range, with no RW alias.
    for virtual, entry in mappings.items():
        if not entry & NX:
            assert names['__text_start'] <= virtual < names['__text_end']
    for table in tables:
        assert mappings[table] & (NX | 2) == NX | 2


def boot(qemu, ram):
    image = ROOT / 'build/kernel.bin'
    names = symbols(image)
    with VM(qemu, image, ram) as vm:
        vm.wait_for(READY)
        vm.connect()
        # Freeze only for inspection. In idle IF should be 1; retry if sampled in ISR.
        for _ in range(10):
            vm.qmp('stop')
            registers = vm.registers()
            if reg(registers, '(?:RFL|EFL)') & (1 << 9):
                break
            vm.qmp('cont')
            time.sleep(.01)
        else:
            raise AssertionError('Maskable interrupts never enabled')
        check_long_mode(registers)
        check_tables(vm, names, registers)
        check_memory(vm, names, registers)
        first, = struct.unpack('<Q', vm.memory(names['timer_ticks'], 8))
        assert first >= 3
        vm.qmp('cont')
        time.sleep(.12)
        vm.qmp('stop')
        second, = struct.unpack('<Q', vm.memory(names['timer_ticks'], 8))
        assert second - first >= 2, (first, second, 'Timer stopped / missing EOI')
        assert vm.output() == READY, vm.output()
        vm.qmp('cont')
        assert vm.qmp('query-status')['running'], 'CPU stopped (possible triple fault)'
    print(f'PASS {ram} MiB: PMM recovery, protected 4-KiB tables, IDT/IST, register preservation, PIT IRQs')


def check_fatal(vm, vector, error, ist=False, cr2=None, execute=False):
    vm.wait_for(b'HALTED\r\n')
    time.sleep(.05)              # Reboot, repeated exception and runaway output check.
    output = vm.output()
    assert b'ERROR' not in output and output.count(b'FATAL ') == 1, output
    assert output.count(b'Hello\r\n') == 1, output
    fields = {key.decode(): int(value, 16) for key, value in
              re.findall(rb'(vector|error|rip|cs|rflags|rsp|ss|cr2)=([0-9a-f]{16})', output)}
    assert fields['vector'] == vector and fields['error'] == error, output
    assert fields['cs'] == 8 and fields['ss'] == 16, output
    assert (fields['rip'] == ARENA if execute else 0x100000 <= fields['rip'] < 0x200000), output
    assert 0x100000 <= fields['rsp'] < 0x200000, output
    assert bool(b'IST stack verified\r\n' in output) == ist, output
    if cr2 is not None:
        assert fields['cr2'] == cr2, output
    registers = vm.registers()
    check_long_mode(registers)
    assert not reg(registers, '(?:RFL|EFL)') & (1 << 9), registers
    address = symbols(vm.image)['timer_ticks']
    ticks = vm.memory(address, 8)
    time.sleep(.05)
    assert vm.output() == output, vm.output()
    assert vm.memory(address, 8) == ticks
    assert vm.qmp('query-status')['running'], 'Triple fault stopped the VM'
    return output


def fault(qemu, vector, error):
    image = ROOT / f'build/fault-{vector}.bin'
    image_check(image)
    with VM(qemu, image) as vm:
        vm.wait_for(b'HALTED\r\n')
        vm.connect()
        output = check_fatal(vm, vector, error, ist=vector == 8,
                             cr2=0x40000000 if vector == 14 else None)
        assert output.startswith(BOOT), output
        assert b'READY:' not in output, output
    print(f'PASS exception {vector}: real CPU fault, error={error:#x}, serial frame, safe halt'
          + (' on dedicated double-fault IST' if vector == 8 else ''))


def nmi(qemu):
    with VM(qemu, ROOT / 'build/kernel.bin') as vm:
        vm.wait_for(READY)
        vm.connect()
        vm.qmp('inject-nmi')
        check_fatal(vm, 2, 0, ist=True)
    print('PASS NMI: QMP injection uses separate IST and halts with diagnostics')


def unsupported(qemu):
    marker = b'Boot error: long mode unsupported\r\n'
    with VM(qemu, ROOT / 'build/kernel.bin', cpu='pentium3') as vm:
        vm.wait_for(marker)
        vm.connect()
        time.sleep(.05)
        assert vm.output() == marker, vm.output()
        registers = vm.registers()
        assert not reg(registers, 'EFER') & (1 << 10), registers
        assert not reg(registers, '(?:RFL|EFL)') & (1 << 9), registers
        assert vm.qmp('query-status')['running']
    print('PASS unsupported CPU: explicit serial error before 64-bit execution')


def memory_fault(qemu, test, vector, error, address=None, ist=False):
    image = ROOT / f'build/fault-{test}.bin'
    names = symbols(image)
    if address == 'text':
        address = names['__text_start']
    if address == 'guard':
        address = names['__boot_guard']
    image_check(image)
    with VM(qemu, image) as vm:
        vm.wait_for(b'HALTED\r\n')
        vm.connect()
        output = check_fatal(vm, vector, error, ist=ist, cr2=address, execute=test == 17)
        assert output.startswith(BOOT) and b'READY:' not in output, output
    print(f'PASS memory fault {test}: vector={vector}, error={error:#x}, CR2/IST match')


def bad_boot(qemu, test):
    image = ROOT / f'build/fault-{test}.bin'
    image_check(image)
    marker = b'Boot error: invalid or insufficient boot memory\r\n'
    with VM(qemu, image) as vm:
        vm.wait_for(marker)
        vm.connect()
        first = vm.output()
        time.sleep(.05)
        assert first == vm.output() and first.endswith(marker), vm.output()
        assert first.count(b'Hello\r\n') == 1 and b'PASS PMM' not in first
        assert not reg(vm.registers(), '(?:RFL|EFL)') & (1 << 9)
        assert vm.qmp('query-status')['running']
    print(f'PASS boot rejection {test}: missing/corrupt memory map fails closed')


def without_nx(qemu):
    marker = b'Boot error: NX unsupported\r\n'
    with VM(qemu, ROOT / 'build/kernel.bin', cpu='qemu64,-nx') as vm:
        vm.wait_for(marker)
        vm.connect()
        time.sleep(.05)
        assert vm.output() == marker, vm.output()
        assert not reg(vm.registers(), 'EFER') & (1 << 10)
        assert vm.qmp('query-status')['running']
    print('PASS CPU without NX: explicit rejection before long mode')


def module(qemu):
    names = symbols(ROOT / 'build/kernel.bin')
    with tempfile.TemporaryDirectory(prefix='kernel-module-') as folder:
        path = Path(folder) / 'payload.bin'
        payload = bytes(range(256)) * 32 + b'!'
        path.write_bytes(payload)
        with VM(qemu, ROOT / 'build/kernel.bin', extra=('-initrd', str(path))) as vm:
            vm.wait_for(READY)
            vm.connect()
            vm.qmp('stop')
            info, = struct.unpack('<I', vm.memory(names['boot_info_address'], 4))
            data = vm.memory(info, 52)
            flags, = struct.unpack_from('<I', data)
            count, table = struct.unpack_from('<II', data, 20)
            assert flags & 8 and count == 1
            start, end = struct.unpack_from('<II', vm.memory(table, 16))
            assert end - start == len(payload) and vm.memory(start, len(payload)) == payload
            for frame in range(start // 4096, (end + 4095) // 4096):
                assert vm.memory(names['frames'] + frame, 1) == b'\0'
            check_memory(vm, names, vm.registers())
            assert vm.output() == READY
    print('PASS Multiboot module: partial edge frames reserved and payload preserved')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    args = parser.parse_args()
    image_check(ROOT / 'build/kernel.bin')
    for ram in (2, 16, 64, 128, 512):
        boot(args.qemu, ram)
    for vector, error in ((6, 0), (13, 0x38), (14, 0), (8, 0)):
        fault(args.qemu, vector, error)
    for test, vector, error, address, ist in (
        (15, 14, 0, 0, False), (16, 14, 3, 'text', False),
        (17, 14, 17, ARENA, False), (18, 14, 0, 'guard', False),
        (19, 8, 0, None, True), (20, 14, 0, ARENA, False)
    ):
        memory_fault(args.qemu, test, vector, error, address, ist)
    nmi(args.qemu)
    unsupported(args.qemu)
    without_nx(args.qemu)
    for test in (100, 101):
        bad_boot(args.qemu, test)
    module(args.qemu)


if __name__ == '__main__':
    main()
