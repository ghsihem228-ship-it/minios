#include "process.h"
#include "user_vm.h"
#include "pipe.h"

static int64_t output_range(uint64_t address, size_t length) {
    if (!vm_user_check(address, length, 1)) return SYS_EFAULT;
    return vm_user_prepare(address, length) ? SYS_OK : SYS_ENOMEM;
}
static const unsigned arguments[18] = {1,1,1,2,2,2,1,5,3,3,2,3,1,0,0,1,3,4};
int services_dispatch(struct interrupt_frame *f, struct syscall_result *result) {
    uint64_t number = f->rax;
    if (number < SYS_EXIT || number > SYS_PIPE_RECEIVE) return 0;
    *result = (struct syscall_result){SYS_EINVAL, 0};
    uint64_t a[6] = {f->rdi, f->rsi, f->rdx, f->r10, f->r8, f->r9};
    for (unsigned i = arguments[number]; i < 6; i++) if (a[i]) return 1;
    if (!user_context.active || (f->cs & 3) != 3) { result->status = SYS_EACCESS; return 1; }
    struct handle_table *t = &user_context.handles;
    if (number == SYS_EXIT) { user_finish(f, (int64_t)(a[0] & 255)); return 2; }
    if (user_context.transaction && number != SYS_TX_COMMIT && number != SYS_TX_ROLLBACK &&
        number != SYS_FS_READ && number != SYS_FS_WRITE && number != SYS_FS_SEEK) {
        result->status = SYS_EBUSY; return 1;
    }
    unsigned char buffer[FS_BYTES]; size_t done = 0; int64_t status = SYS_EINVAL;
    if (number == SYS_FS_OPEN) {
        if (!a[2] || a[2] > FS_NAME || a[4] > 1) goto done;
        if (!vm_user_read(buffer, a[1], a[2])) { status = SYS_EFAULT; goto done; }
        status = fs_open(t, a[0], (char *)buffer, a[2], a[3], (int)a[4], &result->value);
    } else if (number == SYS_FS_READ || number == SYS_FS_WRITE) {
        if (a[2] > sizeof(buffer)) goto done;
        if (number == SYS_FS_READ) {
            if ((status = output_range(a[1], a[2]))) goto done;
            status = fs_read(t, a[0], buffer, a[2], &done);
            if (!status && !vm_user_write(a[1], buffer, done)) status = SYS_EFAULT;
        } else {
            if (!vm_user_read(buffer, a[1], a[2])) { status = SYS_EFAULT; goto done; }
            status = fs_write(t, a[0], buffer, a[2], &done);
        }
        result->value = done;
    } else if (number == SYS_FS_SEEK) status = fs_seek(t, a[0], a[1]);
    else if (number == SYS_FS_UNLINK) {
        if (!a[2] || a[2] > FS_NAME) goto done;
        if (!vm_user_read(buffer, a[1], a[2])) { status = SYS_EFAULT; goto done; }
        status = fs_unlink(t, a[0], (char *)buffer, a[2]);
    } else if (number == SYS_TX_BEGIN) {
        if (!user_context.filesystem) { status = SYS_EBADHANDLE; goto done; }
        struct object *o;
        enum object_status s = handle_lookup(t, a[0], OBJECT_DIRECTORY, CAP_CONTROL, &o);
        if (s != OBJ_OK) { status = s == OBJ_DENIED ? SYS_EACCESS : SYS_EBADHANDLE; goto done; }
        int correct = o == &user_context.filesystem->root; object_put(o);
        if (!correct) { status = SYS_EACCESS; goto done; }
        if (!vm_snapshot_begin()) { status = SYS_EBUSY; goto done; }
        if (!fs_tx_begin(user_context.filesystem)) { vm_snapshot_end(1); status = SYS_EBUSY; goto done; }
        __asm__ volatile ("fxsave64 %0" : "=m"(user_context.transaction_fx) :: "memory");
        user_context.transaction_frame = *f; user_context.transaction = 1; user_context.deadline = timer_now(); status = SYS_OK;
    } else if (number == SYS_TX_COMMIT || number == SYS_TX_ROLLBACK) {
        if (!user_context.transaction) { status = SYS_EINVAL; goto done; }
        int rollback = number == SYS_TX_ROLLBACK;
        if (!fs_tx_end(user_context.filesystem, rollback)) { status = SYS_ECONFLICT; goto done; }
        vm_snapshot_end(rollback); user_context.transaction = 0;
        if (rollback) {
            *f = user_context.transaction_frame; result->value = 1;
            __asm__ volatile ("fxrstor64 %0" :: "m"(user_context.transaction_fx) : "memory");
        }
        status = SYS_OK;
    } else if (number == SYS_PIPE_CREATE) {
        handle_t ends[2];
        if ((status = output_range(a[0], sizeof(ends)))) goto done;
        status = pipe_create(t, &ends[0], &ends[1]);
        if (!status) (void)vm_user_write(a[0], ends, sizeof(ends));
    } else if (number == SYS_PIPE_SEND) {
        if (a[2] > PIPE_FRAME) goto done;
        if (!vm_user_read(buffer, a[1], a[2])) { status = SYS_EFAULT; goto done; }
        status = pipe_send(t, a[0], buffer, a[2]);
    } else if (number == SYS_PIPE_RECEIVE) {
        if (a[2] > PIPE_FRAME || a[3] > UINT16_MAX) goto done;
        if ((status = output_range(a[1], a[2]))) goto done;
        status = pipe_receive(t, a[0], buffer, a[2], (uint16_t)a[3], &done);
        if (!status) (void)vm_user_write(a[1], buffer, done);
        result->value = done;
    }
done:
    result->status = status;
    if (status) result->value = 0;
    return 1;
}
