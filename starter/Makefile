CC = gcc
AS = nasm
LD = ld
OBJCOPY = objcopy
QEMU ?= qemu-system-x86_64
CFLAGS = -m64 -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie \
         -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone \
         -mno-mmx -mno-sse -mno-sse2 -msoft-float -O2 -g -Wall -Wextra -Werror
QEMU_FLAGS = -machine pc -cpu qemu64 -m 64 -accel tcg -display none \
             -serial stdio -monitor none -nic none -no-reboot -no-shutdown
OBJS = build/entry.o build/main.o build/serial.o

all: build/kernel.bin

build/entry.o: boot/entry.asm
	@mkdir -p build
	$(AS) -f elf64 -g -F dwarf $< -o $@

build/%.o: kernel/%.c kernel/serial.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel.elf: $(OBJS) linker.ld
	$(LD) -m elf_x86_64 -nostdlib -T linker.ld -o $@ $(OBJS)

build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@

run: build/kernel.bin
	$(QEMU) $(QEMU_FLAGS) -kernel $<

debug: build/kernel.bin
	$(QEMU) $(QEMU_FLAGS) -kernel $< -S -gdb tcp:127.0.0.1:1234

test: build/kernel.bin
	python3 tests/test_boot.py --qemu "$(QEMU)"

clean:
	rm -rf build

.PHONY: all run debug test clean
