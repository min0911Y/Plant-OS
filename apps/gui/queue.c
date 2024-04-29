#include "gui.h"
#include <stdlib.h>

void abort(void);
queue_t *queue_init() {
  queue_t *q = (queue_t *)malloc(sizeof(queue_t));
  q->phead = NULL;
  q->ctl = list_new();
  return q;
}

void queue_push(queue_t *q, unsigned value) {
  List *l = list_add_val(value,q->ctl);
  if(q->phead == NULL) {
    q->phead = l;
  }
}
unsigned queue_pop(queue_t *q) {
  if(!q->phead) return -1;
  List *next;
  next = q->phead->next;
  unsigned dat = q->phead->val;
  list_delete_child(q->phead,q->ctl);
  q->phead = next;
  return dat;
}
void queue_free(queue_t *q) {
  list_delete(q->ctl);
  free(q);
}