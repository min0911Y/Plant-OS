#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

struct mtask;
/* Update the runnable load atomically with a task's scheduling weight. */
void task_set_weight(struct mtask *task, unsigned weight);

#endif
