#include "minios.h"
int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--delay")) {
        uint32_t ticks;
        if (!parse_u32(argv[2], &ticks) || ticks > 500) {
            printf("Invalid delay (0..500 ticks).\n");
            return 2;
        }
        sys_sleep(ticks);
    }
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("argv[%d]=<%s>\n", i, argv[i]);
    if (argv[argc])
        return 1;
    return 0;
}
