# Step 11: a small source-level POSIX compatibility library

## 1. Alternatives

Emulating Linux's syscall numbers can run some existing binaries but commits to
large undocumented behavioral details, process/signal semantics and an ELF runtime.
A userspace source compatibility library keeps the kernel ABI small and maps
familiar operations to capabilities. A full libc port is more useful eventually,
but cannot be claimed without process, memory, threading and signal foundations.

## 2. Recommendation

Start with a freestanding source-level subset. Do not label this POSIX conformance
or compatibility with arbitrary Unix tools. It is enough to compile and run the
included file-I/O program with familiar signatures while preserving scoped access.

## 3. Specification

```c
typedef long ssize_t;
typedef long off_t;
extern int errno;
void mini_init(uint64_t explicitly_supplied_directory);
int open(const char *name, int flags, ...);
ssize_t read(int fd, void *buffer, size_t length);
ssize_t write(int fd, const void *buffer, size_t length);
off_t lseek(int fd, off_t offset, int whence);
int close(int fd);
```

Eight library-owned descriptors map to opaque native handles. Initially all are
closed, including stdin/stdout/stderr; no console capability is implicitly granted.
mini_init receives a capability supplied to this program and retains it as the
explicit lookup root. Names are scoped leaf names, not global paths. O_RDONLY,
O_WRONLY, O_RDWR and O_CREAT are supported. Mode arguments are ignored: capability
policy controls access; there is no UID/mode-bit implementation. SEEK_SET only is
supported. O_TRUNC/O_APPEND and unsupported flags fail EINVAL, not pretend-success.
Each call transfers at most 2048 bytes; no implicit chunking changes atomicity.

Errors become -1 with errno: EACCES=13, ENOENT=2, EBADF=9, ENOSPC=28, EFAULT=14,
EBUSY=16, ENOMEM=12, EAGAIN=11, ENOSYS=38, EOVERFLOW=75, otherwise EINVAL=22. Descriptor-table
exhaustion is EMFILE=24. A successful call need not clear errno, matching ordinary
POSIX expectations. Lookup failures in the library itself do not invent a kernel
cause chain; mini_error displays the last actual syscall's structured error.

Descriptors and errno are per executable's static data; this runtime is single
threaded. Invalid name pointers can fault the user process before open reaches the
kernel because the library calculates name length. This cannot grant kernel access.

## 4. Working code

user/mini.h and mini.c provide the implementation; user/start.asm provides CRT
entry/exit and demo.c is the client. It is linked without a host libc dependency.

```c
mini_init(root_handle);
int fd = open("note", O_RDWR | O_CREAT);
if (fd >= 0) {
    write(fd, "hello", 5);
    lseek(fd, 0, SEEK_SET);
    char buffer[5];
    read(fd, buffer, sizeof(buffer));
    close(fd);
}
```

## 5. Tests

`make test-extended` boots the actual C runtime in ring 3, including descriptor
mapping, file offsets, permission errno, read/write contents and close. The
structured-error rendering is also tested in that real program. Host sanitizer
checks cover underlying kernel operations; no claim of running libc's complete
conformance suite or external Unix utilities is made.

## 6. Risks and limits

This does not support fork/exec, argv, pipes as POSIX byte streams, waitpid,
signals, sockets, termios, mmap, malloc, stdio formatting, threads or a dynamic
linker. Existing general Unix tools cannot yet run unchanged. Expanding this
library or porting an established libc is a separate sizeable project. Never
implement path compatibility by quietly adding ambient global-root access.
