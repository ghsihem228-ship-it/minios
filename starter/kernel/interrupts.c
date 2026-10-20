#include "interrupts.h"
#include "syscall.h"
#include "io.h"
#include "serial.h"
#include <stddef.h>

struct idt_gate {
    uint16_t offset_low, selector;
    uint8_t ist, attributes;
    uint16_t offset_middle;
    uint32_t offset_high, reserved;
} __attribute__((packed));
struct table_pointer { uint16_t limit; uint64_t base; } __attribute__((packed));
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap_base;
} __attribute__((packed));
_Static_assert(sizeof(struct idt_gate) == 16, "IDT gate size");
_Static_assert(sizeof(struct table_pointer) == 10, "64-bit table pointer size");
_Static_assert(sizeof(struct tss64) == 104, "TSS size");
_Static_assert(offsetof(struct tss64, ist) == 36, "TSS IST offset");
_Static_assert(sizeof(struct interrupt_frame) == 176, "Assembly frame size");
_Static_assert(offsetof(struct interrupt_frame, vector) == 120, "Vector offset");
_Static_assert(offsetof(struct interrupt_frame, rip) == 136, "CPU frame offset");

extern const uintptr_t interrupt_stubs[256];
extern void interrupt_load_gdt(const struct table_pointer *pointer);
extern const char probe_breakpoint_resume[];
static struct idt_gate idt[256] __attribute__((aligned(16)));
static uint64_t gdt[5] __attribute__((aligned(16)));
static struct tss64 tss;
static uint8_t df_stack[16384] __attribute__((section(".bss.df_stack"), aligned(4096)));
static uint8_t nmi_stack[16384] __attribute__((section(".bss.nmi_stack"), aligned(4096)));
static uint8_t mc_stack[16384] __attribute__((section(".bss.mc_stack"), aligned(4096)));
/* Aligned, single-CPU 64-bit access. Volatile is not an SMP synchronization API. */
volatile uint64_t timer_ticks;
static unsigned breakpoint_seen, spurious_seen;
static volatile unsigned panicking;

static _Noreturn void halt(void) {
    for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}
static void hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[17];
    for (unsigned i = 0; i < 16; i++) text[i] = digits[(value >> (60 - 4*i)) & 15];
    text[16] = 0;
    (void)serial_write(text);
}
static void field(const char *label, uint64_t value) {
    (void)serial_write(label);
    hex(value);
}
static int on_stack(const struct interrupt_frame *f, const uint8_t *stack) {
    uintptr_t p = (uintptr_t)f, low = (uintptr_t)stack;
    return p >= low && p <= low + 16384 - sizeof(*f);
}
static const char *exception_name(uint64_t vector) {
    switch (vector) {
    case 0: return "divide error";
    case 2: return "NMI";
    case 3: return "breakpoint";
    case 6: return "invalid opcode";
    case 8: return "double fault";
    case 13: return "general protection";
    case 14: return "page fault";
    case 18: return "machine check";
    default: return "unhandled vector";
    }
}
static _Noreturn void fatal(const struct interrupt_frame *f) {
    /* No lock or heap: NMI/fault recursion must not deadlock a panic logger. */
    __asm__ volatile ("cli" ::: "memory");
    if (panicking) halt();
    panicking = 1;
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    (void)serial_write("FATAL ");
    (void)serial_write(exception_name(f->vector));
    field(" vector=", f->vector);
    field(" error=", f->error);
    field(" rip=", f->rip);
    field(" cs=", f->cs);
    field(" rflags=", f->rflags);
    field(" rsp=", f->rsp);
    field(" ss=", f->ss);
    if (f->vector == 14) field(" cr2=", cr2);
    (void)serial_write("\n");
    if (f->vector == 8 || f->vector == 2 || f->vector == 18) {
        const uint8_t *stack = f->vector == 8 ? df_stack :
                               f->vector == 2 ? nmi_stack : mc_stack;
        (void)serial_write(on_stack(f, stack) ? "IST stack verified\n" :
                                              "ERROR: wrong IST stack\n");
    }
    (void)serial_write("HALTED\n");
    halt();
}

/* PICs use IRQ0..15 -> vectors 32..47, leaving CPU exception vectors alone. */
static void pic_init(void) {
    out8(0x21, 0xff); out8(0xa1, 0xff);
    out8(0x20, 0x11); io_wait();
    out8(0xa0, 0x11); io_wait();
    out8(0x21, 32); io_wait();
    out8(0xa1, 40); io_wait();
    out8(0x21, 4); io_wait();   /* Slave is wired to master's IRQ2. */
    out8(0xa1, 2); io_wait();
    out8(0x21, 1); io_wait();   /* 8086 mode, explicit EOI. */
    out8(0xa1, 1); io_wait();
    out8(0x21, 0xff); out8(0xa1, 0xff);
}
static int spurious_irq(unsigned irq) {
    if (irq != 7 && irq != 15) return 0;
    uint16_t command = irq == 7 ? 0x20 : 0xa0;
    out8(command, 0x0b);       /* Read in-service register. */
    if (in8(command) & 0x80) return 0;
    if (irq == 15) out8(0x20, 0x20); /* Master saw the cascade only. */
    spurious_seen++;
    return 1;                /* No EOI to a PIC that did not accept this IRQ. */
}
void interrupts_init(void) {
    /* IF must be clear throughout GDT/TSS/IDT/PIC setup. */
    tss.ist[0] = (uintptr_t)(df_stack + sizeof(df_stack));
    tss.ist[1] = (uintptr_t)(nmi_stack + sizeof(nmi_stack));
    tss.ist[2] = (uintptr_t)(mc_stack + sizeof(mc_stack));
    tss.iomap_base = sizeof(tss); /* No I/O bitmap within this TSS. No ring 3 yet. */
    gdt[1] = 0x00af9a000000ffffull;
    gdt[2] = 0x00cf92000000ffffull;
    uint64_t base = (uintptr_t)&tss, limit = sizeof(tss) - 1;
    gdt[3] = (limit & 0xffff) | ((base & 0xffffff) << 16) |
             (0x89ull << 40) | (((limit >> 16) & 15) << 48) |
             (((base >> 24) & 255) << 56);
    gdt[4] = base >> 32;
    const struct table_pointer gdtr = {sizeof(gdt) - 1, (uintptr_t)gdt};
    interrupt_load_gdt(&gdtr);
    for (unsigned n = 0; n < 256; n++) {
        uint64_t handler = interrupt_stubs[n];
        idt[n] = (struct idt_gate){
            .offset_low = handler, .selector = 8,
            .ist = n == 8 ? 1 : n == 2 ? 2 : n == 18 ? 3 : 0,
            .attributes = 0x8e, /* Present, DPL0, 64-bit interrupt gate, IF cleared. */
            .offset_middle = handler >> 16, .offset_high = handler >> 32
        };
    }
    const struct table_pointer idtr = {sizeof(idt) - 1, (uintptr_t)idt};
    __asm__ volatile ("lidt %0" :: "m"(idtr) : "memory");
    pic_init();
    /* PIT channel 0, mode 2, lo/hi divisor: ~100.0067 Hz, not a precise clock. */
    const unsigned divisor = 11932;
    out8(0x43, 0x34);
    out8(0x40, divisor & 255);
    out8(0x40, divisor >> 8);
    out8(0x21, 0xfe);          /* Unmask only IRQ0; slave stays fully masked. */
}
void interrupt_dispatch(struct interrupt_frame *f) {
    if (f->vector == 128) { syscall_dispatch(f); return; }
    if (f->vector == 3 && f->rip == (uintptr_t)probe_breakpoint_resume) {
        uint64_t live_flags;
        __asm__ volatile ("pushfq; pop %0" : "=r"(live_flags));
        /* Confirm CLD before C and the original DF and every GPR in the frame. */
        if ((live_flags & (1u << 10)) || !(f->rflags & (1u << 10))) fatal(f);
        if (f->r15 != 0x112233440000000full || f->r14 != 0x112233440000000eull || f->r13 != 0x112233440000000dull || f->r12 != 0x112233440000000cull ||
            f->r11 != 0x112233440000000bull || f->r10 != 0x112233440000000aull || f->r9 != 0x1122334400000009ull || f->r8 != 0x1122334400000008ull ||
            f->rdi != 0x1122334400000007ull || f->rsi != 0x1122334400000006ull || f->rbp != 0x1122334400000005ull || f->rdx != 0x1122334400000004ull ||
            f->rcx != 0x1122334400000003ull || f->rbx != 0x1122334400000002ull || f->rax != 0x1122334400000001ull) fatal(f);
        if (f->error || f->cs != 8 || f->ss != 16) fatal(f);
        breakpoint_seen++;
        return;              /* INT3 saves the following RIP. Never skip a fault. */
    }
    if (f->vector >= 32 && f->vector < 48) {
        unsigned irq = f->vector - 32;
        if (spurious_irq(irq)) return;
        if (!irq) timer_ticks++;
        else {               /* An unconfigured source must not flood the CPU. */
            uint16_t port = irq < 8 ? 0x21 : 0xa1;
            out8(port, in8(port) | (1u << (irq & 7)));
        }
        if (irq >= 8) out8(0xa0, 0x20);
        out8(0x20, 0x20);     /* EOI after service, slave before master. */
        return;
    }
    fatal(f);
}
int interrupt_selftest(void) {
    if (!interrupt_register_probe() || breakpoint_seen != 1) return 0;
    /* Software INT is safe for these no-error-code vectors. IF is still zero. */
    __asm__ volatile ("int $0x27; int $0x2f" ::: "memory");
    return spurious_seen == 2;
}
uint64_t timer_now(void) { return timer_ticks; }
void timer_wait(uint64_t n) {
    uint64_t start = timer_now();
    for (;;) {
        __asm__ volatile ("cli" ::: "memory");
        if (timer_now() - start >= n) break;
        /* STI's interrupt shadow closes the check-to-HLT lost-wakeup race. */
        __asm__ volatile ("sti; hlt" ::: "memory");
    }
    __asm__ volatile ("sti" ::: "memory");
}
_Noreturn void interrupt_test_fault(unsigned vector) {
    __asm__ volatile ("cli" ::: "memory");
    if (vector == 6) __asm__ volatile ("ud2");
    if (vector == 14) {
        /* First byte beyond the bootstrap identity map. A genuine CPU #PF. */
        (void)*(volatile uint8_t *)(uintptr_t)0x40000000;
    }
    if (vector == 8) idt[13].attributes = 0; /* #GP delivery -> #NP -> #DF. */
    if (vector == 13 || vector == 8)
        __asm__ volatile ("mov $0x28, %%ax; mov %%ax, %%ds" ::: "rax", "memory");
    /* Test wiring errors must not appear as a successful fault test. */
    (void)serial_write("ERROR: expected exception missing\n");
    halt();
}
