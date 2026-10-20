/* Run the actual portable kernel code under ASan/UBSan, not a second implementation. */
#include "handle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned destroyed;
struct sample { struct object header; unsigned payload; };
static void destroy_heap(struct object *o) { destroyed++; free(o); }
static void destroy_stack(struct object *o) { (void)o; destroyed++; }
static uint32_t rng = 0x31415926;
static uint32_t random_word(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
int main(void) {
    assert(handle_selftest());
    struct handle_table table;
    handle_table_init(&table);
    struct sample *s = malloc(sizeof(*s));
    assert(s);
    s->payload = 42;
    assert(object_init(&s->header, OBJECT_TEST, destroy_heap) == OBJ_OK);
    handle_t h;
    assert(handle_install(&table, &s->header, CAP_ALL, &h) == OBJ_OK);
    object_put(&s->header);
    struct object *pin;
    assert(handle_lookup(&table, h, OBJECT_TEST, CAP_READ, &pin) == OBJ_OK);
    assert(handle_close(&table, h) == OBJ_OK && !destroyed);
    assert(((struct sample *)pin)->payload == 42);
    object_put(pin); /* frees heap storage only when last lookup pin goes away */
    assert(destroyed == 1);

    /* Independent live-token set tracks ownership; random malformed tokens must
     * fail unless they exactly name a currently live local entry. */
    struct object object;
    assert(object_init(&object, OBJECT_TEST, destroy_stack) == OBJ_OK);
    handle_t live[HANDLE_SLOTS] = {0}, stale[HANDLE_SLOTS] = {0};
    for (unsigned iteration = 0; iteration < 100000; iteration++) {
        unsigned index = random_word() % HANDLE_SLOTS;
        unsigned operation = random_word() % 5;
        if (operation == 0 && !live[index]) {
            assert(handle_install(&table, &object, CAP_ALL, &live[index]) == OBJ_OK);
        } else if (operation == 1 && live[index]) {
            stale[index] = live[index];
            assert(handle_close(&table, live[index]) == OBJ_OK);
            live[index] = 0;
        } else if (operation == 2) {
            handle_t token = ((uint64_t)random_word() << 32) | random_word();
            unsigned found = 0;
            for (unsigned j = 0; j < HANDLE_SLOTS; j++)
                if (live[j] && live[j] == token) found = 1;
            struct object *out;
            enum object_status result = handle_lookup(&table, token, OBJECT_TEST, CAP_READ, &out);
            assert(result == (found ? OBJ_OK : OBJ_INVALID));
            if (found) object_put(out); else assert(!out);
        } else if (operation == 3 && live[index]) {
            struct object *out;
            assert(handle_lookup(&table, live[index], OBJECT_FILE, CAP_READ, &out) == OBJ_TYPE && !out);
            assert(handle_lookup(&table, live[index], OBJECT_TEST, CAP_READ, &out) == OBJ_OK);
            object_put(out);
        } else if (operation == 4 && stale[index]) {
            struct object *out;
            assert(handle_lookup(&table, stale[index], OBJECT_TEST, CAP_READ, &out) == OBJ_INVALID && !out);
        }
        unsigned count = 0;
        for (unsigned j = 0; j < HANDLE_SLOTS; j++) if (live[j]) count++;
        assert(object.refs == count + 1);
    }
    handle_table_clear(&table);
    assert(object.refs == 1);
    object_put(&object);
    assert(destroyed == 2);
    puts("PASS handles host: lifetime, types, 100000 model/fuzz operations under ASan/UBSan");
    return 0;
}
