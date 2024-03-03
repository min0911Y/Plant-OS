#ifndef MST_H
#define MST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
struct ListCtl {
  struct List* start;
  struct List* end;
  size_t all;
};
struct List {
  struct ListCtl* ctl;
  struct List* prev;
  uintptr_t val;
  struct List* next;
};
typedef struct List List;

// 链表
List* list_add_val(uintptr_t val, struct List* Obj);
struct List* list_search_by_count(size_t count, struct List* Obj);
void list_delete_by_count(size_t count, struct List* Obj);
struct List* list_new(void);
void list_change_child_by_count(size_t count, struct List* Obj, uintptr_t val);
size_t list_get_last_count(struct List* Obj);
void list_delete(struct List* Obj);
void list_delete_child(struct List* need_to_free, struct List* Obj);

#ifdef __cplusplus
}
#endif
#endif
