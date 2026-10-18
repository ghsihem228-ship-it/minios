# MiniOS

A small x86 operating system kernel written from scratch in C and assembly, built to learn how an OS really works.

## Goals

- [x] Boot via Multiboot (GRUB / QEMU)
- [x] VGA text-mode output
- [x] GDT and IDT, CPU exceptions ([notes](docs/02-gdt-idt.md))
- [ ] Hardware interrupts (PIC, timer)
- [ ] Keyboard driver
- [ ] Physical memory manager (bitmap frame allocator)
- [ ] Paging and a kernel heap (`kmalloc` / `kfree`)
- [ ] Multitasking with a round-robin scheduler
- [ ] System calls
- [ ] In-memory filesystem (RAM disk)
- [ ] Interactive shell (`help`, `ls`, `cat`, `mem`, `ps`, `clear`)

## Requirements

- `gcc` with 32-bit support (`gcc-multilib`)
- `nasm`
- `ld` (binutils)
- `qemu-system-i386`

On Debian/Ubuntu:

```bash
sudo apt install build-essential gcc-multilib nasm qemu-system-x86
```

## Build and run

```bash
make        # builds build/minios.bin
make run    # boots the kernel in QEMU
make clean
```

Expected output: `MiniOS v0.2` followed by green `[ OK ]` lines for the boot, GDT, IDT and a breakpoint self-test.

## Project layout

```
boot/boot.asm         Multiboot header and entry point
boot/gdt_flush.asm    Loads the GDT and reloads segment registers
boot/isr_stubs.asm    CPU exception entry stubs (0-31) and idt_load
kernel/kernel.c       kernel_main
kernel/vga.c          VGA text-mode driver
kernel/gdt.c          Global Descriptor Table
kernel/idt.c          Interrupt Descriptor Table
kernel/isr.c          Exception handler
docs/                 Design notes, one per milestone
linker.ld             Linker script (kernel loaded at 1 MiB)
Makefile
```

## Roadmap notes

Each milestone should land as its own commit (or small PR) with a short note in `docs/` explaining the design: why the GDT looks the way it does, how the frame allocator tracks memory, how a context switch saves registers, and so on. Those notes are what make the repo valuable to readers.

## References

- OSDev Wiki: https://wiki.osdev.org
- Intel 64 and IA-32 Architectures Software Developer's Manuals
- Operating Systems: Three Easy Pieces (free online)

## License

MIT
