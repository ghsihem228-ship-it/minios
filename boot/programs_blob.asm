section .rodata
%macro program 2
align 4
global program_%1_start
global program_%1_end
program_%1_start:
    incbin %2
program_%1_end:
%endmacro
program hello, "build/user/hello.elf"
program probe, "build/user/probe.elf"
program filedemo, "build/user/filedemo.elf"
program sleep, "build/user/sleep.elf"
program spin, "build/user/spin.elf"
program crash, "build/user/crash.elf"
program readonly, "build/user/readonly.elf"
program isolate, "build/user/isolate.elf"
program guard, "build/user/guard.elf"
program io, "build/user/io.elf"
program http, "build/user/http.elf"
program udpecho, "build/user/udpecho.elf"
program sockprobe, "build/user/sockprobe.elf"
program sockhold, "build/user/sockhold.elf"
program diskdemo, "build/user/diskdemo.elf"
section .note.GNU-stack noalloc noexec nowrite progbits
