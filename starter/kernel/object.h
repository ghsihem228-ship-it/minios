#ifndef STARTER_OBJECT_H
#define STARTER_OBJECT_H
#include <stdint.h>
/* Internal types only: the concrete file/process/etc. implementations come later. */
enum object_type { OBJECT_TEST = 1, OBJECT_FILE, OBJECT_PROCESS, OBJECT_THREAD,
                   OBJECT_SOCKET, OBJECT_DEVICE, OBJECT_TIMER };
enum object_status { OBJ_OK, OBJ_INVALID, OBJ_TYPE, OBJ_FULL, OBJ_OVERFLOW, OBJ_DENIED };
struct object {
    uint32_t refs;
    enum object_type type;
    void (*destroy)(struct object *);
};
/* Caller owns storage; successful init gives one creator reference. Never
 * reinitialize a live object. Destroy must not yield/block or resurrect it. */
enum object_status object_init(struct object *, enum object_type,
                               void (*destroy)(struct object *));
enum object_status object_get(struct object *);
void object_put(struct object *); /* requires one owned reference */
#endif
