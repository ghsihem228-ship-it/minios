# Step 2: x86_64 exceptions and timer interrupts

This independent **C11/x86_64** kernel now installs an IDT, reports fatal CPU
exceptions over COM1, resumes a controlled breakpoint, and handles periodic
hardware interrupts. This is the second incremental milestone. The repository
root still builds the older MiniOS i386; run these commands **inside `starter/`**.
[Step 1's original boot design](docs/01-boot.md) is retained as historical context.

## Design alternatives and recommendation

| Decision | Alternative A | Alternative B | Choice for this step |
| --- | --- | --- | --- |
| Exception entry | Explicit NASM stubs, a documented stack frame and ordinary C dispatcher | Compiler-specific interrupt functions, with compiler restrictions and separate error-code signatures | NASM: all register saves, stack alignment and IRETQ are explicit and testable |
| Gate type | Interrupt gate clears IF, limiting ordinary IRQ nesting | Trap gate preserves IF, useful for some debugger policies but permits IRQ reentry | DPL0 interrupt gates throughout; the controlled INT3 still resumes correctly |
| Interrupt controller / clock | Legacy PIC + PIT, straightforward on QEMU's PC machine | Local/I/O APIC and a local APIC or other modern timer, suited to SMP but needs CPU/platform discovery | PIC + PIT for a verified single-CPU boundary; APIC comes later |
| Critical exception stack | Reuse the interrupted kernel stack | Dedicated IST stacks through a 64-bit TSS | Separate ISTs for double fault, NMI and machine check, so a damaged normal stack need not prevent diagnosis |

The first option is not a production hardware choice: the interrupt dispatcher
will need a controller abstraction before APIC, SMP and user-space drivers.
No scheduler, memory allocator, user syscall, stable user ABI or capabilities
are introduced here. A timer tick is a prerequisite for scheduling, not itself
a scheduler. Early diagnostics are fixed kernel messages, not the later public
structured error-chain interface.

## Concrete specification and invariants

The working code is in `boot/interrupts.asm`, `kernel/interrupts.c` and
`kernel/interrupts.h`. No external libraries are linked.

```c
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

void interrupts_init(void);              /* once, bootstrap CPU, IF=0 */
void interrupt_dispatch(struct interrupt_frame *frame); /* assembly entry only */
int interrupt_selftest(void);            /* boot-only, returns with IF=0 */
uint64_t timer_now(void);                /* delivered IRQ0 count, not wall time */
void timer_wait(uint64_t ticks);         /* boot CPU, exits with IF=1 */
```

These are **internal kernel functions**, not syscall signatures. There is still
no user syscall boundary to specify or freeze.

1. The IDT has 256 packed, 16-byte entries; IDTR.limit is 4095. Every entry
   uses ring-0 code selector `0x08`, type/attributes `0x8e` (present, DPL0,
   64-bit interrupt gate). Reserved bits are zero; software from a future
   ring 3 cannot use these gates as an unrestricted kernel entry.
2. The runtime GDT keeps code/data at selectors `0x08`/`0x10`. A 16-byte TSS
   descriptor at `0x18` refers to a packed 104-byte TSS, loaded with LTR.
   IST1/2/3 point to separate, static, 16-KiB, 16-byte-aligned stacks for
   vectors 8/2/18. I/O bitmap offset is 104, beyond the descriptor limit;
   RSP0 is unused until a future user-mode stage.
3. Error-code exceptions are vectors 8, 10–14, 17, 21, 29 and 30, when the
   relevant CPU features exist. Other stubs push a synthetic zero. Stubs then
   push the vector and all 15 GPRs. In 64-bit mode the CPU saves SS:RSP even
   at the same privilege, giving this frame 176 bytes. Static assertions pin
   its size and key offsets. Do **not** test an error-code vector with `INT n`:
   that instruction does not manufacture a hardware exception error code.
4. Assembly clears DF, passes a valid kernel frame pointer to C with the SysV
   stack alignment, restores every GPR, removes only vector/error and returns
   with IRETQ. The interrupted RFLAGS and stack pointer are restored by IRETQ.
   No red zone or SIMD/x87 state is used; none of that state is saved here.
5. Only the explicitly identified INT3 self-test site is resumable. INT3's
   saved RIP already points after INT3. All other CPU exceptions/unexpected
   vectors print vector, error code, RIP, CS, RFLAGS, RSP and SS; page faults
   additionally print CR2. The kernel then executes CLI/HLT without returning
   or advancing the faulting RIP. This prevents repeated-fault loops.
6. The two 8259 PICs map IRQ0–15 to vectors 32–47. Only IRQ0 is unmasked.
   IRQ handlers issue EOI after service, slave before master when needed.
   Spurious IRQ7 gets no EOI; spurious IRQ15 gets only a master EOI. Other
   unconfigured real IRQs are masked before acknowledging them.
7. PIT channel 0 uses mode 2 and divisor 11932: approximately 100 Hz, not an
   exact wall clock. The ISR increments one aligned 64-bit counter without
   allocation, console output or blocking. Volatile access suffices only for
   this single CPU; it is not SMP synchronization. Masked/delayed IRQs can be
   coalesced, so this counter cannot guarantee elapsed real time.
8. Initialization leaves IF clear. The boot test verifies register restoration
   with INT3, briefly enables IRQs for a register-preservation probe, then
   disables them again. `timer_wait` uses CLI/check followed by adjacent
   `STI; HLT` to avoid sleeping after a tick raced with its condition check.
   It returns with interrupts enabled; normal idle uses HLT with IF=1.

## Build and run the actual code

Required Ubuntu/WSL tools remain GCC/binutils, Make, NASM, Python 3.8+ and QEMU:

```bash
sudo apt install build-essential binutils nasm qemu-system-x86 python3
cd ~/minios
git pull --ff-only origin main
cd starter
make
make test
make run
```

Normal serial output:

```text
Hello
IDT ready: 256 gates; TSS/IST ready
PASS INT3/IRQ0: 64-bit GPRs, DF/IF, IRETQ; spurious IRQ7/15
PASS PIT: IRQ0 ticking at ~100 Hz
READY: interrupts enabled; idle
```

After READY the CPU idles and wakes for timer interrupts; silence is expected.
Exit QEMU with **Ctrl+C**. There is no interactive guest terminal yet. No disk,
network, GUI, host serial device or external service is required. As in step 1,
boot the flat `build/kernel.bin`; `build/kernel.elf` contains debugger symbols.
`make clean` removes only this starter's build directory.

```text
starter/
  boot/entry.asm         unchanged Multiboot/long-mode bootstrap
  boot/interrupts.asm    256 stubs, GDT/TSS activation, save/restore and probes
  kernel/main.c          boot sequence, self-tests, idle
  kernel/interrupts.h    internal frame and function contract
  kernel/interrupts.c    IDT/TSS, panic diagnostics, PIC/PIT and dispatch
  kernel/io.h            port-I/O helpers
  kernel/serial.c        bounded polling serial writer
  tests/test_boot.py     actual QEMU/QMP CPU, memory, IRQ and fault checks
```

The working C boot sequence is deliberately short:

```c
serial_init();
serial_write("Hello\n");
interrupts_init();
/* main.c checks interrupt_selftest() and halts on failure. */
timer_wait(3);
for (;;) __asm__ volatile ("hlt" ::: "memory");
```

See the complete source for checked test output and test-only fault selection.

## Tests and debugging

`make test` builds a normal image and four separate fault images, then runs nine
QEMU TCG scenarios. It checks the Multiboot checksum/load bounds and CPU mode as
before, with IF now **set during normal idle**.

- At 16/64/128 MiB, the boot probe loads distinct full-width values into all
  15 GPRs. INT3 must preserve them and saved DF; C must see DF clear. A real
  timer IRQ must preserve the GPRs and interrupted IF as well. Software probes
  exercise the spurious IRQ7/15 return paths with IF clear.
- QMP pauses the VM and reads all 256 actual IDT gates, TSS stack pointers,
  IDTR/TR registers and PIC masks. It reads the tick counter twice across
  continued execution and requires more IRQs, catching a missing EOI or an
  accidentally disabled interrupt flag. Exact serial output excludes resets
  and duplicate boot messages.
- Genuine UD2 (#UD, vector 6, synthetic error 0), invalid DS selector (#GP,
  vector 13, hardware error `0x28`) and an unmapped read at `0x40000000`
  (#PF, vector 14, error 0, CR2 matches) must halt with the correct frame.
- The double-fault image deliberately makes the #GP gate non-present before
  the same selector fault. #GP delivery raises #NP, causing #DF (vector 8,
  hardware error 0). The handler verifies that its frame is on its IST stack.
  No real page table is corrupted to arrange this test.
- QMP injects an actual NMI; its separate IST is verified. The unsupported
  Pentium III test must still print the early serial error before long mode.

Fault tests require one diagnostic, IF=0, a frozen tick counter and no repeated
output. QEMU must remain running in the halt loop rather than stop on a triple
fault. Each VM has bounded host deadlines and is terminated by its test.
`build/fault-*.bin` intentionally halt and are test artifacts, not normal boot
images. Machine-check gate/stack configuration is inspected, but **actual
machine-check injection and recovery are not tested or implemented**.

Packed-layout assertions are compile-time checks; meaningful entry/return
validation requires a CPU, so it uses QEMU rather than mocked C frame tests.
There is no untrusted parser here to fuzz. Random synthetic pointers into the
C dispatcher would bypass architectural entry and would not prove safety.
Later memory-map/syscall parsers should get host unit tests and fuzz targets;
CPU fault scenarios remain targeted emulator tests.

Optional debugging: `make debug`, then `gdb build/kernel.elf` in another terminal:

```gdb
target remote 127.0.0.1:1234
hbreak interrupt_dispatch
continue
info registers
```

The debug listener binds loopback port 1234 and stops at reset. GDB was not
installed in the validation environment; these tests use QMP instead.

## Risks, security pitfalls and limits

- IF must stay clear until IDT/TSS/PIC setup is complete. CLI does not mask
  NMI or prevent faults; an early bootstrap NMI/fault can still fail before
  the runtime IDT exists. There is no claim of full boot-time fault tolerance.
- Wrong vector/error normalization, a truncated 64-bit address, an ABI stack
  mismatch, ordinary RET instead of IRETQ or forgetting EOI can silently
  corrupt registers, triple-fault or stop future interrupts. Gate DPL must
  not be raised casually when user mode is added.
- IST stacks are static and have **no guard pages yet**. Reentrant use of the
  same IST can overwrite its earlier frame; this implementation treats NMI,
  #DF and #MC as terminal, not recoverable or nestable services. A recursion
  flag avoids blocking on logging locks but is not a general NMI-safe logger.
- Fatal paths use bounded UART polling, no heap and no blocking lock. A broken
  UART still prevents diagnosis. Raw kernel addresses are acceptable for this
  local development console; they must not become unprivileged disclosures.
- There is one CPU, one bootstrap stack and no user entry, SWAPGS, FPU/SIMD
  context management, APIC routing or interrupt-safe scheduler/driver API.
  `timer_wait` is a boot helper, not a sleep syscall or general IRQ-state
  preserving primitive. Never call it from an ISR or a critical section.
- The broad RWX bootstrap identity map is unchanged. Memory-map parsing,
  allocation, page protections and stack guards belong to step 3. TSS/IST
  improves diagnosis; it does not provide process isolation by itself.

Step 2 ends here. Wait for an explicit **next** before step 3: paging and memory
management. Do not start scheduling or capability design yet.
