#ifndef PLANT_SCHED_H
#define PLANT_SCHED_H
struct sched_param {
  int sched_priority;
};
#ifdef __cplusplus
extern "C" {
#endif
int sched_yield(void);
#ifdef __cplusplus
}
#endif
#endif
