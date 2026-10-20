#include "syscall.h"
/* Pure scalar policy: no user pointer dereferences, allocation, blocking or yield. */
struct syscall_result syscall_execute(struct handle_table *table, uint64_t number,
                                      const uint64_t args[6]) {
    struct syscall_result result = { SYS_OK, 0 };
    if (number > SYS_HANDLE_DUPLICATE) {
        result.status = SYS_ENOSYS;
        return result;
    }
    for (unsigned i = 1; i < 6; i++)
        if (args[i]) { result.status = SYS_EINVAL; return result; }
    if (number == SYS_ABI_QUERY) {
        if (args[0] != SYS_ABI_MAJOR) result.status = SYS_EABI;
        else result.value = SYS_ABI_VERSION;
        return result;
    }
    enum object_status status;
    if (number == SYS_HANDLE_CLOSE) status = handle_close(table, args[0]);
    else status = handle_duplicate(table, args[0], &result.value);
    switch (status) {
    case OBJ_OK: break;
    case OBJ_FULL: result.status = SYS_ENOSPC; break;
    case OBJ_OVERFLOW: result.status = SYS_EOVERFLOW; break;
    default: result.status = SYS_EBADHANDLE; break;
    }
    if (result.status) result.value = 0;
    return result;
}
