#include "error.h"
void error_record(struct error_chain *e, int64_t status, uint64_t operation, uint64_t required) {
    *e = (struct error_chain){ .version = 1 };
    if (!status) return;
    e->causes[e->count++] = (struct error_cause){1, 1, operation};
    e->causes[e->count++] = (struct error_cause){2, (uint32_t)(UINT64_C(0) - (uint64_t)status), 0};
    if (status == SYS_EACCESS)
        e->causes[e->count++] = (struct error_cause){3, 1, required};
}
struct syscall_result error_field(const struct error_chain *e, uint64_t index, uint64_t field) {
    struct syscall_result r = { SYS_OK, 0 };
    if (field == 3 && index == 0) { r.value = e->count; return r; }
    if (index >= e->count || field > 2) { r.status = SYS_EINVAL; return r; }
    r.value = field == 0 ? e->causes[index].domain :
              field == 1 ? e->causes[index].code : e->causes[index].detail;
    return r;
}
