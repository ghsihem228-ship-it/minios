#include "serial.h"
#include "interrupts.h"
#include "vmm.h"
#include "scheduler.h"
#include "syscall.h"
#include "process.h"
#include "pipe.h"
_Static_assert(sizeof(void *) == 8, "This starter requires x86_64");
#ifndef TEST_FAULT
#define TEST_FAULT 0
#endif
extern _Noreturn void memory_test_fault(unsigned test);
void kernel_main(uint32_t boot_info) {
    serial_init();
    (void)serial_write("Hello\n");
    interrupts_init();
    (void)serial_write("IDT ready: 256 gates; TSS/IST ready\n");
    if (!interrupt_selftest()) {
        (void)serial_write("ERROR: interrupt self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS INT3/IRQ0: 64-bit GPRs, DF/IF, IRETQ; spurious IRQ7/15\n");
    /* Fault-image-only metadata corruption exercises fail-closed boot paths. */
    if (TEST_FAULT == 100) *(volatile uint32_t *)(uintptr_t)boot_info &= ~(1u << 6);
    if (TEST_FAULT == 101) {
        uint32_t map = *(const uint32_t *)(uintptr_t)(boot_info + 48);
        *(volatile uint32_t *)(uintptr_t)map = 19;
    }
    if (!memory_init(boot_info)) {
        (void)serial_write("Boot error: invalid or insufficient boot memory\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    if (!memory_selftest()) {
        (void)serial_write("ERROR: memory self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS PMM: memory map, ownership, exhaustion/reuse\n");
    (void)serial_write("PASS VMM: 4 KiB, W^X, NULL/stack guards, map/unmap\n");
    timer_wait(3);
    (void)serial_write("PASS PIT: IRQ0 ticking at ~100 Hz\n");
    if (TEST_FAULT >= 15 && TEST_FAULT <= 20) memory_test_fault(TEST_FAULT);
    if (TEST_FAULT && TEST_FAULT < 15) interrupt_test_fault(TEST_FAULT);
    __asm__ volatile ("cli" ::: "memory");
    scheduler_init();
    __asm__ volatile ("sti" ::: "memory");
    if (!scheduler_selftest()) {
        (void)serial_write("ERROR: scheduler self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS scheduler: yield, sleep, exit, capacity, reuse\n");
    if (!handle_selftest()) {
        (void)serial_write("ERROR: handle self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS handles: types, references, stale tokens, capacity, retirement\n");
    if (!syscall_selftest()) {
        (void)serial_write("ERROR: syscall self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS syscalls: INT80, registers, ABI, handles, rejection\n");
    if (!capability_selftest() || !capability_launch_selftest()) {
        (void)serial_write("ERROR: capability self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS capabilities: rights, attenuation, delegation, launch rollback\n");
    if (!fs_selftest() || !pipe_selftest() || !pipe_task_selftest()) {
        (void)serial_write("ERROR: filesystem/pipe self-test failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    int user_result = user_selftest();
    if (user_result) {
        char number[4] = {(char)('0' + user_result / 100 % 10), (char)('0' + user_result / 10 % 10), (char)('0' + user_result % 10), 0};
        (void)serial_write("ERROR: userspace self-test "); (void)serial_write(number); (void)serial_write("\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    (void)serial_write("PASS extended: errors, RAMFS, ELF64/ring3, POSIX subset, COW undo, typed pipes\n");
    (void)serial_write("READY: interrupts enabled; idle\n");
    /* Bootstrap is the idle task. IRQ handlers never invoke the scheduler. */
    for (;;) { task_yield(); __asm__ volatile ("sti; hlt" ::: "memory"); }
}
