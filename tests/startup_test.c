#include "command.h"
#include "startup.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t seed = 123;
static uint32_t random32(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void inspect(unsigned argc, const char *const *argv, const struct startup_stack *out) {
    assert(!(out->sp & 15) && out->sp + out->used == 0x40400000 && out->used <= TASK_START_BYTES);
    assert(get32(out->bytes) == argc);
    for (unsigned i = 0; i < argc; i++) {
        uint32_t ptr = get32(out->bytes + 4 + 4 * i);
        assert(ptr >= out->sp + (argc + 3) * 4 && ptr < 0x40400000);
        unsigned off = ptr - out->sp;
        const void *end = memchr(out->bytes + off, 0, out->used - off);
        assert(end);
        assert(!strcmp((const char *)out->bytes + off, argv[i]));
    }
    assert(!get32(out->bytes + 4 + argc * 4) && !get32(out->bytes + 8 + argc * 4));
}
int main(void) {
    char line[256] = "run /bin/args \"a b\" '' x\\ y 'a\"b' \"c\\\"d\"";
    char *args[9];
    const char *expected[] = {"run", "/bin/args", "a b", "", "x y", "a\"b", "c\"d"};
    assert(command_split(line, sizeof(line), args, 9) == 7);
    for (unsigned i = 0; i < 7; i++)
        assert(!strcmp(args[i], expected[i]));
    char unclosed[] = "run /bin/args 'oops", escape[] = "run /bin/args x\\", limit[] = "a b c";
    assert(command_split(unclosed, sizeof(unclosed), args, 9) == -22);
    assert(command_split(escape, sizeof(escape), args, 9) == -22);
    assert(command_split(limit, sizeof(limit), args, 2) == -7);
    char full[4] = {'a', 'b', 'c', 'd'};
    assert(command_split(full, sizeof(full), args, 9) == -7);
    struct startup_stack out;
    const char *argv[] = {"/bin/args", "hello world", ""};
    assert(!startup_build(3, argv, 0x40400000, 0x403fc000, &out));
    inspect(3, argv, &out);
    assert(startup_build(0, argv, 0x40400000, 0x403fc000, &out) == -22);
    assert(startup_build(9, argv, 0x40400000, 0x403fc000, &out) == -22);
    assert(startup_build(3, argv, 0x40400000, 0x403ffff0, &out) == -7);
    const char *bad[] = {""};
    assert(startup_build(1, bad, 0x40400000, 0x403fc000, &out) == -22);
    for (unsigned trial = 0; trial < 100000; trial++) {
        unsigned len = random32() % sizeof(line);
        for (unsigned i = 0; i < len; i++)
            line[i] = (char)(1 + random32() % 126);
        line[len] = 0;
        int count = command_split(line, sizeof(line), args, 9);
        if (count >= 0)
            for (int i = 0; i < count; i++) {
                assert(args[i] >= line && args[i] < line + sizeof(line));
                assert(memchr(args[i], 0, (size_t)(line + sizeof(line) - args[i])));
            }
        char text[8][65];
        const char *values[8];
        unsigned n = 1 + random32() % 8;
        for (unsigned i = 0; i < n; i++) {
            unsigned size = random32() % 65;
            for (unsigned j = 0; j < size; j++)
                text[i][j] = (char)('a' + random32() % 26);
            text[i][size] = 0;
            values[i] = text[i];
        }
        int error = startup_build(n, values, 0x40400000, 0x403fc000, &out);
        if (!error)
            inspect(n, values, &out);
        else
            assert(error == -22 || error == -7);
    }
    puts("PASS startup: quotes/escapes, argument bounds, aligned private stack and 100000 "
         "sanitizer cases");
}
