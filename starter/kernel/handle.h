#ifndef STARTER_HANDLE_H
#define STARTER_HANDLE_H
#include "object.h"
#include <stddef.h>
#define HANDLE_SLOTS 32u
typedef uint64_t handle_t;
#define HANDLE_INVALID UINT64_C(0)
struct handle_slot { struct object *object; uint32_t generation; };
struct handle_table { struct handle_slot slots[HANDLE_SLOTS]; };
/* Single CPU, cooperative task context only. No operation/destructor may yield.
 * Initialize a NEW table exactly once; clear preserves generation history. */
void handle_table_init(struct handle_table *);
void handle_table_clear(struct handle_table *);
enum object_status handle_install(struct handle_table *, struct object *, handle_t *);
/* Typed lookup returns a new owned reference. Caller must object_put it. */
enum object_status handle_lookup(struct handle_table *, handle_t,
                                 enum object_type, struct object **);
enum object_status handle_duplicate(struct handle_table *, handle_t, handle_t *);
enum object_status handle_close(struct handle_table *, handle_t);
int handle_selftest(void);
#endif
