#!/usr/bin/env python3
"""Boot the actual kernel under QEMU/TCG and inspect VGA through QMP.

No third-party Python packages, GUI, KVM, serial driver or guest test hooks.
Run from any directory: python3 tests/smoke.py [--qemu PATH] [--ram 2 64 128]
"""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


class Guest:
    def __init__(self, qemu, ram):
        self.tmp = tempfile.TemporaryDirectory(prefix="minios-test-")
        self.folder = Path(self.tmp.name)
        self.log = (self.folder / "qemu.log").open("w+")
        self.proc = subprocess.Popen(
            [qemu, "-kernel", str(ROOT / "build/minios.bin"), "-m", str(ram),
             "-accel", "tcg", "-display", "none", "-serial", "none",
             "-monitor", "none", "-nic", "none", "-no-reboot",
             "-qmp", f"unix:{self.folder / 'qmp'},server=on,wait=off"],
            stdout=self.log, stderr=self.log)
        self.sock = None
        self.stream = None

    def __enter__(self):
        try:
            deadline = time.monotonic() + 15
            while not (self.folder / "qmp").exists():
                if self.proc.poll() is not None:
                    self.log.seek(0)
                    raise RuntimeError(self.log.read())
                if time.monotonic() >= deadline:
                    raise TimeoutError("QMP socket did not appear")
                time.sleep(0.05)
            self.sock = socket.socket(socket.AF_UNIX)
            self.sock.settimeout(10)
            self.sock.connect(str(self.folder / "qmp"))
            self.stream = self.sock.makefile("rwb", buffering=0)
            json.loads(self.stream.readline())
            self.qmp("qmp_capabilities")
            self.wait_for("minios>")
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if self.stream:
            self.stream.close()
        if self.sock:
            self.sock.close()
        self.log.close()
        self.tmp.cleanup()

    def qmp(self, name, **args):
        self.stream.write((json.dumps({"execute": name, "arguments": args}) + "\n").encode())
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QEMU closed QMP unexpectedly")
            reply = json.loads(line)
            if "error" in reply:
                raise RuntimeError(reply)
            if "return" in reply:
                return reply["return"]

    def monitor(self, command):
        return self.qmp("human-monitor-command", **{"command-line": command})

    def screen(self):
        target = self.folder / "vga.bin"
        result = self.monitor(f"pmemsave 0xb8000 4000 {json.dumps(str(target))}")
        if result:
            raise RuntimeError(result)
        raw = target.read_bytes()[::2]
        return "\n".join(raw[i:i + 80].decode("ascii", errors="replace").rstrip()
                         for i in range(0, 2000, 80))

    def wait_for(self, text, timeout=15):
        deadline = time.monotonic() + timeout
        while True:
            screen = self.screen()
            if text in screen:
                return screen
            if time.monotonic() >= deadline:
                raise AssertionError(f"Timed out waiting for {text!r}:\n{screen}")
            time.sleep(0.05)

    def type(self, text):
        codes = {" ": "spc", "\n": "ret", "\b": "backspace", "\t": "tab", "\x1b": "esc",
                 "/": "slash", ".": "dot", "-": "minus", "=": "equal"}
        for char in text:
            code = codes.get(char, char)
            self.qmp("send-key", keys=[{"type": "qcode", "data": code}], **{"hold-time": 10})
            time.sleep(0.025)

    def command(self, text, expected):
        # A fresh screen makes every assertion belong to this command.
        self.type("clear\n")
        self.wait_for("minios>")
        self.type(text + "\n")
        screen = self.wait_for(expected)
        assert "[FAIL]" not in screen and "CPU EXCEPTION" not in screen, screen
        return screen


def process_checks(guest, full):
    def free_frames():
        screen = guest.command("mem", "Paging: ON")
        return int(re.search(r"PMM: total=\d+ free=(\d+)", screen).group(1))

    def run_program(path, marker, status=0):
        screen = guest.command("run " + path, "[exit]")
        assert marker in screen and f"status={status}\n" in screen, screen
        return screen

    def spawn(path):
        screen = guest.command("spawn " + path, "Started PID")
        return int(re.search(r"Started PID (\d+)", screen).group(1))

    def ps():
        screen = guest.command("ps", "PID STATE")
        rows = re.findall(r"^(\d+) (ready|running|sleep|zombie) (\d+) (\d+) (\d+|-) (\S+)$",
                          screen, re.MULTILINE)
        return {int(row[0]): row[1:] for row in rows}

    guest.command("trim", "Released heap bytes=")
    before = free_frames()
    guest.command("run /etc/motd", "Error errno=8")
    guest.command("run /missing", "Error errno=2")
    run_program("/bin/hello", "Hello from Ring 3!")
    run_program("/bin/probe", "[user] syscall validation passed")
    run_program("/bin/filedemo", "[user] filesystem roundtrip passed")
    guest.command("cat /tmp/user.txt", "Written by a Ring 3 program.")
    # This would fail EBUSY if process exit did not close its descriptor.
    guest.command("rm /tmp/user.txt", "Removed file.")
    run_program("/bin/sleep", "[user] sleep/wakeup passed")
    screen = run_program("/bin/crash", "user cr2=0x00100000", status=142)
    assert "vector=14 err=0x00000005" in screen, screen
    screen = run_program("/bin/readonly", "user cr2=0x40000000", status=142)
    assert "vector=14 err=0x00000007" in screen, screen
    screen = run_program("/bin/guard", "user cr2=0x403FBFFC", status=142)
    assert "vector=14 err=0x00000004" in screen, screen
    run_program("/bin/io", "vector=13", status=141)
    guest.command("write /tmp/note hello", "Wrote bytes=5")
    guest.command("cat /tmp/note", "\nhello\n")
    guest.command("rm /tmp/note", "Removed file.")
    guest.command("write /etc/motd x", "Error errno=30")
    assert free_frames() == before, "Leaked process frames after exit/fault/failed load"
    if full:
        spins = [spawn("/bin/spin"), spawn("/bin/spin")]
        first = ps()
        time.sleep(0.2)
        later = ps()
        for pid in spins:
            assert int(later[pid][1]) > int(first[pid][1]), (first, later)
            assert int(later[pid][2]) > int(first[pid][2]), (first, later)
        # Two programs touch the same virtual .bss address using distinct PID
        # sentinels. If private frames or CR3 switching break, one exits 1.
        isolates = [spawn("/bin/isolate"), spawn("/bin/isolate")]
        deadline = time.monotonic() + 15
        while True:
            rows = ps()
            if all(rows[pid][0] == "zombie" for pid in isolates): break
            assert time.monotonic() < deadline, rows
        assert all(rows[pid][3] == "0" for pid in isolates), rows
        run_program("/bin/probe", "[user] syscall validation passed")
        for _ in range(5): spins.append(spawn("/bin/spin"))
        guest.command("spawn /bin/hello", "Error errno=11")
        assert before - free_frames() == 6 * 7, "Unexpected frame ownership"
        guest.command("kill 0", "Error errno=1")
        for pid in spins:
            guest.command(f"kill {pid}", f"Killed PID {pid}")
        assert free_frames() == before, "Kill/reap leaked frames"
        for _ in range(8): run_program("/bin/hello", "Hello from Ring 3!")
        assert free_frames() == before, "Slot reuse leaked frames"
        guest.type("clear\nrun /bin/spin\n")
        guest.wait_for("Started PID")
        guest.type("\x1b")
        screen = guest.wait_for("[exit]")
        assert "status=137" in screen, screen
        assert free_frames() == before, "Cancellation leaked frames"


def run(qemu, ram, full):
    with Guest(qemu, ram) as guest:
        boot = guest.screen()
        for text in ("Frame allocator self-test passed", "Paging self-test passed",
                     "Heap self-test passed", "RAMFS self-test passed",
                     "ELF loader self-test passed", "preemptive scheduler ready",
                     f"RAM: {ram - 1} MiB usable"):
            assert text in boot, boot
        guest.command("mem", "Paging: ON; NULL unmapped")
        stats = guest.command("heap", "integrity=OK")
        assert "used=0" in stats and "blocks=1" in stats, stats
        guest.command("page 0", "0x00000000 -> unmapped")
        guest.command("page 0x100000", "0x00100000 -> 0x00100000")
        guest.command("page 0xd0000010", "0xD0000010 -> 0x")
        guest.command("alloc 9001", "Allocated slot 0")
        stats = guest.command("heap", "integrity=OK")
        assert "used=9008" in stats, stats
        guest.command("test", "Free all slots before running tests.")
        guest.command("free 0", "Freed slot 0")
        guest.command("free 0", "Invalid or empty slot.")
        guest.command("alloc 0", "Allocation failed.")
        guest.command("alloc 0xffffffff", "Allocation failed.")
        guest.command("alloc 4294967296", "Invalid number")
        guest.command("free 16", "Invalid or empty slot.")
        stats = guest.command("test", "[PASS] Heap self-test")
        assert "[PASS] Paging self-test" in stats, stats
        if full:
            for slot in range(16):
                guest.command("alloc 0x20", f"Allocated slot {slot} ")
            guest.command("alloc 32", "No free slots.")
            for slot in range(0, 16, 2):
                guest.command(f"free {slot}", f"Freed slot {slot}")
            for slot in range(1, 16, 2):
                guest.command(f"free {slot}", f"Freed slot {slot}")
            stats = guest.command("heap", "integrity=OK")
            assert "used=0" in stats and "blocks=1" in stats, stats
            guest.command("\b\bhep\blp", "alloc <bytes>")
            guest.command("\tmem\t", "Paging: ON")
            guest.command("alloc nope", "Invalid number")
            guest.command("alloc 1 2", "Invalid arguments")
            guest.command("x" * 140, "Line too long; command discarded.")
            guest.command("unknown", "Unknown command.")
            guest.command("mmap", "usable")
            guest.command("page 0xd03fe000", "-> unmapped")
            # Allocate all remaining virtual capacity, then recover.
            committed = int(re.search(r"committed=(\d+)", stats).group(1))
            # One free block can grow up to the configured cap, except on
            # genuinely RAM-limited machines (which still must roll back).
            guest.command("alloc 4186096", "Allocated slot 0")
            guest.command("alloc 16", "Allocation failed.")
            guest.command("free 0", "Freed slot 0")
            stats = guest.command("heap", "integrity=OK")
            assert "committed=4186112" in stats and "blocks=1" in stats, stats
            assert committed <= 4186112
        tick1 = guest.command("uptime", "Uptime ticks")
        first = int(re.search(r"100 Hz\): (\d+)", tick1).group(1))
        time.sleep(0.1)
        tick2 = guest.command("uptime", "Uptime ticks")
        assert int(re.search(r"100 Hz\): (\d+)", tick2).group(1)) > first, tick2
        process_checks(guest, full)
        # Verify real hardware protection and diagnostic handling, last
        # because this deliberate fatal exception halts the guest.
        guest.type("clear\nfault\n")
        fault = guest.wait_for("System halted.")
        assert "Page fault" in fault and "cr2=0x00000000 not-present read supervisor" in fault, fault
        print(f"PASS {ram} MiB: memory, ELF, Ring 3, syscalls, RAMFS, process fault isolation, timer, NULL fault" +
              (", preemption/address-space isolation/limits/reaping/input checks" if full else ""), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--ram", type=int, nargs="+", default=[2, 64, 128])
    args = parser.parse_args()
    if not (ROOT / "build/minios.bin").exists():
        parser.error("Build first with make")
    for ram in args.ram:
        if ram < 2:
            parser.error("At least 2 MiB is needed for the kernel and tests")
        run(args.qemu, ram, full=(ram == 64))


if __name__ == "__main__":
    main()
