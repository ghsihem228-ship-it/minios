CC      = gcc
AS      = nasm
LD      = ld
CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-pic -O2 -Wall -Wextra -Ikernel
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = build/boot.o build/gdt_flush.o build/isr_stubs.o \
       build/kernel.o build/vga.o build/gdt.o build/idt.o build/isr.o \
       build/pic.o build/irq.o build/timer.o build/keyboard.o build/pmm.o \
       build/paging.o build/heap.o build/shell.o build/elf.o build/task.o \
       build/ramfs.o build/syscall.o build/programs.o build/programs_blob.o \
       build/vfs.o build/block.o build/fat32.o

DISK_IMAGE ?= disks/minios.img

USER_PROGRAMS = hello probe filedemo sleep spin crash readonly isolate guard io diskdemo
USER_ELFS = $(addprefix build/user/,$(addsuffix .elf,$(USER_PROGRAMS)))

all: build/minios.bin

build/user/%.o: user/%.asm user/abi.inc
	@mkdir -p build/user
	$(AS) -f elf32 $< -o $@

build/user/%.elf: build/user/%.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -nostdlib -o $@ $<

build/programs_blob.o: boot/programs_blob.asm $(USER_ELFS)
	$(AS) -f elf32 $< -o $@

build/%.o: boot/%.asm
	@mkdir -p build
	$(AS) -f elf32 $< -o $@

build/%.o: kernel/%.c $(wildcard kernel/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/minios.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

disk:
	python3 tools/mkdisk.py "$(DISK_IMAGE)"

run: build/minios.bin disk
	qemu-system-i386 -kernel build/minios.bin -drive file="$(DISK_IMAGE)",format=raw,if=ide,index=0

test: build/minios.bin
	python3 tests/smoke.py
	python3 tests/storage.py

test-storage: build/minios.bin
	python3 tests/storage.py

clean:
	rm -rf build

.PHONY: all run disk test test-storage clean
