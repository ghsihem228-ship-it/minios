#include "pipe.h"
#include "bytes.h"
#include <assert.h>
#include <stdio.h>
static void keep(struct object *o) { (void)o; }
int main(void) {
    struct handle_table parent, child;
    handle_table_init(&parent); handle_table_init(&child);
    const unsigned char message[] = {1,0,3,0,1,0,0,0,1};
    unsigned char received[PIPE_FRAME]; size_t size;
    for (unsigned round = 0; round < 1000; round++) {
        handle_t r, w, copy, granted;
        assert(!pipe_create(&parent, &r, &w));
        struct object *reader, *writer;
        assert(handle_lookup(&parent, r, OBJECT_PIPE, CAP_WAIT, &reader) == OBJ_OK);
        assert(handle_lookup(&parent, w, OBJECT_PIPE, CAP_WAIT, &writer) == OBJ_OK);
        uint64_t events;
        assert(!pipe_signals(reader, SIGNAL_READABLE, &events) && !events);
        assert(pipe_signals(reader, SIGNAL_WRITABLE, &events) == SYS_EINVAL);
        assert(!pipe_signals(writer, SIGNAL_WRITABLE, &events) && events == SIGNAL_WRITABLE);
        for (unsigned i = 0; i < 4; i++) assert(!pipe_send(&parent, w, message, sizeof(message)));
        assert(pipe_send(&parent, w, message, sizeof(message)) == SYS_EAGAIN);
        assert(!pipe_signals(writer, SIGNAL_WRITABLE, &events) && !events);
        assert(handle_duplicate(&parent, w, &copy) == OBJ_OK);
        assert(handle_grant(&parent, w, &child, CAP_WRITE, &granted) == OBJ_OK);
        struct object *denied;
        assert(handle_pin(&child, granted, CAP_WAIT, &denied) == OBJ_DENIED && !denied);
        assert(!handle_close(&parent, w) && !handle_close(&parent, copy));
        object_put(writer); /* The child's explicitly granted endpoint remains alive. */
        assert(!pipe_signals(reader, SIGNAL_PEER_CLOSED, &events) && !events);
        assert(!handle_close(&child, granted));
        assert(!pipe_signals(reader, SIGNAL_READABLE | SIGNAL_PEER_CLOSED, &events) && events == 10);
        for (unsigned i = 0; i < 4; i++) {
            assert(!pipe_receive(&parent, r, received, sizeof(received), 3, &size));
            assert(size == sizeof(message) && bytes_equal(received, message, size));
        }
        assert(!pipe_receive(&parent, r, received, sizeof(received), 3, &size) && !size);
        object_put(reader); assert(!handle_close(&parent, r));
        assert(!pipe_create(&parent, &r, &w));
        assert(!handle_close(&parent, r));
        assert(pipe_send(&parent, w, message, sizeof(message)) == SYS_EPIPE);
        assert(!handle_close(&parent, w));
    }
    /* Force failure after allocating only the first endpoint handle. */
    struct object sentinel; handle_t fills[31], r, w;
    assert(object_init(&sentinel, OBJECT_TEST, keep) == OBJ_OK);
    for (unsigned i = 0; i < 31; i++) assert(handle_install(&parent, &sentinel, 0, &fills[i]) == OBJ_OK);
    assert(pipe_create(&parent, &r, &w) == SYS_ENOSPC && !r && !w);
    for (unsigned i = 0; i < 31; i++) assert(!handle_close(&parent, fills[i]));
    assert(sentinel.refs == 1); object_put(&sentinel);
    handle_t ends[4][2];
    for (unsigned i = 0; i < 4; i++) assert(!pipe_create(&parent, &ends[i][0], &ends[i][1]));
    assert(pipe_create(&parent, &r, &w) == SYS_ENOSPC);
    handle_table_clear(&parent); handle_table_clear(&child);
    puts("PASS IPC host: 1000 endpoint lifecycles, EOF/drain, EPIPE, duplicate/grant lifetime, readiness and partial-create rollback under ASan/UBSan");
}
