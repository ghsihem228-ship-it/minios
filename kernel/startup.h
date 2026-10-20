#ifndef STARTUP_H
#define STARTUP_H
#include <stdint.h>
#define TASK_ARGC_MAX 8u
#define TASK_ARG_LEN 64u /* includes NUL */
#define TASK_START_BYTES 1024u
struct startup_stack {
    uint32_t sp, used;
    uint8_t bytes[TASK_START_BYTES];
};
/* Pure bounded initial-stack packing, independent of page tables. */
int startup_build(unsigned argc, const char *const *argv, uint32_t top, uint32_t bottom,
                  struct startup_stack *out);
#endif
