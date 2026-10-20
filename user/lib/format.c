/* Small integer-only formatter. No heap, FPU, FILE objects or host libc. */
#include "minios.h"
struct output {
    char *data;
    size_t cap, used;
    unsigned console, fill;
    int error;
    char buffer[128];
};
static void flush(struct output *o) {
    if (!o->fill || o->error)
        return;
    int n = sys_console(o->buffer, o->fill);
    if (n != (int)o->fill)
        o->error = n < 0 ? n : -5;
    o->fill = 0;
}
static void emit(struct output *o, char c) {
    if (o->error)
        return;
    if (o->used == 0x7fffffffu) {
        o->error = -7;
        return;
    }
    if (o->console) {
        o->buffer[o->fill++] = c;
        if (o->fill == sizeof(o->buffer))
            flush(o);
    } else if (o->cap && o->used < o->cap - 1)
        o->data[o->used] = c;
    o->used++;
}
static void integer(struct output *o, uint32_t value, unsigned base, int negative, unsigned width,
                    int zero, int upper) {
    char digits[32];
    unsigned n = 0;
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        digits[n++] = alphabet[value % base];
        value /= base;
    } while (value);
    unsigned size = n + (negative != 0);
    if (negative && zero)
        emit(o, '-');
    while (width > size) {
        emit(o, zero ? '0' : ' ');
        width--;
    }
    if (negative && !zero)
        emit(o, '-');
    while (n)
        emit(o, digits[--n]);
}
static int format(struct output *o, const char *p, va_list args) {
    if (!p || (!o->console && o->cap && !o->data))
        return -22;
    while (*p && !o->error) {
        if (*p != '%') {
            emit(o, *p++);
            continue;
        }
        p++;
        int zero = 0;
        if (*p == '0') {
            zero = 1;
            p++;
        }
        unsigned width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p++ - '0');
            if (width > 32) {
                o->error = -22;
                break;
            }
        }
        if (o->error)
            break;
        char c = *p;
        if (!c) {
            o->error = -22;
            break;
        }
        p++;
        if (c == '%')
            emit(o, '%');
        else if (c == 'c')
            emit(o, (char)va_arg(args, int));
        else if (c == 's') {
            const char *s = va_arg(args, const char *);
            if (!s)
                s = "(null)";
            size_t n = strlen(s);
            while (width > n) {
                emit(o, ' ');
                width--;
            }
            while (*s && !o->error)
                emit(o, *s++);
        } else if (c == 'd' || c == 'i') {
            int32_t n = va_arg(args, int);
            integer(o, n < 0 ? 0u - (uint32_t)n : (uint32_t)n, 10, n < 0, width, zero, 0);
        } else if (c == 'u' || c == 'x' || c == 'X')
            integer(o, va_arg(args, unsigned), c == 'u' ? 10 : 16, 0, width, zero, c == 'X');
        else if (c == 'p') {
            emit(o, '0');
            emit(o, 'x');
            integer(o, (uintptr_t)va_arg(args, void *), 16, 0, 8, 1, 0);
        } else
            o->error = -22;
    }
    if (o->console)
        flush(o);
    else if (o->cap)
        o->data[o->used < o->cap ? o->used : o->cap - 1] = 0;
    return o->error ? o->error : (int)o->used;
}
int vsnprintf(char *out, size_t cap, const char *fmt, va_list args) {
    struct output o = {.data = out, .cap = cap};
    return format(&o, fmt, args);
}
int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(out, cap, fmt, args);
    va_end(args);
    return n;
}
int printf(const char *fmt, ...) {
    struct output o = {.console = 1};
    va_list args;
    va_start(args, fmt);
    int n = format(&o, fmt, args);
    va_end(args);
    return n;
}
