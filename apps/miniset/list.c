// 链表
#include "mst.h"
#define page_kmalloc malloc
#define page_kfree(p, sz) free((p))
List *list_add_val(uintptr_t val, struct List* Obj) {
  while (Obj->next != NULL)
    Obj = Obj->next;
  Obj = Obj->ctl->end;
  struct List* new = (struct List*)page_kmalloc(sizeof(struct List));
  Obj->next = new;
  Obj->ctl->end = new;
  new->prev = Obj;
  new->ctl = Obj->ctl;
  new->next = (List*)NULL;
  new->val = val;
  new->ctl->all++;
  return new;
}
struct List* list_search_by_count(size_t count, struct List* Obj) {
  int count_last = list_get_last_count(Obj);
  struct List *p = Obj, *q = Obj->ctl->end;
  if (count > count_last)
    return (List*)NULL;
  for (int i = 0, j = count_last;; i++, j--) {
    if (i == count) {
      return p;
    } else if (j == count) {
      return q;
    }
    p = p->next;
    q = q->prev;
  }
}
void list_delete_by_count(size_t count, struct List* Obj) {
  struct List* Will_Free = list_search_by_count(count, Obj);
  if (Will_Free == NULL) {
    // Not found!
    return;
  }
  if (count == 0) {
    return;
  }
  if (Will_Free->next == (List*)NULL) {
    // 是尾节点
    struct List* prev = Will_Free->prev;
    prev->next = (List*)NULL;
    prev->ctl->end = prev;
  } else {
    struct List* prev = Will_Free->prev;
    struct List* next = Will_Free->next;
    prev->next = next;
    next->prev = prev;
  }
  page_kfree((void *)Will_Free, sizeof(struct List));
  Obj->ctl->all--;
}
void list_delete_child(struct List* need_to_free, struct List* Obj) {
  if (need_to_free == NULL) {
    // Not found!
    return;
  }
  if (need_to_free->prev == NULL) {
    return;
  }
  if (need_to_free->next == (List*)NULL) {
    // 是尾节点
    struct List* prev = need_to_free->prev;
    prev->next = (List*)NULL;
    prev->ctl->end = prev;
  }
  else {
    struct List* prev = need_to_free->prev;
    struct List* next = need_to_free->next;
    prev->next = next;
    next->prev = prev;
  }
  page_kfree((void *)need_to_free, sizeof(struct List));
  Obj->ctl->all--;
}
struct List* list_new() {
  struct List* Obj = (struct List*)page_kmalloc(sizeof(struct List));
  struct ListCtl* ctl = (struct ListCtl*)page_kmalloc(sizeof(struct ListCtl));
  Obj->ctl = ctl;
  Obj->ctl->start = Obj;
  Obj->ctl->end = Obj;
  Obj->val = 0x123456;  // 头结点数据不可用
  Obj->prev = (List*)NULL;
  Obj->next = (List*)NULL;
  Obj->ctl->all = 0;
  return Obj;
}
void list_change_child_by_count(size_t count, struct List* Obj, uintptr_t val) {
  struct List* Will_Change = list_search_by_count(count + 1, Obj);
  if (Will_Change != NULL) {
    Will_Change->val = val;
  } else {
    list_add_val(val, Obj);
  }
}
// 获取尾节点的count
size_t list_get_last_count(struct List* Obj) {
  if(!Obj) return -1; // error
  return Obj->ctl->all;
}
void list_delete(struct List* Obj) {
  Obj = Obj->ctl->start;
  page_kfree((void *)Obj->ctl, sizeof(struct ListCtl));
  for (; Obj != (struct List*)NULL;) {
    //printf("Will free: %llx\n", Obj);
    struct List* tmp = Obj;
    Obj = Obj->next;
    page_kfree((void *)tmp, sizeof(struct List));
  }
  return;
}
