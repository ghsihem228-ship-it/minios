/* irq.c - hardware interrupt dispatch (IRQ 0-15 = vectors 32-47) */

#include "irq.h"
#include "idt.h"
#include "gdt.h"
#include "pic.h"

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void (*const irq_stubs[16])(void) = {
    irq0, irq1, irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15,
};

static irq_handler_t handlers[16];

void irq_install(void) {
    for (int i = 0; i < 16; i++)
        idt_set_gate((uint8_t)(32 + i), (uint32_t)irq_stubs[i], GDT_KERNEL_CODE, 0x8E);
}

void irq_register_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16)
        handlers[irq] = handler;
}

/* Called from isr_handler for vectors 32-47. */
void irq_dispatch(struct regs *r) {
    int irq = (int)r->int_no - 32;

    if (handlers[irq])
        handlers[irq](r);

    pic_send_eoi((uint8_t)irq);
}
