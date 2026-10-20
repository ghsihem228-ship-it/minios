# Validation for milestones 8–13

The final source was verified with:

```bash
export PATH=/workspace/.minios-tools/bin:$PATH
make -C starter -j2 test
make -C starter test-extended
```

A clean parallel build was also exercised during integration. One historical #GP
probe needed updating from selector 0x28 to 0x38 because the added ring-3 GDT
entries made 0x28 valid; both the probe and expected hardware error now agree.

The complete suite passed QEMU at 2/16/64/128/512 MiB, genuine kernel exceptions,
user execution/fault/timeout cases, NMI/IST checks, protected page-table and frame
ownership checks, invalid boot/CPU rejection and Multiboot module preservation.
Every normal boot requires the extended feature PASS marker before READY, so a
userspace-demo failure cannot silently count as an earlier-milestone success.

All host harnesses passed ASan/UBSan with leak checking: memory map/frame model,
handle lifetimes, rights derivation, scalar dispatch, VFS/RAMFS, transaction staging,
ELF validation and typed-frame validation. The extended harness uses 100,000 mutated
ELF/wire rounds; existing harnesses retain their previous bounded model/fuzz cases.
The typed-pipe producer/consumer test runs as two separately granted kernel tasks;
the user program separately exchanges a record through real ring-3 syscalls.

This validates the stated starter subsets. It is not POSIX conformance testing,
production security certification, proof of SMP correctness, crash/power-failure
testing, a coverage-guided fuzz campaign, or validation of a user-mode filesystem
server. Those features are not implemented and must not be inferred from PASS.

## Interactive console and snapshot storage (2026-10-20)

`make test` passes with the VGA/serial shell enabled: original multi-RAM boot,
CPU exception and protection checks; all host sanitizer/model suites; 100000
snapshot wire mutants; QMP PS/2 input, editing/overflow, VGA scrolling, serial
input, guest fault/timeout recovery and frame accounting; real ATA save/reboot,
independent image decoding, fallback from a damaged newer snapshot, and refusal
to modify unrecoverably corrupt/unknown images. Storage tests use temporary
images exclusively. This does not establish physical power-loss safety or native
Windows GUI compatibility; the latter helper has only been reviewed here.
