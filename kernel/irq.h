#ifndef IRQ_H
#define IRQ_H

#include "isr.h"

typedef void (*irq_handler_t)(struct regs *);

void irq_install(void);
void irq_register_handler(int irq, irq_handler_t handler);
void irq_dispatch(struct regs *r);

#endif
