#ifndef MST_H
#define MST_H
#ifdef __cplusplus
extern "C" {
#endif
#define PRIVATE static
#define PUBLIC
#define MST_API
#include <stddef.h>
#include <stdint.h>
#define MEM_LEAK_CHECK 0
#if MEM_LEAK_CHECK
#include "leakcheck.h"
#endif
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
typedef enum {
  TK_NUMBER, // 数字标识符
  TK_STRING, // 字符串标识符
  TK_SPACE_START, // SPACE变量的开始
  TK_SPACE_END, // SPACE变量的结束
  TK_ARRAY_START, // 数组的开始
  TK_ARRAY_END, // 数组的结束
  TK_OP, // 符号标识符
  TK_WORD // 变量的名字
} TOKEN_TYPE;
typedef struct {
  char* tok;
  TOKEN_TYPE t;
} TOKEN;
typedef enum {
  STRING_OP_ERROR = 1,
  UNKNOW_TOKEN,
  ERROR_TOKEN,
  SYNTAX_ERROR,
  WRONG_TYPE_TO_ADD
} ERR_TYPE;
typedef enum { VAR_INTEGER, VAR_ARRAY, VAR_SPACE, VAR_STRING } VAR_TYPE;
typedef struct {
  List* the_space;
} SPACE;
typedef struct {
  char* string;
  unsigned len;
  List* token;
  SPACE* root_space;
  int err;
} MST_Object;
typedef struct {
  uint32_t num;
} Integer;
typedef struct {
  char* str;
} String;
typedef struct {
  List* the_array;
} Array;
typedef struct {
  VAR_TYPE vt;
  char* name;
  void* obj;
  List* this_list;
} Var;
typedef struct {
  VAR_TYPE vt;
  void* obj;
  List* this_list;
} Array_data;
/*
    mini set 解析路线
    Space->var->Array->string/integer
    Space->var->Space……
    Space->var->string/integer
    Space中放Var，Var中可以放Space、Array、integer、string
    Parser会设置一个叫做`root_space`的空间以存放Var
 */

// 链表
List* list_add_val(uintptr_t val, struct List* Obj);
struct List* list_search_by_count(size_t count, struct List* Obj);
void list_delete_by_count(size_t count, struct List* Obj);
struct List* list_new(void);
void list_change_child_by_count(size_t count, struct List* Obj, uintptr_t val);
size_t list_get_last_count(struct List* Obj);
void list_delete(struct List* Obj);
void list_delete_child(struct List* need_to_free, struct List* Obj);

PUBLIC void free_space(SPACE* space);
PUBLIC void free_arr(Array* arr);
// API
PUBLIC MST_API MST_Object* MST_init(char* string);
PUBLIC MST_API Var* MST_get_var(char* name, SPACE* space);
PUBLIC MST_API int MST_space_get_integer(Var* var);
PUBLIC MST_API SPACE* MST_space_get_space(Var* var);
PUBLIC MST_API Array* MST_space_get_array(Var* var);
PUBLIC MST_API char* MST_space_get_str(Var* var);
PUBLIC MST_API Array_data* MST_array_get_data(Array* arr, int idx);
PUBLIC MST_API int MST_array_get_integer(Array_data* ad);
PUBLIC MST_API char* MST_array_get_str(Array_data* ad);
PUBLIC MST_API SPACE* MST_array_get_space(Array_data* ad);
PUBLIC MST_API Array* MST_array_get_array(Array_data* ad);
PUBLIC MST_API void MST_free(MST_Object* mst);

PUBLIC MST_API const char* MST_strerror(MST_Object* mst);
PUBLIC MST_API void MST_add_data_to_array(MST_Object* mst_obj,
                                          Array* arr,
                                          Array_data ad);
PUBLIC MST_API char* MST_build_to_string(MST_Object* mst_obj);
PUBLIC MST_API void MST_add_var_to_space(MST_Object* mst_obj,
                                         SPACE* sp,
                                         Var var);
PUBLIC MST_API Var MST_var_make_integer(char* name, int val);
PUBLIC MST_API Var MST_var_make_string(char* name, char* ss);
PUBLIC MST_API Array_data MST_arr_dat_make_integer(int val);
PUBLIC MST_API Array_data MST_arr_dat_make_string(char* ss);
PUBLIC MST_API void MST_add_empty_array_to_array(Array* arr);
PUBLIC MST_API void MST_add_empty_space_to_array(Array* arr);
PUBLIC MST_API void MST_add_empty_array_to_space(MST_Object* mst_obj,
                                                 SPACE* sp,
                                                 char* name);
PUBLIC MST_API void MST_add_empty_space_to_space(MST_Object* mst_obj,
                                                 SPACE* sp,
                                                 char* name);
PUBLIC MST_API void MST_change_var(MST_Object* mst_obj, Var v, Var* v1);
PUBLIC MST_API void MST_change_arr(MST_Object* mst_obj,
                                   Array_data v,
                                   Array_data* v1);
PUBLIC MST_API void MST_remove_var(Var* v);
PUBLIC MST_API void MST_remove_arr(Array_data* v);
#define MST_get_root_space(m) (m)->root_space
// MST_get_space_in_space
#define MST_get_space_in_space(m, name, space)          \
  MST_get_var((name), (space))                           \
      ? MST_space_get_space(MST_get_var((name), (space))) \
      : NULL
#define MST_get_space_in_array(m, idx, arr)              \
  MST_array_get_data((arr), (idx))                            \
      ? MST_array_get_space(MST_array_get_data((arr), (idx))) \
      : NULL  // MST_get_space_in_array
#define MST_get_integer_in_space(m, name, space)          \
  MST_get_var((name), (space))                             \
      ? MST_space_get_integer(MST_get_var((name), (space))) \
      : -1  // MST_get_integer_in_space
#define MST_get_integer_in_array(m, idx, arr)              \
  MST_array_get_data((arr), (idx))                              \
      ? MST_array_get_integer(MST_array_get_data((arr), (idx))) \
      : -1  // MST_get_integer_in_array
#define MST_get_string_in_space(m, name, space)                               \
  MST_get_var((name), (space)) ? MST_space_get_str(MST_get_var((name), (space))) \
                              : NULL  // MST_get_string_in_space
#define MST_get_string_in_array(m, idx, arr)                                   \
  MST_array_get_data((arr), (idx)) ? MST_array_get_str(MST_array_get_data((arr), (idx))) \
                              : NULL  // MST_get_string_in_array
#define MST_change_var_for_name(m, v, name, space)           \
  {                                                          \
    if (MST_get_var((name), (space))) {                       \
      MST_change_var((m), (v), MST_get_var((name), (space))); \
    }                                                        \
  }
#define MST_change_arr_for_idx(m, v, idx, arr)               \
  {                                                          \
    if (MST_Array_Get((arr), (idx))) {                       \
      MST_change_arr((m), (v), MST_Array_Get((arr), (idx))); \
    }                                                        \
  }
#ifdef __cplusplus
}
#endif
#endif
