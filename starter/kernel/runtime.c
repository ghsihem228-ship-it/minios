#include <stddef.h>
/* Freestanding compiler-emitted structure-copy helpers. No overlap is allowed. */
void *memcpy(void *destination, const void *source, size_t n) {
    unsigned char *d = destination; const unsigned char *s = source;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return destination;
}
void *memset(void *destination, int value, size_t n) {
    unsigned char *d = destination;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)value;
    return destination;
}
