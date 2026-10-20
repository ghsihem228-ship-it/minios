# v0.10: C programs, startup and command arguments

MiniOS now builds and runs freestanding C user programs. A process receives
`argc/argv`, its C entry routine calls `main`, and returning from `main` exits
through the existing checked syscall ABI. The HTTP example is now C and takes
its server, port and path from the shell.

## Build boundary

The Ubuntu/cloud **host** compiles programs; MiniOS itself has no compiler.
`user/include/minios.h` is the small public API. `user/lib/` builds into
`build/user/libmini.a`, and `user/crt0.asm` supplies `_start`. C programs link
statically with the existing i386 linker script and are embedded as ordinary
read-only `/bin` ELF files, or copied to `/disk` and loaded by the same ELF
loader. No additional syscall numbers were needed.

Compilation uses `-m32 -std=c11 -ffreestanding -fno-builtin -fno-pie -fno-pic`,
no stack protector, and no x87/MMX/SSE code. Functions/data have separate
sections; the linker garbage-collects unused library sections. GCC headers
provide integer, size and varargs types; no host glibc or startup files are
linked. Memory helpers are supplied by the library itself. C builds require
no new dependency beyond the existing GCC/binutils/NASM workflow.

Floating-point state is still deliberately disabled in the kernel. There is no
`malloc`, `sbrk`, stdio `FILE`, environment-variable API, signals, threads,
dynamic linker, C++ constructor support, or full ISO C/POSIX compatibility.
Use stack/static storage and the documented small API. This is a foundation
for C applications, not a claim that arbitrary host C programs run unchanged.

## Initial process stack

`startup.c` validates and packs arguments before process image allocation.
The maximum is eight arguments including the executable path. Each string is
at most 63 bytes plus NUL; the total is consequently bounded by 512 bytes.
Empty non-program arguments are valid; an empty executable path is rejected.
The complete initial image is bounded by a 1024-byte kernel buffer and must
fit in the already mapped 16 KiB user stack.

The initial ESP is aligned to 16 bytes and points to:

| Address | Content |
| --- | --- |
| ESP | 32-bit argc |
| ESP + 4 | argv[0], a user virtual address |
| ESP + 4 + 4*i | argv[i] |
| ESP + 4 + 4*argc | NULL terminator for argv |
| ESP + 8 + 4*argc | NULL for the empty envp vector |
| higher addresses | zero padding and NUL-terminated argument strings |

All pointers and strings refer to the new process's writable stack frames.
They never refer to shell/kernel storage. `paging_space_write()` copies the
packed bytes into the fresh address space while task 0 still owns CR3. A
failed write destroys the new mappings and returns ENOMEM before publishing
a PID. Argument validation failures allocate no process pages. Background
programs keep their arguments even after the parent shell reuses its line.

`crt0.asm` clears DF, reads argc/argv, aligns the call site according to the
i386 cdecl ABI, calls `main(int argc, char **argv)`, and exits with its return
value. The existing kernel exit syscall retains its eight-bit exit status.
An empty envp terminator is provided for layout clarity; environment variables
and an ELF auxiliary vector are not implemented.

Old assembly programs keep their entry point/register convention and may
ignore the stack arguments. Stack guard pages and read-only code remain
unchanged. No kernel pointers are put in the initial frame.

## Shell syntax and bounds

`command_split()` operates in place within a known capacity and returns an
error for the whole command on malformed input. The shell now accepts up to
255 bytes per line, with space/tab delimiters, single/double quotes, and
backslash escaping outside single quotes. Empty quoted words are retained.
Quotes concatenate with adjacent text. A backslash escapes the next literal
character; `\n` does not become a newline.

```text
run /bin/args alpha beta
run /bin/args "hello world" "" 'single quoted'
run /bin/args escaped\ space
spawn /bin/args --delay 200 retained
write /tmp/note "hello world"
```

The argument count for run/spawn includes argv[0] but excludes the shell's
`run`/`spawn` word. More than eight program arguments returns a shell count
error; a string of 64 or more bytes returns E2BIG. Unterminated quotes and a
trailing backslash are rejected before launching anything. Other shell
commands keep their existing arity rules. There are no pipelines, expansion,
command substitution, redirection or job-control operators. Keyboard layout
remains QWERTY; the earlier AZERTY typing caveat still applies.

## The small C library

| Area | API |
| --- | --- |
| Output/formatting | `printf`, `snprintf`, `vsnprintf` |
| Strings/memory | `strlen`, `strcmp`, `strncmp`, `memcpy`, `memmove`, `memset`, `memcmp` |
| Strict numeric input | `parse_u32`, `parse_ipv4` |
| Process/console | `sys_console`, `sys_exit`, `sys_yield`, `sys_getpid`, `sys_ticks`, `sys_sleep` |
| Files | `sys_open`, `sys_read`, `sys_write`, `sys_close`, `sys_seek`, `sys_unlink`, `sys_sync`, `sys_stat`, `write_all` |
| Sockets | `sys_socket`, `sys_connect`, `sys_send`, `sys_recv`, `sys_socket_close`, `sys_socket_status`, `sys_bind` |

The wrappers expose negative errno directly; there is no global `errno`.
`sys_ticks` returns a wrapping unsigned 32-bit counter. File/socket descriptor
namespaces, ownership, flags and limits remain those of v0.6–v0.9. Socket calls
are nonblocking; sleep/yield between EAGAIN/EINPROGRESS retries. Wrappers do
not make a large socket transfer atomic or bypass the 1024-byte syscall limit.
`write_all` splits **file** writes into at most 1024 bytes, handles short
progress and returns an error on failure (previous chunks may already exist).

Formatting supports `%s`, `%c`, `%d`/`%i`, `%u`, `%x`/`%X`, `%p` and `%%`.
Integer width/zero padding is bounded to 32; `%p` prints an eight-digit i386
address with a `0x` prefix. There are no floats, 64-bit/length modifiers,
precision, locale, `%n`, or left alignment. Unsupported formats return EINVAL.
Signed conversion handles INT32_MIN without signed overflow. `snprintf` NUL
terminates when capacity is positive, reports the untruncated length and
supports capacity 0 with a NULL output. As with ordinary C functions, callers
must provide valid format strings and matching argument types.

`printf` uses a 128-byte stack buffer and repeated console calls rather than
allocating a whole formatted message. Long output can interleave with other
processes. Numeric parsers reject signs, junk, empty values and overflow;
IPv4 accepts exactly four decimal octets. They do not perform DNS lookup.

## Configurable HTTP

Start the existing host demonstration server in a second Ubuntu/WSL terminal:

```bash
cd ~/minios
make netfiles
python3 -m http.server 8080 --bind 127.0.0.1 --directory netfiles
```

Then inside MiniOS:

```text
dhcp
run /bin/http 10.0.2.2 8080 /welcome.txt
run /bin/http --help
```

The no-argument form still defaults to `10.0.2.2:8080/welcome.txt`, preserving
the v0.9 workflow. The new form accepts a numeric IPv4 address, decimal port
1–65535 and absolute ASCII path, including query strings. Whitespace/control
characters in the path are rejected rather than copied into request headers.
The Host header includes the chosen address and port.

The program sends HTTP/1.0 with `Connection: close`, drains raw response bytes
until EOF, then prints their exact count/FNV-1a digest and closes its socket.
Its wait deadline is ten seconds and response cap is 1 MiB. It does not parse
HTTP status, Content-Length/chunked framing, redirects, compression, DNS names,
HTTPS or TLS. A fully received 404 response is still a successful raw transfer.
It prints output rather than saving a download; the existing shell TFTP
`fetch` remains the download-to-file workflow. Invalid arguments return status
2; transport failures return status 1; a completed stream returns status 0.

`/bin/args` prints argc and every argument and optionally delays for at most
500 ticks with `--delay <ticks>` to demonstrate background argument ownership.
`/bin/ctest` runs the actual C startup/library/file/socket checks in Ring 3.

## Adding a C program

1. Add `user/example.c` including `minios.h`, with `int main(int argc, char **argv)`.
2. Add `example` to `USER_C_PROGRAMS` in the Makefile. `make build/user/example.elf`
   builds it against the CRT and library.
3. To embed it under `/bin`, add its `program` entry in `boot/programs_blob.asm`
   and matching DECLARE/ADD entries in `kernel/programs.c` (within the RAMFS node
   limit), then rebuild the kernel. Alternatively expose the ELF through the
   host `netfiles/` directory and use `fetch` to create a new `/disk/*.elf` file.
4. Run `run /bin/example ...` or `run /disk/example.elf ...`. No kernel code or
   new syscall is needed for functions already exposed by the library.

## Validation

A clean build verifies that C ELF files depend only on the checked-in CRT,
linker script and static library, not old assembly objects. The shared pure
shell/stack routines run under ASan/UBSan with 100,000 randomized cases, plus
explicit quote, escape, count, length, alignment and pointer-layout checks.
Together with the existing DNS/DHCP/TCP harnesses the host tests run 300,000
malformed-input cases.

`make test-runtime` boots QEMU at 2/64 MiB and verifies the actual C entry ABI,
BSS/data reset on repeated loads, mutable private argv, delayed/background
argument lifetime, maximum/empty/quoted arguments, invalid command rejection,
formatting/truncation/INT32_MIN, strings, negative-error syscalls, file writes
split across the syscall limit, socket wrappers, and heap/frame recovery.
A local HTTP server on a variable port checks the requested query path and
Host header, and the guest's byte count/hash must match independent expected
bytes. C ELF bytes are inspected independently on FAT32 and executed after a
fresh VM restart. No public service is required.

`make test` also runs all existing memory/process, storage, DHCP/DNS/TFTP and
TCP/UDP regressions. The former input-overflow check now exceeds the documented
255-byte line limit. Tests create temporary disk images and listeners and
terminate only their own VMs/services.
