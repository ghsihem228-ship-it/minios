#include "object.h"
/* No hosted assert dependency in the freestanding kernel. Invalid ownership
 * is an internal programming error; do not silently wrap the reference count. */
static _Noreturn void ownership_bug(void) {
#ifdef OBJECT_HOST_TEST
    __builtin_trap();
#else
    for (;;) __asm__ volatile ("cli; hlt");
#endif
}
enum object_status object_init(struct object *o, enum object_type type,
                               void (*destroy)(struct object *)) {
    if (!o || !destroy || type < OBJECT_TEST || type > OBJECT_PIPE) return OBJ_INVALID;
    *o = (struct object){ .refs = 1, .type = type, .destroy = destroy };
    return OBJ_OK;
}
enum object_status object_get(struct object *o) {
    if (!o || !o->refs) return OBJ_INVALID;
    if (o->refs == UINT32_MAX) return OBJ_OVERFLOW;
    o->refs++;
    return OBJ_OK;
}
void object_put(struct object *o) {
    if (!o || !o->refs) ownership_bug();
    if (--o->refs == 0) {
        /* Callback may free containing storage. Never touch o after this call. */
        void (*destroy)(struct object *) = o->destroy;
        destroy(o);
    }
}
