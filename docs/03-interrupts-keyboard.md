# Milestone 3: hardware interrupts, timer and keyboard

## Why this milestone

Until now the kernel only ran straight-line code. Real operating systems react to hardware: a timer ticks, a key is pressed. That needs three pieces: a controller that routes device signals to the CPU (the PIC), a way to dispatch them (IRQ handlers in the IDT), and drivers that use them (timer, keyboard). The timer is also the heartbeat the scheduler will use later to switch between tasks.

## The PIC (`kernel/pic.c`)

The two cascaded 8259 chips deliver IRQ lines 0-15. By default they map IRQ 0-7 to vectors 8-15, which collide with CPU exceptions (vector 8 is the double fault). `pic_remap` reprograms them so IRQ 0-15 arrive on vectors **32-47**, then masks every line. A driver calls `pic_unmask(irq)` to receive its interrupt.

After handling an IRQ the kernel must send an *end of interrupt* (`pic_send_eoi`), otherwise the PIC delivers nothing more on that line.

## IRQ dispatch (`kernel/irq.c`, `boot/isr_stubs.asm`)

`irq0` to `irq15` are small assembly stubs that push a dummy error code and the vector number, then jump to the same `isr_common` used by CPU exceptions. `isr_handler` checks for vectors 32-47 and hands them to `irq_dispatch`, which calls the handler registered with `irq_register_handler`, then sends the EOI.

## The timer (`kernel/timer.c`)

The PIT (programmable interval timer) has an input clock of 1,193,182 Hz. Writing a divisor to channel 0 makes it raise IRQ0 at the frequency we want. MiniOS uses 100 Hz: `divisor = 1193182 / 100 = 11931`. Every tick increments a counter, and once per second the uptime in the status bar is refreshed.

## The keyboard (`kernel/keyboard.c`)

On IRQ1 the handler reads a scancode from port `0x60`. Scancode set 1 sends one code when a key goes down and another (with bit 7 set) when it comes up. The driver:

- translates press codes to ASCII with a US layout table, and a second table when Shift is held,
- ignores release codes except for Shift,
- ignores the `0xE0` extended prefix (arrows, Delete) for now,
- stores characters in a ring buffer read by `keyboard_getchar()`.

`keyboard_getchar` uses `cli`, checks the buffer, and if it is empty runs `sti; hlt`. The CPU delays interrupts until after the instruction following `sti`, so a key press can not slip in between the check and the sleep. The CPU sleeps instead of spinning.

## VGA improvements (`kernel/vga.c`)

- Row 0 is a status bar (left and right text). Typing and scrolling only touch rows 1-24, so an interrupt handler can refresh it safely.
- Backspace and tab are supported.
- The blinking hardware cursor follows the text through ports `0x3D4` / `0x3D5`.

## Testing

`make run`, then type. Letters, digits, Shift, Backspace and Enter work, and the uptime counter in the top right increases every second.

## Things to try

- Change `timer_init(100)` to `timer_init(10)` and watch how the system still behaves.
- Add Caps Lock support.
- Handle the arrow keys (the `0xE0` prefix) to move the cursor.

## Next

Memory management: read the memory map from the Multiboot info, then write a physical frame allocator.
