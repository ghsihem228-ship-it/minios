#include "syscall.h"
#include <assert.h>
#include <stdio.h>
static unsigned destroyed;
static void destroy(struct object *o) { (void)o; destroyed++; }
static uint32_t seed = 0x5678abcd;
static uint32_t random_word(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
int main(void) {
    struct handle_table table;
    struct object object;
    uint64_t args[6] = {1, 0, 0, 0, 0, 0};
    struct syscall_result r;
    handle_t h, slots[HANDLE_SLOTS - 1];
    handle_table_init(&table);
    r = syscall_execute(&table, SYS_ABI_QUERY, args);
    assert(r.status == SYS_OK && r.value == SYS_ABI_VERSION);
    assert(object_init(&object, OBJECT_TEST, destroy) == OBJ_OK);
    assert(handle_install(&table, &object, CAP_ALL, &h) == OBJ_OK);
    /* Every reserved argument register is validated before side effects. */
    for (uint64_t nr = 0; nr < 3; nr++) {
        for (unsigned i = 1; i < 6; i++) {
            args[0] = nr == 0 ? 1 : h;
            args[i] = UINT64_MAX;
            r = syscall_execute(&table, nr, args);
            assert(r.status == SYS_EINVAL && !r.value && object.refs == 2);
            args[i] = 0;
        }
    }
    args[0] = h;
    r = syscall_execute(&table, SYS_HANDLE_DUPLICATE, args);
    assert(r.status == SYS_OK && r.value != h);
    handle_t duplicate = r.value;
    for (unsigned i = 0; i < HANDLE_SLOTS - 2; i++)
        assert(handle_install(&table, &object, CAP_ALL, &slots[i]) == OBJ_OK);
    r = syscall_execute(&table, SYS_HANDLE_DUPLICATE, args);
    assert(r.status == SYS_ENOSPC && !r.value && object.refs == HANDLE_SLOTS + 1);
    handle_table_clear(&table);
    r = syscall_execute(&table, SYS_HANDLE_CLOSE, args);
    assert(r.status == SYS_EBADHANDLE && !r.value);
    args[0] = duplicate;
    r = syscall_execute(&table, SYS_HANDLE_DUPLICATE, args);
    assert(r.status == SYS_EBADHANDLE && !r.value);
    assert(handle_install(&table, &object, CAP_ALL, &h) == OBJ_OK);
    args[0] = h;
    object.refs = UINT32_MAX;
    r = syscall_execute(&table, SYS_HANDLE_DUPLICATE, args);
    assert(r.status == SYS_EOVERFLOW && !r.value);
    object.refs = 2;
    /* Scalar fuzz: independent rejection/version oracle; random raw numbers,
     * all six args and malformed/stale tokens must never dereference them. */
    for (unsigned i = 0; i < 100000; i++) {
        uint64_t nr = ((uint64_t)random_word() << 32) | random_word();
        if (i % 2) nr = i % 3;
        for (unsigned j = 0; j < 6; j++)
            args[j] = i % 4 == 0 ? ((uint64_t)random_word() << 32) | random_word() : 0;
        int reserved = 0;
        for (unsigned j = 1; j < 6; j++) if (args[j]) reserved = 1;
        int64_t expected = nr > 2 ? SYS_ENOSYS : reserved ? SYS_EINVAL :
                           nr == 0 ? SYS_EABI : SYS_EBADHANDLE;
        /* Zero args[0] cannot negotiate v1 or name a live handle. */
        r = syscall_execute(&table, nr, args);
        assert(r.status == expected && !r.value && object.refs == 2);
    }
    handle_table_clear(&table);
    object_put(&object);
    assert(destroyed == 1);
    puts("PASS syscalls host: validation, lifetimes, capacity, overflow, 100000 scalar fuzz cases");
}
