#include "mst.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MEM_LEAK_CHK
int _malloc_times = 0;
int _free_times = 0;
void* Malloc(size_t size) {
  _malloc_times++;
  return malloc(size);
}
void Free(void* ptr) {
  _free_times++;
  return free(ptr);
}
#define malloc Malloc
#define free Free
#endif
PRIVATE bool put_token(char* buf, TOKEN_TYPE t, MST_Object* mst) {
  TOKEN* tok = malloc(sizeof(TOKEN));
  if (tok == NULL) {
    free(buf);
    mst->err = ERROR_TOKEN;
    return false;
  }
  tok->t = t;
  tok->tok = buf;
  if (!AddVal((uintptr_t)tok, mst->token)) {
    free(buf);
    free(tok);
    mst->err = ERROR_TOKEN;
    return false;
  }
  return true;
}
PRIVATE char* __next(char* buf) {
  char *x, *y;
  x = strchr(buf, ' ');   // 找到最近的空格
  y = strchr(buf, '\t');  // 找到最近的缩进
  /*
    如果x、y都不为零，那就取小的那个
    如果x、y都为0，那就返回0
    若是x、y其中一个为0，那就返回不为0的那个
  */
  return x < y ? (x != 0 ? x : y) : (y != 0 ? y : x);
}
PRIVATE char* _next(char* buf) {
  if (buf[0] == '#') {  // 是注释，我们不用做任何事情
    return buf;         // do nothing
  }
  char *x, *y;
  /* 下一个token的分隔符是\n还是空格或缩进，因此执行一遍__next函数*/
  x = __next(buf);
  y = strchr(buf, '\n');
  return x < y ? (x != 0 ? x : y) : (y != 0 ? y : x);
}
PRIVATE char* next1(char* buf) {
  char* p = _next(buf);
  if (!(p))
    return 0;
  while (
      p[0] ==
      '#') {  // 跳过注释，下面的代码执行后，p指针就会指向下一行，因此，只要下一行不是注释，就可以退出循环了
    p = strchr(p, '\n');  // 下一行
    if (p) {
      p++;  // 此时*p是'\n'我们需要跳过一格，然后才是下一行的起始位置
      if (!(*p)) {  // 遇到了字符串结束符，也相当于没有token了，返回0
        return 0;
      }
    } else {
      return 0;
    }
  }
  while (*p == ' ' || *p == '\t' || *p == '\n') {  // 找到有字符的地方
    p++;
  }
  if (*p) {                // 如果字符串没到结尾
    while (p[0] == '#') {  // 继续跳过注释
      p = strchr(p, '\n');
      if (p) {
        p++;
        if (!(*p)) {
          return 0;
        }
      } else {
        return 0;
      }
    }
    while ((*p == ' ' || *p == '\t' || *p == '\n') &&
           *p) {  // 继续跳过没字符的地方，于是我们就到了下一个有字符的地方
      p++;
    }
    return *p ? p : 0;
  } else
    return 0;
}
PRIVATE char* next(char* buf) {
  char* p =
      next1(buf);  // 其实上面注释什么的还没有处理完，所以现在我们进行下一步处理
  // next1函数处理的是注释符号在行的开头的情况，我们现在要处理注释和正常字符混合在一起的情况
  if (!p) {  // 上一步都没返回成功，直接0了
    return 0;
  }
  char* p1 = p;
  int fg = 0;  // 字符串标识符，这里是为了防止字符串里的注释也被误判
  while ((*p != '#' && fg) && *p != ' ' && *p) {
    if (*p == '\"') {
      fg = !fg;
    }
    p++;
  }
  // 此时p代表的就是下一个注释的起始位置
  if (*p != '#') {  // 没有注释？直接返回了
    return p1;
  } else {
    while (*p != '\n' && *p != '\0') {  // 把这个注释删了
      *p = ' ';
      p++;
    }
    return p1;
  }
}
PRIVATE char* get_str(char* text,
                      char** p) {  // 从next返回的token中获取有效字符串
  size_t len = 0;
  char* src = text;
  char* result;
  int flag_str = 0;   // 字符串标识
  int flag_str1 = 0;  // 字符串内转义字符标识
  int flag_op = 0;    // 数组标识
  while (*text) {
    len++;
    if (!flag_str &&
        *text == '[') {  // 不在字符串内并且是数组的开始，设置flag_op标识(+1 代表嵌套的层数)
      flag_op++;
    }
    if (!flag_str &&
        *text == ']') {  // 不在字符串内并且是数组的结束，结束flag_op标识（-1 少一个嵌套的层数）
      if (!flag_op) {  // 只有闭合，没有开始，报错
        return NULL;
      }
      flag_op--;
    }
    /* 处理字符串 */
    if (!flag_op) {
      if (*text == '\\') { // 转义字符
        flag_str1 = !flag_str1;
      } else if (flag_str1) {
        flag_str1 = 0;
      } else {
        if (*text == '\"') {
          flag_str = !flag_str;
        }
      }
    }
    if (*text == '#' || *text == ' ' || *text == '\n') {
      if (flag_str == 0 && flag_op == 0) {
        // printk("This.\n");
        break;
      }
    }
    text++;
  }
  if (flag_op + flag_str1 + flag_str != 0) {
    return NULL;
  }
  result = (char*)malloc(len + 1);
  if (result == NULL) {
    return NULL;
  }
  result[len] = 0;
  memcpy(result, src, len);
  *p = text;
  return result;
}
PRIVATE size_t token_strlen(char* s) {
  size_t result = 0;
  if (*s != '\"') {
    return 0;
  }
  s++;
  while (*s != '\"' && *s) {
    result++;
    s++;
  }
  return result;
}
PRIVATE int token_put_string(char* p1, MST_Object* mst) {
  char* result;
  result = malloc(token_strlen(p1) + 1);
  if (result == NULL) {
    mst->err = ERROR_TOKEN;
    return 0;
  }
  TOKEN_TYPE tt;
  size_t length = strlen(p1);
  int flag = 0;
  if (p1[0] == '\"') {
    tt = STRING;
    int j = 0;
    for (int i = 1; i < length; i++) {
      if (p1[i] == '\"' && !flag) {
        break;
      }
      if (flag) {
        flag = 0;
      }
      if (p1[i] == '\\') {
        flag = 1;
      }
      result[j++] = p1[i];
    }
    result[j] = 0;
    if (!put_token(result, tt, mst)) {
      return 0;
    }
    return j + 1;
  } else {
    free(result);
    return 0;
  }
}
PRIVATE int token_put_integer(char* p1, MST_Object* mst) {
  char* result;
  result = malloc(strlen(p1) + 1);
  if (result == NULL) {
    mst->err = ERROR_TOKEN;
    return 0;
  }
  size_t length = strlen(p1);
  int j = 0;
  if (p1[0] == '-') {
    result[j++] = p1[0];
    p1++;
    length--;
  }
  for (int i = 0; i < length; i++) {
    if (isdigit(p1[i])) {
      result[j++] = p1[i];
    } else {
      break;
    }
  }
  result[j] = 0;
  if (!put_token(result, NUMBER, mst)) {
    return 0;
  }
  return j - 1;
}
PRIVATE bool put_symbol_token(char symbol, TOKEN_TYPE type, MST_Object* mst) {
  char* token = malloc(2);
  if (token == NULL) {
    mst->err = ERROR_TOKEN;
    return false;
  }
  token[0] = symbol;
  token[1] = '\0';
  return put_token(token, type, mst);
}
PRIVATE void auto_put_token(char* p1, MST_Object* mst) {
  // printk("p1=%s\n",p1);

  size_t length = strlen(p1);

  for (int i = 0; i < length; i++) {
    switch (p1[i]) {
      case '\"':
        //   printk("str.\n");
        i = i + token_put_string(p1 + i, mst);
        break;
      case '[': {
        if (!put_symbol_token('[', ARRAY_START, mst)) return;
        break;
      }
      case ']': {
        if (!put_symbol_token(']', ARRAY_END, mst)) return;
        break;
      }
      case '{': {
        if (!put_symbol_token('{', SPACE_START, mst)) return;
        break;
      }
      case '}': {
        if (!put_symbol_token('}', SPACE_END, mst)) return;
        break;
      }
      case '=': {
        if (!put_symbol_token('=', OP, mst)) return;
        break;
      }
      case ' ':
      case '\r':
      case '\n':
      case ',':
      case '\t':
        break;
      default: {
        if (isdigit(p1[i]) || p1[i] == '-') {
          i = i + token_put_integer(p1 + i, mst);
        } else {
          mst->err = UNKNOW_TOKEN;
          return;
        }
        break;
      }
    }
  }
}
PRIVATE void Mst_lexer(char* buffer, MST_Object* mst) {
  char* start = buffer;
  char* p = start;
  while (p) {
    if (p[0] != '#') {
      // 处理token
      // printk("------\n%s-----\n",p);
      char* p1 = get_str(p, &p);
      if (!p1) {
        mst->err = STRING_OP_ERROR;
        return;
      }
      auto_put_token(p1, mst);
      free(p1);
      if (mst->err) {
        return;
      }
    }
    p = next(p);  // next
  }
}
PRIVATE void free_arr(Array* arr);
PRIVATE void free_space(SPACE* space);
PRIVATE void process_token(MST_Object* mobj) {
  TOKEN* t_bmp[2];
  int idx = 0;
  for (int i = 1; FindForCount(i, mobj->token) != NULL; i++) {
    TOKEN* t = (TOKEN*)FindForCount(i, mobj->token)->val;
    t_bmp[idx] = t;
    if (idx) {
      if (t_bmp[0]->t == STRING &&
          (t_bmp[1]->t == OP && t_bmp[1]->tok[0] == '=')) {
        t_bmp[0]->t = WORD;
      } else if (t_bmp[1]->t == STRING) {
        t_bmp[0] = t_bmp[1];
        idx = 0;
      }
    }
    idx = !idx;
  }
}
PRIVATE int parser_space(MST_Object* mst, int idx, SPACE* space, int mode);
PRIVATE int parser_array(MST_Object* mst, int idx, Array* arr) {
  int i = idx;
  for (; FindForCount(i, mst->token) != NULL; i++) {
    TOKEN* t = (TOKEN*)FindForCount(i, mst->token)->val;
    if (t->t == ARRAY_END) {
      break;
    }
    if (t->t == SPACE_START) {
      //     printk("Array:Found a SPACE\n");
      Array_data* v = (Array_data*)malloc(sizeof(Array_data));
      SPACE* sp = (SPACE*)malloc(sizeof(SPACE));
      if (v == NULL || sp == NULL) {
        free(v);
        free(sp);
        mst->err = ERROR_TOKEN;
        return -1;
      }
      v->vt = SPAC;
      v->obj = sp;
      sp->the_space = NewList();
      if (sp->the_space == NULL) {
        free(sp);
        free(v);
        mst->err = ERROR_TOKEN;
        return -1;
      }
      i = parser_space(mst, i + 1, sp, 0);
      if (i == -1) {
        free_space(sp);
        DeleteList(sp->the_space);
        free(v->obj);
        free(v);
        return -1;
      }
      if (!AddVal((uintptr_t)v, arr->the_array)) {
        free_space(sp);
        DeleteList(sp->the_space);
        free(v->obj);
        free(v);
        mst->err = ERROR_TOKEN;
        return -1;
      }
    } else if (t->t == ARRAY_START) {
      //  printk("Array:Found a Array\n");
      Array_data* v = (Array_data*)malloc(sizeof(Array_data));
      Array* arr1 = (Array*)malloc(sizeof(Array));
      if (v == NULL || arr1 == NULL) {
        free(v);
        free(arr1);
        mst->err = ERROR_TOKEN;
        return -1;
      }
      v->vt = ARRAY;
      v->obj = arr1;
      arr1->the_array = NewList();
      if (arr1->the_array == NULL) {
        free(arr1);
        free(v);
        mst->err = ERROR_TOKEN;
        return -1;
      }
      i = parser_array(mst, i + 1, arr1);
      if (i == -1) {
        free_arr(arr1);
        DeleteList(arr1->the_array);
        free(v->obj);
        free(v);
        return -1;
      }
      if (!AddVal((uintptr_t)v, arr->the_array)) {
        free_arr(arr1);
        DeleteList(arr1->the_array);
        free(v->obj);
        free(v);
        mst->err = ERROR_TOKEN;
        return -1;
      }
    } else if (t->t == INTEGER) {
      //  printk("Array:Found a Number:%s\n",t->tok);
      Array_data* v = (Array_data*)malloc(sizeof(Array_data));
      Integer* number = (Integer*)malloc(sizeof(Integer));
      if (v == NULL || number == NULL) {
        free(v);
        free(number);
        mst->err = ERROR_TOKEN;
        return -1;
      }
      v->vt = INTEGER;
      v->obj = number;
      number->num = strtol(t->tok, NULL, 10);
      if (!AddVal((uintptr_t)v, arr->the_array)) {
        free(v->obj);
        free(v);
        mst->err = ERROR_TOKEN;
        return -1;
      }
    } else if (t->t == STRING) {
      //   printk("Array:Found a String:%s\n",t->tok);
      Array_data* v = (Array_data*)malloc(sizeof(Array_data));
      String* str = (String*)malloc(sizeof(String));
      if (v == NULL || str == NULL) {
        free(v);
        free(str);
        mst->err = ERROR_TOKEN;
        return -1;
      }
      v->vt = STR;
      v->obj = str;
      str->str = t->tok;
      if (!AddVal((uintptr_t)v, arr->the_array)) {
        free(v->obj);
        free(v);
        mst->err = ERROR_TOKEN;
        return -1;
      }
    } else {
      mst->err = ERROR_TOKEN;
      return -1;
    }
  }
  return i;
}
PRIVATE void free_space(SPACE* space);
PRIVATE void free_arr(Array* arr) {
  int i = 1;
  for (; FindForCount(i, arr->the_array) != NULL; i++) {
    Array_data* v = (Array_data*)FindForCount(i, arr->the_array)->val;
    switch (v->vt) {
      case INTEGER:
        free(v->obj);
        break;
      case STR:
        free(v->obj);
        break;
      case SPAC:
        free_space((SPACE*)v->obj);
        DeleteList(((SPACE*)v->obj)->the_space);
        free(v->obj);
        break;
      case ARRAY:
        free_arr((Array*)v->obj);
        DeleteList(((Array*)v->obj)->the_array);
        free(v->obj);
        break;
      default:
        break;
    }
    free(v);
  }
}
PRIVATE void free_space(SPACE* space) {
  int i = 1;
  if (!space) {
    return;
  }
  for (; FindForCount(i, space->the_space) != NULL; i++) {
    Var* v = (Var*)FindForCount(i, space->the_space)->val;
    switch (v->vt) {
      case INTEGER:
        free(v->obj);
        break;
      case STR:
        free(v->obj);
        break;
      case SPAC:
        free_space((SPACE*)v->obj);
        DeleteList(((SPACE*)v->obj)->the_space);
        free(v->obj);
        break;
      case ARRAY:
        free_arr((Array*)v->obj);
        DeleteList(((Array*)v->obj)->the_array);
        free(v->obj);
        break;
      default:
        break;
    }
    free(v);
  }
}
PRIVATE int parser_space(MST_Object* mst, int idx, SPACE* space, int mode) {
  int i = idx;
  int flag = 0;
  for (; FindForCount(i, mst->token) != NULL; i++) {
    TOKEN* t = (TOKEN*)FindForCount(i, mst->token)->val;
    if (!mode && t->t == SPACE_END) {
      flag = 1;
      break;
    }
    if (t->t == WORD) {
      Var* v = (Var*)malloc(sizeof(Var));
      if (v == NULL) {
        mst->err = ERROR_TOKEN;
        return -1;
      }
      v->name = t->tok;
      List* tk_list1 = FindForCount(i + 1, mst->token);
      if (!tk_list1) {
        free(v);
        mst->err = SYNTAX_ERROR;
        return -1;
      }
      TOKEN* t2 = (TOKEN*)tk_list1->val;
      if (t2->t != OP) {
        free(v);
        mst->err = SYNTAX_ERROR;
        return -1;
      }
      List* tk_list2 = FindForCount(i + 2, mst->token);
      if (!tk_list2) {
        free(v);
        mst->err = SYNTAX_ERROR;
        return -1;
      }
      TOKEN* t1 = (TOKEN*)tk_list2->val;
      if (t1->t == SPACE_START) {
        // printk("START\n");
        //    printk("Found a Space %s\n",t->tok);
        v->vt = SPAC;
        SPACE* sp = (SPACE*)malloc(sizeof(SPACE));
        if (sp == NULL) {
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        v->obj = sp;
        sp->the_space = NewList();
        if (sp->the_space == NULL) {
          free(sp);
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        i = parser_space(mst, i + 3, sp, 0);
        if (i == -1) {
          free_space(sp);
          DeleteList(sp->the_space);
          free(v->obj);
          free(v);
          return -1;
        }
        if (!AddVal((uintptr_t)v, space->the_space)) {
          free_space(sp);
          DeleteList(sp->the_space);
          free(v->obj);
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        // i += 2;
      } else if (t1->t == ARRAY_START) {
        //    printk("Found a Array:%s\n",t->tok);
        v->vt = ARRAY;
        Array* arr = (Array*)malloc(sizeof(Array));
        if (arr == NULL) {
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        v->obj = arr;
        arr->the_array = NewList();
        if (arr->the_array == NULL) {
          free(arr);
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        i = parser_array(mst, i + 3, arr);
        if (i == -1) {
          free_arr(arr);
          DeleteList(arr->the_array);
          free(v->obj);
          free(v);
          return -1;
        }
        if (!AddVal((uintptr_t)v, space->the_space)) {
          free_arr(arr);
          DeleteList(arr->the_array);
          free(v->obj);
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
      } else if (t1->t == INTEGER) {
        //  printk("Space:Found a Number:%s\n",t->tok);
        // Array_data *v = (Array_data *)malloc(sizeof(Array_data));
        v->vt = INTEGER;
        Integer* number = (Integer*)malloc(sizeof(Integer));
        if (number == NULL) {
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        v->obj = number;
        number->num = strtol(t1->tok, NULL, 10);
        if (!AddVal((uintptr_t)v, space->the_space)) {
          free(v->obj);
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        i += 2;
      } else if (t1->t == STRING) {
        // printk("Space:Found a String:%s\n",t->tok);
        //  Array_data *v = (Array_data *)malloc(sizeof(Array_data));
        v->vt = STR;
        String* str = (String*)malloc(sizeof(String));
        if (str == NULL) {
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        v->obj = str;

        str->str = t1->tok;
        //  printk("string = %llx %llx\n", str->str, t1->tok);
        if (!AddVal((uintptr_t)v, space->the_space)) {
          free(v->obj);
          free(v);
          mst->err = ERROR_TOKEN;
          return -1;
        }
        i += 2;
      }
    } else {
      mst->err = SYNTAX_ERROR;
      return -1;
    }
  }
  if (!flag && space != MST_GetRootSpace(mst)) {
    // printk("fatal error: Couldn't find the end of opcode\n");
    // exit(-1);
    mst->err = SYNTAX_ERROR;
    return -1;
  }
  return i;
}
PUBLIC MST_API MST_Object* Init_MstObj(char* string) {
  if (string == NULL) {
    return NULL;
  }
  MST_Object* result = (MST_Object*)malloc(sizeof(MST_Object));
  if (result == NULL) {
    return NULL;
  }
  memset(result, 0, sizeof(MST_Object));
  result->string = malloc(strlen(string) + 1);
  if (result->string == NULL) {
    free(result);
    return NULL;
  }
  result->err = 0;
  result->root_space = NULL;
  strcpy(result->string, string);
  result->token = NewList();
  if (result->token == NULL) {
    free(result->string);
    free(result);
    return NULL;
  }
  Mst_lexer(result->string, result);
  if (!result->err) {
    process_token(result);
    result->root_space = (SPACE*)malloc(sizeof(SPACE));
    if (result->root_space == NULL) {
      result->err = ERROR_TOKEN;
      return result;
    }
    result->root_space->the_space = NewList();
    if (result->root_space->the_space == NULL) {
      free(result->root_space);
      result->root_space = NULL;
      result->err = ERROR_TOKEN;
      return result;
    }
    // printk("%08x\n",result->root_space->the_space);
    parser_space(result, 1, result->root_space, 1);
  }
  return result;
}
PUBLIC MST_API Var* MST_GetVar(char* name, SPACE* space) {
  for (int i = 1; FindForCount(i, space->the_space) != NULL; i++) {
    Var* sp = (Var*)FindForCount(i, space->the_space)->val;
    if (strcmp(name, sp->name) == 0) {
      return sp;
    }
  }
  return NULL;
}
PUBLIC MST_API int MST_Space_GetInteger(Var* var) {
  if (var->vt != INTEGER) {
    return -1;
  }
  Integer* n = (Integer*)var->obj;
  return n->num;
}
PUBLIC MST_API SPACE* MST_Space_GetSpace(Var* var) {
  if (var->vt != SPAC) {
    return NULL;
  }
  SPACE* n = (SPACE*)var->obj;
  return n;
}
PUBLIC MST_API Array* MST_Space_GetArray(Var* var) {
  if (var->vt != ARRAY) {
    return NULL;
  }
  Array* n = (Array*)var->obj;
  return n;
}
PUBLIC MST_API char* MST_Space_GetStr(Var* var) {
  if (var->vt != STR) {
    return NULL;
  }
  String* n = (String*)var->obj;
  return n->str;
}
PUBLIC MST_API Array_data* MST_Array_Get(Array* arr, int idx) {
  if (FindForCount(idx + 1, arr->the_array) == NULL) {
    return NULL;
  }
  return (Array_data*)FindForCount(idx + 1, arr->the_array)->val;
}
PUBLIC MST_API int MST_Array_get_integer(Array_data* ad) {
  if (ad->vt != INTEGER) {
    return -1;
  }
  Integer* n = (Integer*)ad->obj;
  return n->num;
}
PUBLIC MST_API char* MST_Array_get_str(Array_data* ad) {
  if (ad->vt != STR) {
    return NULL;
  }
  String* n = (String*)ad->obj;
  return n->str;
}
PUBLIC MST_API SPACE* MST_Array_get_space(Array_data* ad) {
  if (ad->vt != SPAC) {
    return NULL;
  }
  SPACE* n = (SPACE*)ad->obj;
  return n;
}
PUBLIC MST_API Array* MST_Array_get_array(Array_data* ad) {
  if (ad->vt != ARRAY) {
    return NULL;
  }
  Array* n = (Array*)ad->obj;
  return n;
}
PUBLIC MST_API void MST_FreeObj(MST_Object* mst) {
  if (mst == NULL) {
    return;
  }
  free(mst->string);
  if (mst->token != NULL) {
    for (int i = 1; FindForCount(i, mst->token) != NULL; i++) {
      TOKEN* t = (TOKEN*)FindForCount(i, mst->token)->val;
      free(t->tok);
      free(t);
    }
    DeleteList(mst->token);
  }

  if (mst->root_space != NULL) {
    free_space(mst->root_space);
    DeleteList(mst->root_space->the_space);
    free(mst->root_space);
  }
  free(mst);
#ifdef MEM_LEAK_CHK
  printk("malloc times:%d\nfree times:%d\n", _malloc_times, _free_times);
#endif
}
PUBLIC MST_API const char* MST_strerror(MST_Object* mst) {
  switch (mst->err) {
    case STRING_OP_ERROR:
      return "fatal error: string not closed or operator error";
      break;
    case UNKNOW_TOKEN:
      return "fatal error: An unknown token was detected.";
      break;
    case ERROR_TOKEN:
      return "fatal error: The token contained an error.";
      break;
    case SYNTAX_ERROR:
      return "fatal error: A syntax error has occurred.";
      break;
    default:
      return "(null)";
      break;
  }
}
