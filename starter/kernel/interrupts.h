#ifndef STARTER_INTERRUPTS_H
#define STARTER_INTERRUPTS_H
#include <stdint.h>

/* Internal ring-0 ABI, in the order pushed by boot/interrupts.asm.
 * Long mode saves SS:RSP even without a privilege change. Not a user ABI. */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

/* Called once on the bootstrap CPU with IF=0. Returns with IF still clear. */
void interrupts_init(void);
void interrupt_dispatch(struct interrupt_frame *frame);
/* Assembly probe verifies all 15 GPRs and interrupted DF survive INT3. */
int interrupt_register_probe(void);
int interrupt_selftest(void);
uint64_t timer_now(void);
/* Boot CPU only; enables interrupts and sleeps until at least n IRQ0 ticks. */
void timer_wait(uint64_t n);
/* Test images only: 6=UD2, 13=bad selector, 14=unmapped read, 8=double fault. */
_Noreturn void interrupt_test_fault(unsigned vector);
#endif
