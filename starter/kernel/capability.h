#ifndef STARTER_CAPABILITY_H
#define STARTER_CAPABILITY_H
#include <stdint.h>
typedef uint64_t cap_rights_t;
#define CAP_READ      (UINT64_C(1) << 0)
#define CAP_WRITE     (UINT64_C(1) << 1)
#define CAP_WAIT      (UINT64_C(1) << 2)
#define CAP_CONTROL   (UINT64_C(1) << 3)
#define CAP_DUPLICATE (UINT64_C(1) << 4)
#define CAP_TRANSFER  (UINT64_C(1) << 5)
#define CAP_ALL (CAP_READ | CAP_WRITE | CAP_WAIT | CAP_CONTROL | CAP_DUPLICATE | CAP_TRANSFER)
#endif
