#include <stdio.h>
#include <string.h>
#include "mst.h"
#include "mstr.h"
#include <assert.h>
PRIVATE void build_space(mstr* result, SPACE* space, int spaces_no);
PRIVATE void put_token(char* buf, TOKEN_TYPE t, MST_Object* mst) {
  TOKEN* tok = malloc(sizeof(TOKEN));
  assert(tok);
  tok->t = t;
  tok->tok = buf;
  list_add_val((uintptr_t) tok, mst->token);
}
PUBLIC MST_API Var MST_var_make_integer(char* name, int val) {
  Var r;
  char* s = (char*)malloc(strlen(name) + 1);
  assert(s);
  strcpy(s, name);
  r.name = s;
  r.vt = VAR_INTEGER;
  r.obj = malloc(sizeof(Integer));
  ((Integer*)(r.obj))->num = val;
  return r;
}
PUBLIC MST_API Var MST_var_make_string(char* name, char* ss) {
  Var r;
  char* s = (char*)malloc(strlen(name) + 1);
  assert(s);
  strcpy(s, name);
  r.name = s;
  r.vt = VAR_STRING;
  r.obj = malloc(sizeof(String));
  char* s1 = (char*)malloc(strlen(ss) + 1);
  assert(s1);
  strcpy(s1, ss);
  ((String*)(r.obj))->str = s1;
  return r;
}

PUBLIC MST_API Array_data MST_arr_dat_make_integer(int val) {
  Array_data r;
  r.vt = VAR_INTEGER;
  r.obj = malloc(sizeof(Integer));
  ((Integer*)(r.obj))->num = val;
  return r;
}
PUBLIC MST_API Array_data MST_arr_dat_make_string(char* ss) {
  Array_data r;
  r.vt = VAR_STRING;
  r.obj = malloc(sizeof(String));
  assert(r.obj);
  char* s1 = (char*)malloc(strlen(ss) + 1);
  assert(s1);
  strcpy(s1, ss);
  ((String*)(r.obj))->str = s1;
  return r;
}

PUBLIC MST_API void MST_add_data_to_array(MST_Object* mst_obj,
                                          Array* arr,
                                          Array_data ad) {
  if (ad.vt == VAR_SPACE || ad.vt == VAR_ARRAY) {
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  Array_data* v = (Array_data*)malloc(sizeof(Array_data));
  assert(v);
  memcpy(v, &ad, sizeof(Array_data));
  if (v->vt == VAR_STRING) {
    String* s = (String*)v->obj;
    put_token(s->str, TK_STRING, mst_obj);
  }
  v->this_list = list_add_val((uintptr_t) v, arr->the_array);
}
PUBLIC MST_API void MST_add_var_to_space(MST_Object* mst_obj,
                                         SPACE* sp,
                                         Var var) {
  if (var.vt == VAR_SPACE || var.vt == VAR_ARRAY) {
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  put_token(var.name, TK_WORD, mst_obj);
  Var* v = (Var*)malloc(sizeof(Var));
  assert(v);
  memcpy(v, &var, sizeof(Var));
  if (v->vt == VAR_STRING) {
    String* s = (String*)v->obj;
    put_token(s->str, TK_STRING, mst_obj);
  }
  v->this_list = list_add_val((uintptr_t) v, sp->the_space);
}
PUBLIC MST_API void MST_add_empty_space_to_space(MST_Object* mst_obj,SPACE* sp,char* name) {
  char* s = (char*)malloc(strlen(name) + 1);
  assert(s);
  strcpy(s, name);
  Var* v = (Var*)malloc(sizeof(Var));
  assert(v);
  v->name = s;
  v->vt = VAR_SPACE;
  v->obj = malloc(sizeof(SPACE));
  assert(v->obj);
  ((SPACE*)v->obj)->the_space = list_new();
  put_token(s, TK_WORD, mst_obj);
  v->this_list = list_add_val((uintptr_t) v, sp->the_space);
}
PUBLIC MST_API void MST_add_empty_array_to_space(MST_Object* mst_obj,SPACE* sp,char* name) {
  char* s = (char*)malloc(strlen(name) + 1);
  assert(s);
  strcpy(s, name);
  Var* v = (Var*)malloc(sizeof(Var));
  assert(v);
  v->name = s;
  v->vt = VAR_ARRAY;
  v->obj = malloc(sizeof(Array));
  assert(v->obj);
  ((Array*)v->obj)->the_array = list_new();
  put_token(s, TK_WORD, mst_obj);
  v->this_list = list_add_val((uintptr_t) v, sp->the_space);
}
PUBLIC MST_API void MST_add_empty_space_to_array(Array* arr) {
  Array_data* v = (Array_data*)malloc(sizeof(Array_data));
  assert(v);
  v->vt = VAR_SPACE;
  v->obj = malloc(sizeof(SPACE));
  assert(v->obj);
  ((SPACE*)v->obj)->the_space = list_new();
  v->this_list = list_add_val((uintptr_t) v, arr->the_array);
}
PUBLIC MST_API void MST_add_empty_array_to_array(Array* arr) {
  Array_data* v = (Array_data*)malloc(sizeof(Array_data));
  assert(v);
  v->vt = VAR_ARRAY;
  v->obj = malloc(sizeof(Array));
  assert(v->obj);
  ((Array*)v->obj)->the_array = list_new();
  v->this_list =  list_add_val((uintptr_t) v, arr->the_array);
}

PRIVATE void mst_add_str(mstr* result, char* str, int space_no) {
  for (int i = 0; i < space_no; i++) {
    mstr_add_char(result, '\t');
  }
  mstr_add_str(result, str);
}
PRIVATE void build_array(mstr *result, Array *arr, int spaces_no) {
  int flag = 0;
  mst_add_str(result, "[", 0);
  for (int i = 0; MST_array_get_data(arr, i) != NULL; i++) {
    flag = 1;
    // printf(".\n");
    Array_data* ad = (Array_data*) MST_array_get_data(arr, i);
    switch (ad->vt) {
      case VAR_SPACE:
        mstr_add_str(result, "{\n");
        build_space(result, MST_array_get_space(ad), spaces_no + 1);
        mst_add_str(result, "}", spaces_no);
        break;
      case VAR_INTEGER: {
        char num_buff[50];
        sprintf(num_buff, "%d", MST_array_get_integer(ad));
        mstr_add_str(result, num_buff);
        break;
      }
      case VAR_STRING: {
        char* buff = (char*)malloc(strlen(MST_array_get_str(ad)) + 3);
        assert(buff);
        strcpy(buff, MST_array_get_str(ad));
        sprintf(buff, "\"%s\"", MST_array_get_str(ad));
        mstr_add_str(result, buff);
        free(buff);
        break;
      }
      case VAR_ARRAY:
        build_array(result, MST_array_get_array(ad), spaces_no);
        break;
      default:
        break;
    }
    mstr_add_char(result, ',');
  }
  if (flag) {
    mstr_backspace(result);
  }
  mstr_add_char(result, ']');
}
PRIVATE void build_space(mstr* result, SPACE* space, int spaces_no) {
  for (int i = 1; list_search_by_count(i, space->the_space) != NULL; i++) {
    Var* sp = (Var*) list_search_by_count(i, space->the_space)->val;
    char* n = (char*)malloc(strlen(sp->name) + 6);
    assert(n);
    strcpy(n, sp->name);
    sprintf(n, "\"%s\" = ", sp->name);
    mst_add_str(result, n, spaces_no);
    free(n);
    switch (sp->vt) {
      case VAR_SPACE:
        mst_add_str(result, "{\n", spaces_no);
        build_space(result, MST_space_get_space(sp), spaces_no + 1);
        mst_add_str(result, "}", spaces_no);
        break;
      case VAR_INTEGER: {
        char num_buff[50];
        sprintf(num_buff, "%d", MST_space_get_integer(sp));
        mstr_add_str(result, num_buff);
        break;
      }
      case VAR_STRING: {
        char* buff = (char*)malloc(strlen(MST_space_get_str(sp)) + 3);
        assert(buff);
        strcpy(buff, MST_space_get_str(sp));
        sprintf(buff, "\"%s\"", MST_space_get_str(sp));
        mstr_add_str(result, buff);
        free(buff);
        break;
      }
      case VAR_ARRAY:
        build_array(result, MST_space_get_array(sp), spaces_no);
        break;
      default:
        break;
    }
    mstr_add_char(result, '\n');
  }
}
PUBLIC MST_API void MST_change_var(MST_Object* mst_obj, Var v, Var* v1) {
  if (v1->vt == VAR_SPACE) {
    free_space((SPACE*)v1->obj);
    list_delete(((SPACE *) v1->obj)->the_space);
  }
  free(v1->obj);
  List* backup = v1->this_list; // 这个位置还是不变的，所以要记录一下
  put_token(v.name, TK_WORD, mst_obj);
  memcpy(v1, &v, sizeof(Var));
  if (v1->vt == VAR_STRING) {
    String* s = (String*)v1->obj;
    put_token(s->str, TK_STRING, mst_obj);
  }
  v1->this_list = backup;
}
PUBLIC MST_API void MST_change_arr(MST_Object* mst_obj, Array_data v, Array_data* v1) {
  if (v1->vt == VAR_SPACE) {
    free_space((SPACE*)v1->obj);
    list_delete(((SPACE *) v1->obj)->the_space);
  }
  free(v1->obj);
  memcpy(v1, &v, sizeof(Var));
  if (v1->vt == VAR_STRING) {
    String* s = (String*)v1->obj;
    put_token(s->str, TK_STRING, mst_obj);
  }
}
PUBLIC MST_API void MST_remove_arr(Array_data* v) {
  assert(v->obj);
  if (v->vt == VAR_SPACE) {
    free_space((SPACE*)v->obj);
    list_delete(((SPACE *) v->obj)->the_space);
  }
  free(v->obj);
  assert(v->this_list);
  list_delete_child(v->this_list,v->this_list->ctl->start);
  free(v);
}
PUBLIC MST_API void MST_remove_var(Var* v) {
  if (v->vt == VAR_SPACE) {
    free_space((SPACE*)v->obj);
    list_delete(((SPACE *) v->obj)->the_space);
  }
  free(v->obj);
  list_delete_child(v->this_list,v->this_list->ctl->start);
  free(v);
}
PUBLIC MST_API char* MST_build_to_string(MST_Object* mst_obj) {
  mstr* ms = mstr_init();
  build_space(ms, MST_get_root_space(mst_obj), 0);
  mstr_backspace(ms);

  char* s = (char*)malloc(strlen(mstr_get(ms)) + 1);
  assert(s);
  strcpy(s, mstr_get(ms));
  mstr_free(ms);
  return s;
}