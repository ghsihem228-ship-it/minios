#include "handle.h"
void handle_table_init(struct handle_table *t) {
    for (unsigned i = 0; i < HANDLE_SLOTS; i++)
        t->slots[i] = (struct handle_slot){ .generation = 1 };
}
static struct handle_slot *find(struct handle_table *t, handle_t h) {
    uint32_t index = (uint32_t)h;
    uint32_t generation = h >> 32;
    if (!t || !index || index > HANDLE_SLOTS || !generation) return NULL;
    struct handle_slot *s = &t->slots[index - 1];
    return s->object && s->generation == generation ? s : NULL;
}
enum object_status handle_install(struct handle_table *t, struct object *o, cap_rights_t rights, handle_t *out) {
    if (!out) return OBJ_INVALID;
    *out = HANDLE_INVALID;
    if (!t || !o || (rights & ~CAP_ALL)) return OBJ_INVALID;
    for (unsigned i = 0; i < HANDLE_SLOTS; i++) {
        struct handle_slot *s = &t->slots[i];
        if (s->object || !s->generation) continue; /* generation zero is retired */
        enum object_status status = object_get(o);
        if (status != OBJ_OK) return status;
        s->object = o;
        s->rights = rights;
        *out = ((uint64_t)s->generation << 32) | (i + 1);
        return OBJ_OK;
    }
    return OBJ_FULL;
}
enum object_status handle_lookup(struct handle_table *t, handle_t h,
                                 enum object_type type, cap_rights_t required, struct object **out) {
    if (!out) return OBJ_INVALID;
    *out = NULL;
    if (required & ~CAP_ALL) return OBJ_INVALID;
    struct handle_slot *s = find(t, h);
    if (!s) return OBJ_INVALID;
    if ((s->rights & required) != required) return OBJ_DENIED;
    if (s->object->type != type) return OBJ_TYPE;
    enum object_status status = object_get(s->object);
    if (status == OBJ_OK) *out = s->object;
    return status;
}
enum object_status handle_pin(struct handle_table *t, handle_t h, cap_rights_t rights, struct object **out) {
    *out = NULL;
    if (rights & ~CAP_ALL) return OBJ_INVALID;
    struct handle_slot *s = find(t, h);
    if (!s) return OBJ_INVALID;
    if ((s->rights & rights) != rights) return OBJ_DENIED;
    enum object_status result = object_get(s->object);
    if (result == OBJ_OK) *out = s->object;
    return result;
}
enum object_status handle_duplicate(struct handle_table *t, handle_t h, handle_t *out) {
    struct handle_slot *s = find(t, h);
    if (!s) { if (out) *out = HANDLE_INVALID; return OBJ_INVALID; }
    return handle_clone(t, h, s->rights, out);
}
enum object_status handle_close(struct handle_table *t, handle_t h) {
    struct handle_slot *s = find(t, h);
    if (!s) return OBJ_INVALID;
    struct object *o = s->object;
    s->rights = 0;
    s->object = NULL; /* invalidate before invoking an arbitrary destructor */
    /* UINT32_MAX + 1 retires the slot: an old token can never revive by wrap. */
    s->generation++;
    object_put(o);
    return OBJ_OK;
}
void handle_table_clear(struct handle_table *t) {
    for (unsigned i = 0; i < HANDLE_SLOTS; i++) {
        struct handle_slot *s = &t->slots[i];
        if (s->object) {
            handle_t h = ((uint64_t)s->generation << 32) | (i + 1);
            (void)handle_close(t, h);
        }
    }
}

static enum object_status derive(struct handle_table *source, handle_t h,
                                 struct handle_table *destination, cap_rights_t rights,
                                 cap_rights_t authority, handle_t *out) {
    if (!out) return OBJ_INVALID;
    *out = HANDLE_INVALID;
    if (!destination || (rights & ~CAP_ALL)) return OBJ_INVALID;
    struct handle_slot *s = find(source, h);
    if (!s) return OBJ_INVALID;
    if (!(s->rights & authority) || (rights & ~s->rights)) return OBJ_DENIED;
    return handle_install(destination, s->object, rights, out);
}
enum object_status handle_clone(struct handle_table *t, handle_t h,
                                cap_rights_t rights, handle_t *out) {
    return derive(t, h, t, rights, CAP_DUPLICATE, out);
}
enum object_status handle_grant(struct handle_table *source, handle_t h,
                                struct handle_table *destination, cap_rights_t rights,
                                handle_t *out) {
    return derive(source, h, destination, rights, CAP_TRANSFER, out);
}
enum object_status handle_restrict(struct handle_table *t, handle_t h, cap_rights_t rights) {
    if (rights & ~CAP_ALL) return OBJ_INVALID;
    struct handle_slot *s = find(t, h);
    if (!s) return OBJ_INVALID;
    if (rights & ~s->rights) return OBJ_DENIED;
    s->rights = rights; /* Affects only this entry, not earlier copies or pins. */
    return OBJ_OK;
}
