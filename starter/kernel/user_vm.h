#ifndef STARTER_USER_VM_H
#define STARTER_USER_VM_H
#include "vmm.h"
#define USER_BASE UINT64_C(0x40000000)
#define USER_PAGES 16u
#define USER_END (USER_BASE + USER_PAGES * PAGE_SIZE)
#define PTE_USER UINT64_C(4)
int vm_user_map(unsigned index, uint64_t flags);
void vm_user_clear(void);
int vm_user_read(void *, uint64_t address, size_t);
int vm_user_write(uint64_t address, const void *, size_t);
int vm_user_check(uint64_t address, size_t, int write);
int vm_user_prepare(uint64_t address, size_t);
void *vm_user_physical(unsigned index);
int vm_snapshot_begin(void);
void vm_snapshot_end(int rollback);
int vm_user_cow(uint64_t address);
#endif
