#include "startup.h"
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (8 * i));
}
int startup_build(unsigned argc, const char *const *argv, uint32_t top, uint32_t bottom,
                  struct startup_stack *out) {
    if (!out || !argv || !argc || argc > TASK_ARGC_MAX || top <= bottom || (top & 15))
        return -22;
    unsigned length[TASK_ARGC_MAX], strings = 0;
    for (unsigned i = 0; i < argc; i++) {
        if (!argv[i])
            return -22;
        unsigned n = 0;
        while (n < TASK_ARG_LEN && argv[i][n])
            n++;
        if (n == TASK_ARG_LEN)
            return -7;
        if (!i && !n)
            return -22;
        strings += (length[i] = n + 1);
    }
    unsigned words = (argc + 3) * 4, used = (strings + words + 15) & ~15u;
    if (used > sizeof(out->bytes) || used > top - bottom)
        return -7;
    out->used = used;
    out->sp = top - used;
    for (unsigned i = 0; i < used; i++)
        out->bytes[i] = 0;
    put32(out->bytes, argc);
    unsigned at = used - strings;
    for (unsigned i = 0; i < argc; i++) {
        put32(out->bytes + 4 + 4 * i, out->sp + at);
        for (unsigned j = 0; j < length[i]; j++)
            out->bytes[at++] = argv[i][j];
    }
    /* argv[argc] and the empty envp vector are already zero. */
    return 0;
}
