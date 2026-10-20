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
OBJS += build/syscall.o build/syscall_entry.o build/syscall_stubs.o
OBJS += build/capability_test.o
OBJS += build/object.o build/handle.o build/handle_test.o
OBJS += build/scheduler.o build/switch.o
OBJS += build/bootmem.o build/bootinfo.o build/pmm.o build/vmm.o build/memory_test.o
FAULTS = 6 8 13 14 15 16 17 18 19 20 100 101
FAULT_OBJS = $(foreach n,$(FAULTS),build/fault-$(n).o)
FAULT_IMAGES = $(foreach n,$(FAULTS),build/fault-$(n).bin)

all: build/kernel.bin

build/entry.o: boot/entry.asm
	@mkdir -p build
	$(AS) -f elf64 -g -F dwarf $< -o $@

build/syscall_stubs.o: boot/syscall.asm
	@mkdir -p build
	$(AS) -f elf64 -g -F dwarf $< -o $@

build/switch.o: boot/switch.asm
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
	$(MAKE) test-host

build/test_memory: tests/test_memory.c kernel/bootmem.c kernel/pmm.c kernel/bootmem.h kernel/pmm.h
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Ikernel -fsanitize=address,undefined \
	    -fno-omit-frame-pointer -fno-pie -no-pie tests/test_memory.c kernel/bootmem.c kernel/pmm.c -o $@

test-host: build/test_memory build/test_handle build/test_syscall build/test_capability
	ASAN_OPTIONS=detect_leaks=1 ./build/test_memory
	ASAN_OPTIONS=detect_leaks=1 ./build/test_capability
	ASAN_OPTIONS=detect_leaks=1 ./build/test_syscall
	ASAN_OPTIONS=detect_leaks=1 ./build/test_handle

clean:
	rm -rf build

.PHONY: all run debug test test-host clean
.SECONDARY:
-include $(wildcard build/*.d)

test-scheduler: build/kernel.bin
	python3 tests/test_scheduler.py --qemu "$(QEMU)"
.PHONY: test-scheduler

build/test_handle: tests/test_handle.c kernel/object.c kernel/handle.c kernel/handle_test.c kernel/object.h kernel/handle.h kernel/capability.h
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Ikernel -DOBJECT_HOST_TEST \
	    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
	    tests/test_handle.c kernel/object.c kernel/handle.c kernel/handle_test.c -o $@

test-handles: build/kernel.bin build/test_handle
	ASAN_OPTIONS=detect_leaks=1 ./build/test_handle
	python3 tests/test_scheduler.py --qemu "$(QEMU)"

.PHONY: test-handles

build/test_syscall: tests/test_syscall.c kernel/syscall.c kernel/handle.c kernel/object.c kernel/syscall.h kernel/syscall_abi.h kernel/handle.h kernel/object.h kernel/capability.h
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Ikernel -DOBJECT_HOST_TEST \
	    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
	    tests/test_syscall.c kernel/syscall.c kernel/handle.c kernel/object.c -o $@

test-syscalls: build/kernel.bin build/test_syscall
	ASAN_OPTIONS=detect_leaks=1 ./build/test_syscall
	python3 tests/test_scheduler.py --qemu "$(QEMU)"
.PHONY: test-syscalls

build/test_capability: tests/test_capability.c kernel/capability_test.c kernel/syscall.c kernel/handle.c kernel/object.c kernel/handle.h kernel/object.h kernel/capability.h kernel/syscall_abi.h kernel/syscall.h
	@mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Ikernel -DOBJECT_HOST_TEST \
	    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
	    tests/test_capability.c kernel/capability_test.c kernel/syscall.c kernel/handle.c kernel/object.c -o $@

test-capabilities: build/kernel.bin build/test_capability
	ASAN_OPTIONS=detect_leaks=1 ./build/test_capability
	python3 tests/test_scheduler.py --qemu "$(QEMU)"
.PHONY: test-capabilities
