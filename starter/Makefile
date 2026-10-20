CC = gcc
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:
AS = nasm
LD = ld
OBJCOPY = objcopy
QEMU ?= qemu-system-x86_64
CFLAGS = -m64 -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie \
         -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone \
         -mno-mmx -mno-sse -mno-sse2 -msoft-float -O2 -g -Wall -Wextra -Werror -MMD -MP
QEMU_FLAGS = -machine pc -cpu qemu64 -m 64 -accel tcg -display none \
             -serial stdio -monitor none -nic none -no-reboot -no-shutdown
OBJS = build/entry.o build/interrupt_stubs.o build/main.o build/serial.o build/interrupts.o
FAULTS = 6 8 13 14
FAULT_OBJS = $(foreach n,$(FAULTS),build/fault-$(n).o)
FAULT_IMAGES = $(foreach n,$(FAULTS),build/fault-$(n).bin)

all: build/kernel.bin

build/entry.o: boot/entry.asm
	@mkdir -p build
	$(AS) -f elf64 -g -F dwarf $< -o $@

build/interrupt_stubs.o: boot/interrupts.asm
	@mkdir -p build
	$(AS) -f elf64 -g -F dwarf $< -o $@

build/%.o: kernel/%.c
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

$(FAULT_OBJS): build/fault-%.o: kernel/main.c
	@mkdir -p build
	$(CC) $(CFLAGS) -DTEST_FAULT=$* -c $< -o $@

build/fault-%.elf: build/fault-%.o $(filter-out build/main.o,$(OBJS)) linker.ld
	$(LD) -m elf_x86_64 -nostdlib -T linker.ld -o $@ $(filter %.o,$^)

build/fault-%.bin: build/fault-%.elf
	$(OBJCOPY) -O binary $< $@

test: build/kernel.bin $(FAULT_IMAGES)
	python3 tests/test_boot.py --qemu "$(QEMU)"

clean:
	rm -rf build

.PHONY: all run debug test clean
.SECONDARY:
-include $(wildcard build/*.d)
