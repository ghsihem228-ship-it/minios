/* Intentional faults are selected only by separate test-image main objects. */
#include "vmm.h"
#include "serial.h"
extern const char __text_start[], __boot_guard[];
extern _Noreturn void memory_stack_fault(uint64_t stack);
_Noreturn void memory_test_fault(unsigned test) {
    __asm__ volatile ("cli" ::: "memory");
    if (test == 15) __asm__ volatile ("xor %%rax, %%rax; mov (%%rax), %%rax" ::: "rax", "memory");
    if (test == 16) __asm__ volatile ("movb $0x90, (%0)" :: "r"(__text_start) : "memory");
    if (test == 17) {
        *(volatile uint8_t *)(uintptr_t)VM_ARENA = 0xc3; /* RET, if NX were broken. */
        ((void (*)(void))(uintptr_t)VM_ARENA)();
    }
    if (test == 18) (void)*(volatile uint8_t *)__boot_guard;
    if (test == 19) memory_stack_fault((uintptr_t)__boot_guard + PAGE_SIZE);
    if (test == 20) {
        if (!vm_free_page(VM_ARENA)) goto missing;
        (void)*(volatile uint8_t *)(uintptr_t)VM_ARENA;
    }
missing:
    (void)serial_write("ERROR: expected memory fault missing\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
