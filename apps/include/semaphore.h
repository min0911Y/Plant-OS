#ifndef PLANT_SEMAPHORE_H
#define PLANT_SEMAPHORE_H

#include <stdint.h>
#include <time.h>

typedef struct {
  uint32_t value;
} sem_t;

#define SEM_VALUE_MAX 0x7fffffff

#ifdef __cplusplus
extern "C" {
#endif
int sem_init(sem_t *semaphore, int shared, unsigned value);
int sem_destroy(sem_t *semaphore);
int sem_post(sem_t *semaphore);
int sem_wait(sem_t *semaphore);
int sem_trywait(sem_t *semaphore);
int sem_timedwait(sem_t *semaphore, const struct timespec *deadline);
int sem_getvalue(sem_t *semaphore, int *value);
#ifdef __cplusplus
}
#endif

#endif
