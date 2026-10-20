#include "syscall.h"
#include "interrupts.h"
#include "scheduler.h"
#include "process.h"
#include <stddef.h>
_Static_assert(sizeof(struct syscall_result) == 16, "SysV result registers");
_Static_assert(offsetof(struct syscall_result, value) == 8, "SysV second result");
void syscall_dispatch(struct interrupt_frame *f) {
    uint64_t args[6] = { f->rdi, f->rsi, f->rdx, f->r10, f->r8, f->r9 };
    uint64_t number = f->rax;
    int user = user_context.active && (f->cs & 3) == 3;
    struct handle_table *table = user ? &user_context.handles : task_handle_table();
    struct error_chain *error = user ? &user_context.error : task_error_chain();
    struct syscall_result r;
    if (number == SYS_ERROR_GET) {
        r = (struct syscall_result){ SYS_EINVAL, 0 };
        if (!(args[2] | args[3] | args[4] | args[5])) r = error_field(error, args[0], args[1]);
    } else {
        int handled = services_dispatch(f, &r);
        if (handled == 2) return;
        if (!handled) {
            if (user && user_context.transaction && number >= SYS_HANDLE_CLOSE && number <= SYS_HANDLE_CLONE)
                r = (struct syscall_result){SYS_EBUSY, 0};
            else r = syscall_execute(table, number, args);
        }
        uint64_t required = number == SYS_HANDLE_DUPLICATE ? CAP_DUPLICATE :
                            number == SYS_HANDLE_CLONE ? CAP_DUPLICATE | args[1] :
                            number == SYS_HANDLE_RESTRICT ? args[1] :
                            number == SYS_TX_BEGIN ? CAP_CONTROL : number == SYS_FS_WRITE || number == SYS_FS_UNLINK || number == SYS_PIPE_SEND ? CAP_WRITE :
                            number == SYS_FS_READ || number == SYS_PIPE_RECEIVE ? CAP_READ : number == SYS_FS_OPEN ? CAP_READ | args[3] | (args[4] ? CAP_WRITE : 0) : 0;
        error_record(error, r.status, number, required);
    }
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
        handle_install(task_handle_table(), &object, CAP_ALL, &h) != OBJ_OK) return 0;
    r = syscall_invoke(SYS_HANDLE_DUPLICATE, h, 0, 0, 0, 0, 0);
    if (r.status || !r.value || r.value == h) return 0;
    duplicate = r.value;
    r = syscall_invoke(SYS_HANDLE_CLOSE, h, 0, 0, 0, 0, 0);
    if (r.status || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_CLOSE, h, 0, 0, 0, 0, 0);
    if (r.status != SYS_EBADHANDLE || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_CLOSE, duplicate, 0, 0, 0, 0, 0);
    if (r.status || object.refs != 1) return 0;
    if (handle_install(task_handle_table(), &object, CAP_ALL, &h) != OBJ_OK) return 0;
    r = syscall_invoke(SYS_HANDLE_CLONE, h, CAP_READ, 0, 0, 0, 0);
    if (r.status || !r.value) return 0;
    duplicate = r.value;
    r = syscall_invoke(SYS_HANDLE_DUPLICATE, duplicate, 0, 0, 0, 0, 0);
    if (r.status != SYS_EACCESS || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_RESTRICT, h, CAP_READ, 0, 0, 0, 0);
    if (r.status || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_RESTRICT, h, CAP_ALL, 0, 0, 0, 0);
    if (r.status != SYS_EACCESS || r.value) return 0;
    r = syscall_invoke(SYS_HANDLE_CLOSE, h, 0, 0, 0, 0, 0);
    if (r.status) return 0;
    r = syscall_invoke(SYS_HANDLE_CLOSE, duplicate, 0, 0, 0, 0, 0);
    if (r.status || object.refs != 1) return 0;
    object_put(&object);
    return destroyed == 1;
}
