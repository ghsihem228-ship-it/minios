#ifndef STARTER_BYTES_H
#define STARTER_BYTES_H
#include <stddef.h>
#include <stdint.h>
static inline void bytes_copy(void *to, const void *from, size_t n) {
    unsigned char *d = to; const unsigned char *s = from;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}
static inline void bytes_zero(void *to, size_t n) {
    unsigned char *d = to; for (size_t i = 0; i < n; i++) d[i] = 0;
}
static inline int bytes_equal(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}
#endif
