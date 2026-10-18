CC      = gcc
AS      = nasm
LD      = ld
CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-pic -O2 -Wall -Wextra -Ikernel
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = build/boot.o build/gdt_flush.o build/isr_stubs.o \
       build/kernel.o build/vga.o build/gdt.o build/idt.o build/isr.o \
       build/pic.o build/irq.o build/timer.o build/keyboard.o

all: build/minios.bin

build/%.o: boot/%.asm
	@mkdir -p build
	$(AS) -f elf32 $< -o $@

build/%.o: kernel/%.c $(wildcard kernel/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/minios.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

run: build/minios.bin
	qemu-system-i386 -kernel build/minios.bin

clean:
	rm -rf build

.PHONY: all run clean
