#include "pipe.h"
#include "bytes.h"
#define PIPE_COUNT 4u
#define PIPE_QUEUE 4u
struct message { size_t size; unsigned char bytes[PIPE_FRAME]; };
struct pipe_object;
struct endpoint { struct object object; struct pipe_object *pipe; unsigned write; };
struct pipe_object {
    struct endpoint read, write;
    unsigned used, reader_closed, writer_closed, head, count;
    struct message messages[PIPE_QUEUE];
};
static struct pipe_object pipes[PIPE_COUNT];
static unsigned u16(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t u32(const unsigned char *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int utf8(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n;) {
        uint32_t c = p[i++], minimum; unsigned tail;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { c &= 31; tail = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { c &= 15; tail = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { c &= 7; tail = 3; minimum = 0x10000; }
        else return 0;
        if (tail > n - i) return 0;
        while (tail--) { unsigned b = p[i++]; if ((b & 0xc0) != 0x80) return 0; c = (c << 6) | (b & 63); }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
    }
    return 1;
}
static int scalar(unsigned type, const unsigned char *p, size_t n) {
    if (type == 1 || type == 2) return n == 8;
    if (type == 3) return n == 1 && p[0] <= 1;
    if (type == 4) return utf8(p, n);
    return type == 5;
}
int wire_validate(const void *data, size_t n, uint16_t *type) {
    const unsigned char *p = data;
    if (n < 8 || n > PIPE_FRAME || u16(p) != 1 || u32(p + 4) != n - 8) return 0;
    unsigned t = u16(p + 2);
    if (t == 6) {
        size_t offset = 8; unsigned previous = 0, fields = 0;
        while (offset < n) {
            if (n - offset < 8 || ++fields > 8) return 0;
            unsigned id = u16(p + offset), field_type = u16(p + offset + 2);
            uint32_t length = u32(p + offset + 4);
            offset += 8;
            if (!id || id <= previous || length > n - offset ||
                !scalar(field_type, p + offset, length)) return 0;
            previous = id; offset += length;
        }
    } else if (!scalar(t, p + 8, n - 8)) return 0;
    *type = (uint16_t)t; return 1;
}
static int64_t status(enum object_status s) {
    return s == OBJ_DENIED ? SYS_EACCESS : s == OBJ_FULL ? SYS_ENOSPC :
           s == OBJ_OVERFLOW ? SYS_EOVERFLOW : SYS_EBADHANDLE;
}
static void destroy(struct object *o) {
    struct endpoint *e = (struct endpoint *)o;
    struct pipe_object *p = e->pipe;
    if (e->write) p->writer_closed = 1; else p->reader_closed = 1;
    if (p->reader_closed && p->writer_closed) p->used = 0;
}
int64_t pipe_create(struct handle_table *t, handle_t *read, handle_t *write) {
    *read = *write = 0; unsigned i;
    for (i = 0; i < PIPE_COUNT && pipes[i].used; i++) {}
    if (i == PIPE_COUNT) return SYS_ENOSPC;
    struct pipe_object *p = &pipes[i]; bytes_zero(p, sizeof(*p)); p->used = 1;
    p->read.pipe = p->write.pipe = p; p->write.write = 1;
    (void)object_init(&p->read.object, OBJECT_PIPE, destroy);
    (void)object_init(&p->write.object, OBJECT_PIPE, destroy);
    enum object_status s = handle_install(t, &p->read.object, CAP_READ | CAP_WAIT | CAP_DUPLICATE | CAP_TRANSFER, read);
    if (s == OBJ_OK) {
        s = handle_install(t, &p->write.object, CAP_WRITE | CAP_WAIT | CAP_DUPLICATE | CAP_TRANSFER, write);
        if (s != OBJ_OK) { (void)handle_close(t, *read); *read = 0; }
    }
    object_put(&p->read.object); object_put(&p->write.object);
    return s == OBJ_OK ? SYS_OK : status(s);
}
/* The caller pins an endpoint with WAIT authority before polling its signals. */
int64_t pipe_signals(struct object *o, uint64_t requested, uint64_t *observed) {
    struct endpoint *e = (struct endpoint *)o; struct pipe_object *p = e->pipe;
    uint64_t allowed = SIGNAL_PEER_CLOSED | (e->write ? SIGNAL_WRITABLE : SIGNAL_READABLE);
    if (!requested || (requested & ~allowed)) return SYS_EINVAL;
    uint64_t ready = 0;
    if (e->write) {
        if (p->reader_closed) ready |= SIGNAL_PEER_CLOSED;
        else if (p->count < PIPE_QUEUE) ready |= SIGNAL_WRITABLE;
    } else {
        if (p->writer_closed) ready |= SIGNAL_PEER_CLOSED;
        if (p->count || p->writer_closed) ready |= SIGNAL_READABLE;
    }
    *observed = ready & requested; return SYS_OK;
}
int64_t pipe_send(struct handle_table *t, handle_t h, const void *data, size_t n) {
    uint16_t type;
    if (!wire_validate(data, n, &type)) return SYS_EFORMAT;
    struct object *o;
    enum object_status s = handle_lookup(t, h, OBJECT_PIPE, CAP_WRITE, &o);
    if (s != OBJ_OK) return status(s);
    struct pipe_object *p = ((struct endpoint *)o)->pipe; int64_t result = SYS_OK;
    if (p->reader_closed) result = SYS_EPIPE;
    else if (p->count == PIPE_QUEUE) result = SYS_EAGAIN;
    else {
        struct message *m = &p->messages[(p->head + p->count) % PIPE_QUEUE];
        bytes_copy(m->bytes, data, n); m->size = n; p->count++;
    }
    object_put(o); return result;
}
int64_t pipe_receive(struct handle_table *t, handle_t h, void *data, size_t capacity, uint16_t expected, size_t *done) {
    *done = 0; struct object *o;
    enum object_status s = handle_lookup(t, h, OBJECT_PIPE, CAP_READ, &o);
    if (s != OBJ_OK) return status(s);
    struct pipe_object *p = ((struct endpoint *)o)->pipe; int64_t result = SYS_OK;
    if (!p->count) result = p->writer_closed ? SYS_OK : SYS_EAGAIN;
    else {
        struct message *m = &p->messages[p->head];
        if (expected != u16(m->bytes + 2)) result = SYS_EFORMAT;
        else if (capacity < m->size) result = SYS_ENOSPC;
        else { bytes_copy(data, m->bytes, m->size); *done = m->size; p->head = (p->head + 1) % PIPE_QUEUE; p->count--; }
    }
    object_put(o); return result;
}
int pipe_selftest(void) {
    struct handle_table t; handle_t read, write; size_t n; unsigned char result[PIPE_FRAME];
    const unsigned char valid[] = {1,0,3,0,1,0,0,0,1};
    uint16_t type;
    handle_table_init(&t);
    if (!wire_validate(valid, sizeof(valid), &type) || type != 3 || pipe_create(&t, &read, &write)) return 0;
    if (pipe_send(&t, read, valid, sizeof(valid)) != SYS_EACCESS) return 0;
    for (unsigned i = 0; i < 4; i++) if (pipe_send(&t, write, valid, sizeof(valid))) return 0;
    if (pipe_send(&t, write, valid, sizeof(valid)) != SYS_EAGAIN) return 0;
    if (pipe_receive(&t, read, result, sizeof(result), 1, &n) != SYS_EFORMAT) return 0;
    for (unsigned i = 0; i < 4; i++)
        if (pipe_receive(&t, read, result, sizeof(result), 3, &n) || n != sizeof(valid) || !bytes_equal(result, valid, n)) return 0;
    if (pipe_receive(&t, read, result, sizeof(result), 3, &n) != SYS_EAGAIN) return 0;
    handle_table_clear(&t); return 1;
}
