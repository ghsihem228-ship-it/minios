#ifndef STARTER_PIPE_H
#define STARTER_PIPE_H
#include "handle.h"
#include "syscall_abi.h"
#define PIPE_PAYLOAD 128u
#define PIPE_FRAME (8u + PIPE_PAYLOAD)
int wire_validate(const void *, size_t, uint16_t *type);
int64_t pipe_create(struct handle_table *, handle_t *read, handle_t *write);
int64_t pipe_send(struct handle_table *, handle_t, const void *, size_t);
int64_t pipe_receive(struct handle_table *, handle_t, void *, size_t, uint16_t expected, size_t *);
int64_t pipe_signals(struct object *, uint64_t requested, uint64_t *observed);
int pipe_selftest(void);
int pipe_task_selftest(void);
#endif
