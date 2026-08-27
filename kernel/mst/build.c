#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mst.h"
#include "mstr.h"
#ifdef MEM_LEAK_CHK
void* Malloc(size_t size);
void Free(void* ptr);
#define malloc Malloc
#define free Free
#endif
PRIVATE bool put_token(char* buf, TOKEN_TYPE t, MST_Object* mst) {
  TOKEN* tok = malloc(sizeof(TOKEN));
  if (tok == NULL) {
    free(buf);
    mst->err = WRONG_TYPE_TO_ADD;
    return false;
  }
  tok->t = t;
  tok->tok = buf;
  if (!AddVal((uintptr_t)tok, mst->token)) {
    free(buf);
    free(tok);
    mst->err = WRONG_TYPE_TO_ADD;
    return false;
  }
  return true;
}
PRIVATE void rollback_last_token(MST_Object* mst) {
  if (mst == NULL || mst->token == NULL || mst->token->ctl->all == 0) {
    return;
  }
  size_t index = mst->token->ctl->all;
  TOKEN* token = (TOKEN*)(uintptr_t)FindForCount(index, mst->token)->val;
  DeleteVal(index, mst->token);
  free(token->tok);
  free(token);
}
PUBLIC MST_API Var MST_var_make_integer(char* name, int val) {
  Var r = {0};
  if (name == NULL) {
    return r;
  }
  char* s = (char*)malloc(strlen(name) + 1);
  if (s == NULL) {
    return r;
  }
  strcpy(s, name);
  r.name = s;
  r.vt = INTEGER;
  r.obj = malloc(sizeof(Integer));
  if (r.obj == NULL) {
    free(s);
    r.name = NULL;
    return r;
  }
  ((Integer*)(r.obj))->num = val;
  return r;
}
PUBLIC MST_API Var MST_var_make_string(char* name, char* ss) {
  Var r = {0};
  if (name == NULL || ss == NULL) {
    return r;
  }
  char* s = (char*)malloc(strlen(name) + 1);
  if (s == NULL) {
    return r;
  }
  strcpy(s, name);
  r.name = s;
  r.vt = STR;
  r.obj = malloc(sizeof(String));
  if (r.obj == NULL) {
    free(s);
    r.name = NULL;
    return r;
  }
  char* s1 = (char*)malloc(strlen(ss) + 1);
  if (s1 == NULL) {
    free(r.obj);
    free(s);
    r.obj = NULL;
    r.name = NULL;
    return r;
  }
  strcpy(s1, ss);
  ((String*)(r.obj))->str = s1;
  return r;
}

PUBLIC MST_API Array_data MST_arr_dat_make_integer(int val) {
  Array_data r = {0};
  r.vt = INTEGER;
  r.obj = malloc(sizeof(Integer));
  if (r.obj == NULL) {
    return r;
  }
  ((Integer*)(r.obj))->num = val;
  return r;
}
PUBLIC MST_API Array_data MST_arr_dat_make_string(char* ss) {
  Array_data r = {0};
  if (ss == NULL) {
    return r;
  }
  r.vt = STR;
  r.obj = malloc(sizeof(String));
  if (r.obj == NULL) {
    return r;
  }
  char* s1 = (char*)malloc(strlen(ss) + 1);
  if (s1 == NULL) {
    free(r.obj);
    r.obj = NULL;
    return r;
  }
  strcpy(s1, ss);
  ((String*)(r.obj))->str = s1;
  return r;
}

// TODO:我们需要将name（char *）的数据送到token这个链表里面，因为我们需要Free
PUBLIC MST_API void MST_add_data_to_array(MST_Object* mst_obj,
                                          Array* arr,
                                          Array_data ad) {
  if (ad.vt == SPAC || ad.vt == ARRAY) {
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  Array_data* v = (Array_data*)malloc(sizeof(Array_data));
  if (v == NULL || ad.obj == NULL) {
    free(v);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  memcpy(v, &ad, sizeof(Array_data));
  if (!AddVal((uintptr_t)v, arr->the_array)) {
    if (v->vt == STR) {
      free(((String*)v->obj)->str);
    }
    free(v->obj);
    free(v);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  if (v->vt == STR &&
      !put_token(((String*)v->obj)->str, STRING, mst_obj)) {
    DeleteVal(arr->the_array->ctl->all, arr->the_array);
    free(v->obj);
    free(v);
  }
}
PUBLIC MST_API void MST_add_var_to_space(MST_Object* mst_obj,
                                         SPACE* sp,
                                         Var var) {
  if (var.vt == SPAC || var.vt == ARRAY) {
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  Var* v = (Var*)malloc(sizeof(Var));
  if (v == NULL || var.name == NULL || var.obj == NULL) {
    free(v);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  memcpy(v, &var, sizeof(Var));
  if (!AddVal((uintptr_t)v, sp->the_space)) {
    free(v->name);
    if (v->vt == STR) {
      free(((String*)v->obj)->str);
    }
    free(v->obj);
    free(v);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  if (!put_token(v->name, WORD, mst_obj)) {
    DeleteVal(sp->the_space->ctl->all, sp->the_space);
    if (v->vt == STR) {
      free(((String*)v->obj)->str);
    }
    free(v->obj);
    free(v);
    return;
  }
  if (v->vt == STR &&
      !put_token(((String*)v->obj)->str, STRING, mst_obj)) {
    rollback_last_token(mst_obj);
    DeleteVal(sp->the_space->ctl->all, sp->the_space);
    free(v->obj);
    free(v);
  }
}
PUBLIC MST_API void MST_add_empty_space_to_space(MST_Object* mst_obj,
                                                 SPACE* sp,
                                                 char* name) {
  if (mst_obj == NULL || sp == NULL || name == NULL) {
    return;
  }
  char* s = (char*)malloc(strlen(name) + 1);
  Var* v = (Var*)malloc(sizeof(Var));
  SPACE* inner = (SPACE*)malloc(sizeof(SPACE));
  if (s == NULL || v == NULL || inner == NULL) {
    free(s);
    free(v);
    free(inner);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  strcpy(s, name);
  v->name = s;
  v->vt = SPAC;
  v->obj = inner;
  inner->the_space = NewList();
  if (inner->the_space == NULL || !AddVal((uintptr_t)v, sp->the_space)) {
    if (inner->the_space != NULL) {
      DeleteList(inner->the_space);
    }
    free(inner);
    free(v);
    free(s);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  if (!put_token(s, WORD, mst_obj)) {
    DeleteVal(sp->the_space->ctl->all, sp->the_space);
    DeleteList(inner->the_space);
    free(inner);
    free(v);
    return;
  }
}
PUBLIC MST_API void MST_add_empty_array_to_space(MST_Object* mst_obj,
                                                 SPACE* sp,
                                                 char* name) {
  if (mst_obj == NULL || sp == NULL || name == NULL) {
    return;
  }
  char* s = (char*)malloc(strlen(name) + 1);
  Var* v = (Var*)malloc(sizeof(Var));
  Array* inner = (Array*)malloc(sizeof(Array));
  if (s == NULL || v == NULL || inner == NULL) {
    free(s);
    free(v);
    free(inner);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  strcpy(s, name);
  v->name = s;
  v->vt = ARRAY;
  v->obj = inner;
  inner->the_array = NewList();
  if (inner->the_array == NULL || !AddVal((uintptr_t)v, sp->the_space)) {
    if (inner->the_array != NULL) {
      DeleteList(inner->the_array);
    }
    free(inner);
    free(v);
    free(s);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  if (!put_token(s, WORD, mst_obj)) {
    DeleteVal(sp->the_space->ctl->all, sp->the_space);
    DeleteList(inner->the_array);
    free(inner);
    free(v);
    return;
  }
}
PUBLIC MST_API void MST_add_empty_space_to_array(MST_Object* mst_obj,
                                                 Array* arr) {
  if (mst_obj == NULL || arr == NULL) {
    return;
  }
  Array_data* v = (Array_data*)malloc(sizeof(Array_data));
  SPACE* inner = (SPACE*)malloc(sizeof(SPACE));
  if (v == NULL || inner == NULL) {
    free(v);
    free(inner);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  v->vt = SPAC;
  v->obj = inner;
  inner->the_space = NewList();
  if (inner->the_space == NULL || !AddVal((uintptr_t)v, arr->the_array)) {
    if (inner->the_space != NULL) {
      DeleteList(inner->the_space);
    }
    free(inner);
    free(v);
    mst_obj->err = WRONG_TYPE_TO_ADD;
  }
}
PUBLIC MST_API void MST_add_empty_array_to_array(MST_Object* mst_obj,
                                                 Array* arr) {
  if (mst_obj == NULL || arr == NULL) {
    return;
  }
  Array_data* v = (Array_data*)malloc(sizeof(Array_data));
  Array* inner = (Array*)malloc(sizeof(Array));
  if (v == NULL || inner == NULL) {
    free(v);
    free(inner);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return;
  }
  v->vt = ARRAY;
  v->obj = inner;
  inner->the_array = NewList();
  if (inner->the_array == NULL || !AddVal((uintptr_t)v, arr->the_array)) {
    if (inner->the_array != NULL) {
      DeleteList(inner->the_array);
    }
    free(inner);
    free(v);
    mst_obj->err = WRONG_TYPE_TO_ADD;
  }
}

PRIVATE void mst_add_str(mstr* result, char* str, int space_no) {
  for (int i = 0; i < space_no; i++) {
    mstr_add_char(result, '\t');
  }
  mstr_add_str(result, str);
}
PRIVATE bool build_space(mstr* result, SPACE* space, int spaces_no);
PRIVATE bool build_array(mstr* result,
                         Array* arr,
                         int spaces_no_for_start,
                         int spaces_no) {
  int flag = 0;
  mst_add_str(result, "[", spaces_no_for_start);
  for (int i = 0; MST_Array_Get(arr, i) != NULL; i++) {
    flag = 1;
    // printk(".\n");
    Array_data* ad = (Array_data*)MST_Array_Get(arr, i);
    switch (ad->vt) {
      case SPAC:
        mstr_add_str(result, "{\n");
        if (!build_space(result, MST_Array_get_space(ad), spaces_no + 1)) {
          return false;
        }
        mst_add_str(result, "}", spaces_no);
        break;
      case INTEGER: {
        char num_buff[50];
        sprintf(num_buff, "%d", MST_Array_get_integer(ad));
        mstr_add_str(result, num_buff);
        break;
      }
      case STR: {
        char* buff = (char*)malloc(strlen(MST_Array_get_str(ad)) + 3);
        if (buff == NULL) {
          return false;
        }
        strcpy(buff, MST_Array_get_str(ad));
        sprintf(buff, "\"%s\"", MST_Array_get_str(ad));
        mstr_add_str(result, buff);
        free(buff);
        break;
      }
      case ARRAY:
        if (!build_array(result, MST_Array_get_array(ad), 0, spaces_no)) {
          return false;
        }
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
  return true;
}
PRIVATE bool build_space(mstr* result, SPACE* space, int spaces_no) {
  for (int i = 1; FindForCount(i, space->the_space) != NULL; i++) {
    Var* sp = (Var*)FindForCount(i, space->the_space)->val;
    //  printk("name found!\n");
    char* n = (char*)malloc(strlen(sp->name) + 6);
    if (n == NULL) {
      return false;
    }
    strcpy(n, sp->name);
    sprintf(n, "\"%s\" = ", sp->name);
    // printk("n=%s %d\n",n,i);
    mst_add_str(result, n, spaces_no);
    free(n);
    switch (sp->vt) {
      case SPAC:
        mst_add_str(result, "{\n", spaces_no);
        if (!build_space(result, MST_Space_GetSpace(sp), spaces_no + 1)) {
          return false;
        }
        mst_add_str(result, "}", spaces_no);
        break;
      case INTEGER: {
        char num_buff[50];
        sprintf(num_buff, "%d", MST_Space_GetInteger(sp));
        mstr_add_str(result, num_buff);
        break;
      }
      case STR: {
        char* buff = (char*)malloc(strlen(MST_Space_GetStr(sp)) + 3);
        if (buff == NULL) {
          return false;
        }
        strcpy(buff, MST_Space_GetStr(sp));
        sprintf(buff, "\"%s\"", MST_Space_GetStr(sp));
        mstr_add_str(result, buff);
        free(buff);
        break;
      }
      case ARRAY:
        if (!build_array(result, MST_Space_GetArray(sp), 0, spaces_no)) {
          return false;
        }
        break;
      default:
        break;
    }
    mstr_add_char(result, '\n');
  }
  return true;
}
PRIVATE void free_mst_value(VAR_TYPE type, void* object);
PRIVATE void free_mst_array(Array* array) {
  if (array == NULL || array->the_array == NULL) {
    return;
  }
  for (int i = 1; FindForCount(i, array->the_array) != NULL; i++) {
    Array_data* value =
        (Array_data*)(uintptr_t)FindForCount(i, array->the_array)->val;
    if (value != NULL) {
      free_mst_value(value->vt, value->obj);
      free(value);
    }
  }
  DeleteList(array->the_array);
}
PRIVATE void free_mst_space(SPACE* space) {
  if (space == NULL || space->the_space == NULL) {
    return;
  }
  for (int i = 1; FindForCount(i, space->the_space) != NULL; i++) {
    Var* value = (Var*)(uintptr_t)FindForCount(i, space->the_space)->val;
    if (value != NULL) {
      free_mst_value(value->vt, value->obj);
      free(value);
    }
  }
  DeleteList(space->the_space);
}
PRIVATE void free_mst_value(VAR_TYPE type, void* object) {
  if (object == NULL) {
    return;
  }
  if (type == SPAC) {
    free_mst_space((SPACE*)object);
  } else if (type == ARRAY) {
    free_mst_array((Array*)object);
  }
  free(object);
}
PUBLIC MST_API void MST_change_var(MST_Object* mst_obj, Var v, Var* v1) {
  if (mst_obj == NULL || v1 == NULL || v.name == NULL || v.obj == NULL) {
    return;
  }
  if (!put_token(v.name, WORD, mst_obj)) {
    if (v.vt == STR) {
      free(((String*)v.obj)->str);
    }
    free_mst_value(v.vt, v.obj);
    return;
  }
  if (v.vt == STR && !put_token(((String*)v.obj)->str, STRING, mst_obj)) {
    rollback_last_token(mst_obj);
    free(v.obj);
    return;
  }
  free_mst_value(v1->vt, v1->obj);
  memcpy(v1, &v, sizeof(Var));
}
PUBLIC MST_API void MST_change_arr(MST_Object* mst_obj, Array_data v, Array_data* v1) {
  if (mst_obj == NULL || v1 == NULL || v.obj == NULL) {
    return;
  }
  if (v.vt == STR && !put_token(((String*)v.obj)->str, STRING, mst_obj)) {
    free(v.obj);
    return;
  }
  free_mst_value(v1->vt, v1->obj);
  memcpy(v1, &v, sizeof(Array_data));
}
PUBLIC MST_API char* MST_build_to_string(MST_Object* mst_obj) {
  if (mst_obj == NULL || MST_GetRootSpace(mst_obj) == NULL) {
    return NULL;
  }
  mstr* ms = mstr_init();
  if (ms == NULL) {
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return NULL;
  }
  // printk("mstr init ok!\n");
  if (!build_space(ms, MST_GetRootSpace(mst_obj), 0)) {
    mstr_free(ms);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return NULL;
  }
  if (strlen(mstr_get(ms)) != 0) {
    mstr_backspace(ms);
  }
  // printk("build_space!\n");
  char* s = (char*)malloc(strlen(mstr_get(ms)) + 1);
  if (s == NULL) {
    mstr_free(ms);
    mst_obj->err = WRONG_TYPE_TO_ADD;
    return NULL;
  }
#ifdef MEM_LEAK_CHK
  // s这个指针需要用户自己去释放，因此不计算在malloc次数中
  extern int _malloc_times;
  _malloc_times--;
#endif
  strcpy(s, mstr_get(ms));
  mstr_free(ms);
  return s;
}
