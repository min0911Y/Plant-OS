#ifndef LIBP_TIME_INTERNAL_H
#define LIBP_TIME_INTERNAL_H

#include <stdint.h>
#include <time.h>

int runtime_deadline(clockid_t clock, const struct timespec *time,
                     uint64_t *deadline);

#endif
