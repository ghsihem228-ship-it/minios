#include "programs.h"
#include "vfs.h"
#define DECLARE(name) extern const unsigned char program_##name##_start[], program_##name##_end[]
DECLARE(hello); DECLARE(probe); DECLARE(filedemo); DECLARE(sleep);
DECLARE(spin); DECLARE(crash); DECLARE(readonly);
DECLARE(isolate); DECLARE(guard); DECLARE(io);
DECLARE(http);
DECLARE(ctest);
DECLARE(args);
DECLARE(udpecho);
DECLARE(sockprobe);
DECLARE(sockhold);
DECLARE(diskdemo);
#define ADD(name) if (fs_add("/bin/" #name, program_##name##_start, \
    (uint32_t)program_##name##_end - (uint32_t)program_##name##_start)) return 0
int programs_install(void) {
    ADD(hello); ADD(probe); ADD(filedemo); ADD(sleep);
    ADD(spin); ADD(crash); ADD(readonly);
    ADD(isolate); ADD(guard); ADD(io);
    ADD(http);
    ADD(ctest);
    ADD(args);
    ADD(udpecho);
    ADD(sockprobe);
    ADD(sockhold);
    ADD(diskdemo);
    return 1;
}
