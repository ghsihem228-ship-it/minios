#include "syscall.h"
#include <assert.h>
#include <stdio.h>
static unsigned destroyed;
static void destroy(struct object *o) { (void)o; destroyed++; }
static uint32_t seed = 0x718293ab;
static uint32_t random_word(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
struct model_entry { handle_t handle; cap_rights_t rights; };
int main(void) {
    assert(capability_selftest());
    struct handle_table tables[2];
    struct model_entry model[2][HANDLE_SLOTS] = {0};
    struct object object;
    for (unsigned i = 0; i < 2; i++) handle_table_init(&tables[i]);
    assert(object_init(&object, OBJECT_TEST, destroy) == OBJ_OK);
    for (unsigned iteration = 0; iteration < 100000; iteration++) {
        unsigned table = random_word() % 2, index = random_word() % HANDLE_SLOTS;
        unsigned operation = random_word() % 5;
        cap_rights_t requested = random_word() & 127; /* includes unknown bit 6 */
        struct model_entry *entry = &model[table][index];
        if (!entry->handle) {
            /* Trusted mint in the fixture re-seeds authority; not a user API. */
            entry->rights = random_word() & CAP_ALL;
            assert(handle_install(&tables[table], &object, entry->rights, &entry->handle) == OBJ_OK);
        } else if (operation == 0 || operation == 1) {
            unsigned destination = operation ? table ^ 1 : table;
            unsigned free_slot;
            for (free_slot = 0; free_slot < HANDLE_SLOTS; free_slot++)
                if (!model[destination][free_slot].handle) break;
            cap_rights_t needed = operation ? CAP_TRANSFER : CAP_DUPLICATE;
            enum object_status expected = requested & ~CAP_ALL ? OBJ_INVALID :
                !(entry->rights & needed) || (requested & ~entry->rights) ? OBJ_DENIED :
                free_slot == HANDLE_SLOTS ? OBJ_FULL : OBJ_OK;
            handle_t out;
            enum object_status result = operation ?
                handle_grant(&tables[table], entry->handle, &tables[destination], requested, &out) :
                handle_clone(&tables[table], entry->handle, requested, &out);
            assert(result == expected);
            if (result == OBJ_OK) model[destination][free_slot] = (struct model_entry){out, requested};
            else assert(!out);
        } else if (operation == 2) {
            enum object_status expected = requested & ~CAP_ALL ? OBJ_INVALID :
                                          requested & ~entry->rights ? OBJ_DENIED : OBJ_OK;
            assert(handle_restrict(&tables[table], entry->handle, requested) == expected);
            if (expected == OBJ_OK) entry->rights = requested;
        } else if (operation == 3) {
            struct object *pin;
            enum object_status expected = requested & ~CAP_ALL ? OBJ_INVALID :
                                          requested & ~entry->rights ? OBJ_DENIED : OBJ_OK;
            assert(handle_lookup(&tables[table], entry->handle, OBJECT_TEST, requested, &pin) == expected);
            if (expected == OBJ_OK) { assert(pin == &object); object_put(pin); }
            else assert(!pin);
        } else {
            assert(handle_close(&tables[table], entry->handle) == OBJ_OK);
            entry->handle = 0;
        }
        unsigned references = 1;
        for (unsigned t = 0; t < 2; t++)
            for (unsigned i = 0; i < HANDLE_SLOTS; i++) if (model[t][i].handle) references++;
        assert(object.refs == references);
    }
    for (unsigned i = 0; i < 2; i++) handle_table_clear(&tables[i]);
    assert(object.refs == 1);
    object_put(&object);
    assert(destroyed == 1);

    /* Numeric handle equality is table-local, not globally transferable. */
    struct handle_table fresh;
    handle_table_init(&fresh);
    uint64_t args[6] = {0, 0, 0, 0, 0, 0};
    struct syscall_result r;
    assert(object_init(&object, OBJECT_TEST, destroy) == OBJ_OK);
    handle_t full;
    assert(handle_install(&fresh, &object, CAP_ALL, &full) == OBJ_OK);
    args[0] = full; args[1] = CAP_READ;
    r = syscall_execute(&fresh, SYS_HANDLE_CLONE, args);
    assert(r.status == SYS_OK && r.value);
    handle_t read_only = r.value;
    args[0] = read_only; args[1] = 0;
    r = syscall_execute(&fresh, SYS_HANDLE_DUPLICATE, args);
    assert(r.status == SYS_EACCESS && !r.value);
    args[1] = CAP_ALL;
    r = syscall_execute(&fresh, SYS_HANDLE_RESTRICT, args);
    assert(r.status == SYS_EACCESS && !r.value);
    args[1] = UINT64_MAX;
    r = syscall_execute(&fresh, SYS_HANDLE_CLONE, args);
    assert(r.status == SYS_EINVAL && !r.value);
    args[1] = CAP_READ;
    for (uint64_t number = SYS_HANDLE_RESTRICT; number <= SYS_HANDLE_CLONE; number++) {
        for (unsigned i = 2; i < 6; i++) {
            args[i] = 1;
            r = syscall_execute(&fresh, number, args);
            assert(r.status == SYS_EINVAL && !r.value);
            args[i] = 0;
        }
    }
    handle_table_clear(&fresh);
    object_put(&object);
    puts("PASS capabilities host: 4096 mask pairs, 100000 two-table model operations, syscall denials");
}
