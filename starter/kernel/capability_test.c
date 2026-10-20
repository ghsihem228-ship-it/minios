#include "handle.h"
static unsigned destroyed;
static void destroy(struct object *o) { (void)o; destroyed++; }
int capability_selftest(void) {
    struct handle_table parent, child;
    struct object object, *pin;
    handle_t source, derived;
    destroyed = 0;
    handle_table_init(&parent);
    handle_table_init(&child);
    if (object_init(&object, OBJECT_TEST, destroy) != OBJ_OK) return 0;
    /* All 4096 source/request pairs, independent expected subset rule. */
    for (cap_rights_t rights = 0; rights <= CAP_ALL; rights++) {
        if (handle_install(&parent, &object, rights, &source) != OBJ_OK) return 0;
        for (cap_rights_t requested = 0; requested <= CAP_ALL; requested++) {
            int subset = (requested & rights) == requested;
            enum object_status expected = subset && (rights & CAP_DUPLICATE) ? OBJ_OK : OBJ_DENIED;
            enum object_status status = handle_clone(&parent, source, requested, &derived);
            if (status != expected) return 0;
            if (status == OBJ_OK) {
                enum object_status access = requested & CAP_WRITE ? OBJ_OK : OBJ_DENIED;
                if (handle_lookup(&parent, derived, OBJECT_TEST, CAP_WRITE, &pin) != access) return 0;
                if (pin) object_put(pin);
                if (handle_close(&parent, derived) != OBJ_OK) return 0;
            } else if (derived) return 0;
            expected = subset && (rights & CAP_TRANSFER) ? OBJ_OK : OBJ_DENIED;
            status = handle_grant(&parent, source, &child, requested, &derived);
            if (status != expected) return 0;
            if (status == OBJ_OK) {
                enum object_status access = requested & CAP_READ ? OBJ_OK : OBJ_DENIED;
                if (handle_lookup(&child, derived, OBJECT_TEST, CAP_READ, &pin) != access) return 0;
                if (pin) object_put(pin);
                if (handle_close(&child, derived) != OBJ_OK) return 0;
            } else if (derived) return 0;
            if (object.refs != 2) return 0;
        }
        if (handle_close(&parent, source) != OBJ_OK || object.refs != 1) return 0;
    }
    if (handle_install(&parent, &object, CAP_ALL, &source) != OBJ_OK) return 0;
    if (handle_clone(&parent, source, CAP_READ | CAP_WRITE, &derived) != OBJ_OK) return 0;
    if (handle_restrict(&parent, source, CAP_READ) != OBJ_OK ||
        handle_restrict(&parent, source, CAP_WRITE) != OBJ_DENIED) return 0;
    /* Narrowing parent is not revocation of previously derived entries. */
    if (handle_lookup(&parent, derived, OBJECT_TEST, CAP_WRITE, &pin) != OBJ_OK) return 0;
    object_put(pin);
    if (handle_close(&parent, derived) != OBJ_OK) return 0;
    if (handle_clone(&parent, source, CAP_READ, &derived) != OBJ_DENIED || derived) return 0;
    if (handle_grant(&parent, source, &child, CAP_READ, &derived) != OBJ_DENIED || derived) return 0;
    if (handle_restrict(&parent, source, 0) != OBJ_OK ||
        handle_lookup(&parent, source, OBJECT_TEST, CAP_READ, &pin) != OBJ_DENIED || pin) return 0;
    if (handle_close(&parent, source) != OBJ_OK) return 0; /* close needs no rights */
    if (handle_install(&parent, &object, ~CAP_ALL, &source) != OBJ_INVALID || source) return 0;
    object_put(&object);
    return destroyed == 1;
}
