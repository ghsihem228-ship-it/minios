#include "syscall.h"
#include "interrupts.h"
#include "scheduler.h"
#include <stddef.h>
_Static_assert(sizeof(struct syscall_result) == 16, "SysV result registers");
_Static_assert(offsetof(struct syscall_result, value) == 8, "SysV second result");
void syscall_dispatch(struct interrupt_frame *f) {
    uint64_t args[6] = { f->rdi, f->rsi, f->rdx, f->r10, f->r8, f->r9 };
    struct syscall_result r = syscall_execute(task_handle_table(), f->rax, args);
    f->rax = (uint64_t)r.status;
    f->rdx = r.value;
}
static unsigned destroyed;
static void destroy(struct object *o) { (void)o; destroyed++; }
int syscall_selftest(void) {
    struct syscall_result r;
    struct object object;
    handle_t h, duplicate;
    destroyed = 0;
    if (!syscall_register_probe()) return 0;
    r = syscall_invoke(SYS_ABI_QUERY, 1, 0, 0, 0, 0, 0);
    if (r.status || r.value != SYS_ABI_VERSION) return 0;
    r = syscall_invoke(SYS_ABI_QUERY, 2, 0, 0, 0, 0, 0);
    if (r.status != SYS_EABI || r.value) return 0;
    r = syscall_invoke(SYS_ABI_QUERY, 1, 0, 0, 0, 0, 1);
    if (r.status != SYS_EINVAL || r.value) return 0;
    if (object_init(&object, OBJECT_TEST, destroy) != OBJ_OK ||
        handle_install(task_handle_table(), &object, &h) != OBJ_OK) return 0;
    r = syscall_invoke(SYS_HANDLE_DUPLICATE, h, 0, 0, 0, 0, 0);
    if (r.status || !r.value || r.value == h) return 0;
    duplicate = r.value;
    r = syscall_invoke(SYS_HANDLE_CLOSE, h, 0, 0, 0, 0, 0);
    if (r.status || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_CLOSE, h, 0, 0, 0, 0, 0);
    if (r.status != SYS_EBADHANDLE || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_CLOSE, duplicate, 0, 0, 0, 0, 0);
    if (r.status || object.refs != 1) return 0;
    object_put(&object);
    return destroyed == 1;
}
