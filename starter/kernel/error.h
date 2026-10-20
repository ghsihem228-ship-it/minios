#ifndef STARTER_ERROR_H
#define STARTER_ERROR_H
#include "syscall_abi.h"
#define ERROR_DEPTH 4u
struct error_cause { uint32_t domain, code; uint64_t detail; };
struct error_chain { uint32_t version, count; struct error_cause causes[ERROR_DEPTH]; };
_Static_assert(sizeof(struct error_cause) == 16, "cause layout");
_Static_assert(sizeof(struct error_chain) == 72, "bounded cause chain");
/* Domain 1 operation, 2 status, 3 missing authority. No pointers or secret paths. */
void error_record(struct error_chain *, int64_t status, uint64_t operation, uint64_t required);
struct syscall_result error_field(const struct error_chain *, uint64_t index, uint64_t field);
#endif
