#include "mini.h"
static uint64_t directory, descriptors[8];
int errno;
static size_t length(const char *s) { size_t n = 0; while (s[n] && n <= 31) n++; return n; }
static int fail(int64_t status) {
    errno = status == SYS_EACCESS ? 13 : status == SYS_ENOENT ? 2 : status == SYS_EBADHANDLE ? 9 :
            status == SYS_ENOSPC ? 28 : status == SYS_EFAULT ? 14 : status == SYS_EBUSY ? 16 :
            status == SYS_ENOMEM ? 12 : status == SYS_EAGAIN ? 11 : status == SYS_ENOSYS ? 38 : status == SYS_EOVERFLOW ? 75 : 22;
    return -1;
}
void mini_init(uint64_t root) { directory = root; errno = 0; for (unsigned i = 0; i < 8; i++) descriptors[i] = 0; }
uint64_t mini_handle(int fd) { return fd >= 0 && fd < 8 ? descriptors[fd] : 0; }
int open(const char *name, int flags, ...) {
    if ((flags & ~(O_CREAT | 3)) || (flags & 3) == 3) return fail(SYS_EINVAL);
    unsigned fd; for (fd = 0; fd < 8 && descriptors[fd]; fd++) {}
    if (fd == 8) { errno = 24; return -1; }
    cap_rights_t rights = (flags & 3) == O_WRONLY ? CAP_WRITE : (flags & 3) == O_RDWR ? CAP_READ | CAP_WRITE : CAP_READ;
    struct syscall_result r = syscall_invoke(SYS_FS_OPEN, directory, (uintptr_t)name, length(name), rights, !!(flags & O_CREAT), 0);
    if (r.status) return fail(r.status);
    descriptors[fd] = r.value; return (int)fd;
}
ssize_t read(int fd, void *data, size_t size) {
    if (!mini_handle(fd)) return fail(SYS_EBADHANDLE);
    struct syscall_result r = syscall_invoke(SYS_FS_READ, descriptors[fd], (uintptr_t)data, size, 0, 0, 0);
    return r.status ? fail(r.status) : (ssize_t)r.value;
}
ssize_t write(int fd, const void *data, size_t size) {
    if (!mini_handle(fd)) return fail(SYS_EBADHANDLE);
    struct syscall_result r = syscall_invoke(SYS_FS_WRITE, descriptors[fd], (uintptr_t)data, size, 0, 0, 0);
    return r.status ? fail(r.status) : (ssize_t)r.value;
}
off_t lseek(int fd, off_t offset, int whence) {
    if (!mini_handle(fd)) return fail(SYS_EBADHANDLE);
    if (whence != SEEK_SET || offset < 0) return fail(SYS_EINVAL);
    struct syscall_result r = syscall_invoke(SYS_FS_SEEK, descriptors[fd], (uint64_t)offset, 0, 0, 0, 0);
    return r.status ? fail(r.status) : offset;
}
int close(int fd) {
    if (!mini_handle(fd)) return fail(SYS_EBADHANDLE);
    struct syscall_result r = syscall_invoke(SYS_HANDLE_CLOSE, descriptors[fd], 0, 0, 0, 0, 0);
    if (r.status) return fail(r.status);
    descriptors[fd] = 0; return 0;
}
static size_t append(char *out, size_t capacity, size_t used, const char *s) {
    for (size_t i = 0; s[i]; i++) { if (capacity && used < capacity - 1) out[used] = s[i]; used++; }
    return used;
}
size_t mini_error(char *out, size_t capacity) {
    struct syscall_result count = syscall_invoke(SYS_ERROR_GET, 0, 3, 0, 0, 0, 0);
    size_t used = 0;
    if (!count.status && count.value) {
        struct syscall_result operation = syscall_invoke(SYS_ERROR_GET, 0, 2, 0, 0, 0, 0);
        const char *name = operation.value == SYS_FS_OPEN ? "open failed" :
                           operation.value == SYS_FS_READ ? "read failed" :
                           operation.value == SYS_FS_WRITE ? "write failed" : "operation failed";
        used = append(out, capacity, used, name);
        for (uint64_t i = 1; i < count.value; i++) {
            struct syscall_result domain = syscall_invoke(SYS_ERROR_GET, i, 0, 0, 0, 0, 0);
            struct syscall_result code = syscall_invoke(SYS_ERROR_GET, i, 1, 0, 0, 0, 0);
            if (domain.value == 2) used = append(out, capacity, used,
                code.value == (uint64_t)-SYS_EACCESS ? ", because permission denied" : ", because request rejected");
            if (domain.value == 3) {
                struct syscall_result rights = syscall_invoke(SYS_ERROR_GET, i, 2, 0, 0, 0, 0);
                used = append(out, capacity, used, rights.value == CAP_WRITE ? ", because the write capability is absent" :
                                                   rights.value == CAP_READ ? ", because the read capability is absent" :
                                                   ", because required capability rights are absent");
            }
        }
    }
    if (capacity) out[used < capacity ? used : capacity - 1] = 0;
    return used;
}
