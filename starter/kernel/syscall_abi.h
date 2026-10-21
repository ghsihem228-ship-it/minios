#ifndef STARTER_SYSCALL_ABI_H
#define STARTER_SYSCALL_ABI_H
#include <stdint.h>
#include "capability.h"
/* INT80 ABI; user gate enabled only during a controlled CPL3 dispatch. */
#define SYS_ABI_MAJOR UINT64_C(1)
#define SYS_ABI_VERSION ((SYS_ABI_MAJOR << 32) | UINT64_C(4))
#define SYS_ABI_QUERY UINT64_C(0)
#define SYS_HANDLE_CLOSE UINT64_C(1)
#define SYS_HANDLE_DUPLICATE UINT64_C(2)
#define SYS_HANDLE_RESTRICT UINT64_C(3)
#define SYS_HANDLE_CLONE UINT64_C(4)
#define SYS_EACCESS (-INT64_C(7))
#define SYS_ERROR_GET UINT64_C(5)
#define SYS_EXIT UINT64_C(6)
#define SYS_FS_OPEN UINT64_C(7)
#define SYS_FS_READ UINT64_C(8)
#define SYS_FS_WRITE UINT64_C(9)
#define SYS_FS_SEEK UINT64_C(10)
#define SYS_FS_UNLINK UINT64_C(11)
#define SYS_TX_BEGIN UINT64_C(12)
#define SYS_TX_COMMIT UINT64_C(13)
#define SYS_TX_ROLLBACK UINT64_C(14)
#define SYS_PIPE_CREATE UINT64_C(15)
#define SYS_PIPE_SEND UINT64_C(16)
#define SYS_PIPE_RECEIVE UINT64_C(17)
#define SYS_PROCESS_SPAWN UINT64_C(18)
#define SYS_PROCESS_STATUS UINT64_C(19)
#define SYS_PROCESS_KILL UINT64_C(20)
#define SYS_OBJECT_WAIT UINT64_C(21)
#define SYS_YIELD UINT64_C(22)
#define SYS_SLEEP UINT64_C(23)
#define SYS_WAIT_MANY UINT64_C(24)
#define SYS_CALL_COUNT 25u
#define WAIT_ITEMS 8u
struct wait_item { uint64_t handle, signals; };
#define SIGNAL_EXITED UINT64_C(1)
#define SIGNAL_READABLE UINT64_C(2)
#define SIGNAL_WRITABLE UINT64_C(4)
#define SIGNAL_PEER_CLOSED UINT64_C(8)
#define LAUNCH_GRANTS 8u
#define LAUNCH_ELF_BYTES 16384u
struct launch_grant { uint64_t source, rights; };
#define SYS_EPIPE (-INT64_C(16))
#define SYS_ETIMEDOUT (-INT64_C(17))
#define SYS_ENOENT (-INT64_C(8))
#define SYS_EBUSY (-INT64_C(9))
#define SYS_EFAULT (-INT64_C(10))
#define SYS_EAGAIN (-INT64_C(11))
#define SYS_EFORMAT (-INT64_C(12))
#define SYS_ECONFLICT (-INT64_C(13))
#define SYS_ENOMEM (-INT64_C(14))
#define SYS_EIO (-INT64_C(15))
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
