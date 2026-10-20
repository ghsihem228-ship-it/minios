#ifndef STARTER_SYSCALL_ABI_H
#define STARTER_SYSCALL_ABI_H
#include <stdint.h>
/* v1 scalar ABI. INT 0x80 remains DPL0 until a safe ring-3 entry exists. */
#define SYS_ABI_MAJOR UINT64_C(1)
#define SYS_ABI_VERSION (SYS_ABI_MAJOR << 32)
#define SYS_ABI_QUERY UINT64_C(0)
#define SYS_HANDLE_CLOSE UINT64_C(1)
#define SYS_HANDLE_DUPLICATE UINT64_C(2)
#define SYS_OK INT64_C(0)
#define SYS_ENOSYS (-INT64_C(1))
#define SYS_EINVAL (-INT64_C(2))
#define SYS_EBADHANDLE (-INT64_C(3))
#define SYS_ENOSPC (-INT64_C(4))
#define SYS_EOVERFLOW (-INT64_C(5))
#define SYS_EABI (-INT64_C(6))
struct syscall_result { int64_t status; uint64_t value; };
/* SysV wrapper; arguments become RDI,RSI,RDX,R10,R8,R9; RAX=number.
 * Return RAX=status, RDX=value; all other interrupted GPRs preserved. */
struct syscall_result syscall_invoke(uint64_t number, uint64_t a0, uint64_t a1,
                                    uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
#endif
