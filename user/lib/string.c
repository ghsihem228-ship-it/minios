#include "minios.h"
size_t strlen(const char *p) {
    size_t n = 0;
    while (p[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!a[i] || a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
    return 0;
}
void *memcpy(void *out, const void *in, size_t n) {
    uint8_t *a = out;
    const uint8_t *b = in;
    for (size_t i = 0; i < n; i++)
        a[i] = b[i];
    return out;
}
void *memmove(void *out, const void *in, size_t n) {
    uint8_t *a = out;
    const uint8_t *b = in;
    if ((uintptr_t)a < (uintptr_t)b)
        for (size_t i = 0; i < n; i++)
            a[i] = b[i];
    else
        for (size_t i = n; i; i--)
            a[i - 1] = b[i - 1];
    return out;
}
void *memset(void *out, int byte, size_t n) {
    uint8_t *p = out;
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)byte;
    return out;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}
int parse_u32(const char *p, uint32_t *out) {
    if (!p || !*p || !out)
        return 0;
    uint32_t n = 0;
    while (*p) {
        if (*p < '0' || *p > '9')
            return 0;
        unsigned d = *p++ - '0';
        if (n > (UINT32_MAX - d) / 10)
            return 0;
        n = n * 10 + d;
    }
    *out = n;
    return 1;
}
int parse_ipv4(const char *p, uint32_t *out) {
    if (!p || !out)
        return 0;
    uint32_t ip = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned n = 0, d = 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p++ - '0');
            if (++d > 3 || n > 255)
                return 0;
        }
        if (!d || (i < 3 && *p++ != '.') || (i == 3 && *p))
            return 0;
        ip = (ip << 8) | n;
    }
    *out = ip;
    return 1;
}
