#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* CPU state saved by isr_common (see boot/isr_stubs.asm). The field order
 * mirrors exactly what the assembly code pushes onto the stack. */
struct regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by pusha */
    uint32_t int_no, err_code;                        /* pushed by the stub */
    uint32_t eip, cs, eflags;                         /* pushed by the CPU */
} __attribute__((packed));

/* Catch layout mistakes at compile time: 14 words, matching the assembly. */
_Static_assert(sizeof(struct regs) == 14 * 4, "struct regs must match isr_common");

void isr_install(void);
void isr_handler(struct regs *r);

#endif
