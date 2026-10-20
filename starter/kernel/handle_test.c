#include "handle.h"
static unsigned destroyed;
static void destroy(struct object *o) { (void)o; destroyed++; }
int handle_selftest(void) {
    struct handle_table table;
    struct object object, *held, *invalid;
    handle_t first, duplicate, next, handles[HANDLE_SLOTS];
    destroyed = 0;
    handle_table_init(&table);
    if (object_init(&object, OBJECT_TEST, destroy) != OBJ_OK) return 0;
    if (handle_install(&table, &object, &first) != OBJ_OK || object.refs != 2) return 0;
    if (handle_lookup(&table, first, OBJECT_FILE, &held) != OBJ_TYPE || held) return 0;
    if (handle_lookup(&table, first, OBJECT_TEST, &held) != OBJ_OK) return 0;
    if (handle_duplicate(&table, first, &duplicate) != OBJ_OK || duplicate == first) return 0;
    if (handle_close(&table, first) != OBJ_OK || handle_close(&table, first) != OBJ_INVALID) return 0;
    if (handle_install(&table, &object, &next) != OBJ_OK || next == first) return 0;
    if (handle_lookup(&table, first, OBJECT_TEST, &invalid) != OBJ_INVALID) return 0;
    object_put(held); /* lookup pin survives closure/reuse of the original slot */
    handle_table_clear(&table);
    if (object.refs != 1 || destroyed) return 0;
    for (unsigned i = 0; i < HANDLE_SLOTS; i++)
        if (handle_install(&table, &object, &handles[i]) != OBJ_OK) return 0;
    if (handle_install(&table, &object, &next) != OBJ_FULL || next) return 0;
    handle_table_clear(&table);
    /* Force generation exhaustion without billions of operations. */
    table.slots[0].generation = UINT32_MAX;
    if (handle_install(&table, &object, &next) != OBJ_OK) return 0;
    if (handle_close(&table, next) != OBJ_OK || table.slots[0].generation) return 0;
    if (handle_install(&table, &object, &next) != OBJ_OK || (uint32_t)next == 1) return 0;
    handle_table_clear(&table);
    object.refs = UINT32_MAX;
    if (handle_install(&table, &object, &next) != OBJ_OVERFLOW || next) return 0;
    object.refs = 1;
    object_put(&object);
    return destroyed == 1;
}
