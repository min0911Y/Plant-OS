// Plant OS 默认init程序
// Copyright (C) 2024 min0911

#include <mst.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
void convert(char *str) {
  int i, j;
  for (i = 0, j = 0; str[i] != '\0'; i++, j++) {
    if (str[i] == '\\') {
      i++;
      switch (str[i]) {
      case 'n':
        str[j] = '\n';
        break;
      case 't':
        str[j] = '\t';
        break;
      case 'r':
        str[j] = '\r';
        break;
      case '\"':
        str[j] = '\"';
        break;
      case '\\':
        str[j] = '\\';
        break;
      default:
        str[j] = str[i];
      }
    } else
      str[j] = str[i];
  }
  str[j] = '\0';
}
int main() {
  logk("init.bin started\n");
  int file_size = filesize("init.mst");
  if (file_size < 0 || file_size == INT_MAX) {
    set_cons_color(0x0c);
    printf("ERROR!!! Couldn't find the file \"init.mst\"!");
    for (;;)
      ;
  }
  char *buffer = (char *)malloc((size_t)file_size + 1);
  if (buffer == NULL ||
      (file_size != 0 && !api_readfile("init.mst", buffer))) {
    logk("init: unable to read init.mst\n");
    free(buffer);
    return 1;
  }
  buffer[file_size] = 0;
  MST_Object *m = MST_init(buffer);
  if (m == NULL || m->err) {
    logk("init: unable to parse init.mst\n");
    if (m != NULL) {
      MST_free(m);
    }
    free(buffer);
    return 1;
  }
  Var *v = MST_get_var("todo", MST_get_root_space(m));
  if (!v) {
    set_cons_color(0x0c);
    printf("ERROR!!! necessary option \"todo\" is missing\n");
    for (;;)
      ;
  }
  Array *a = MST_space_get_array(v);
  if (!a) {
    set_cons_color(0x0c);
    printf("ERROR!!! the type of \"todo\" is wrong, it should be array\n");
    for (;;)
      ;
  }
  for (int i = 0;; i++) {
    Array_data *ad;
    ad = MST_array_get_data(a, i);
    if (!ad) {
      break;
    }
    SPACE *s;
    s = MST_array_get_space(ad);
    if (!s) {
      set_cons_color(0x0c);
      printf("ERROR!!! the type of action is wrong, it should be space\n");
      for (;;)
        ;
    }
    Var *v1 = MST_get_var("action", s);
    if (!v1) {
      set_cons_color(0x0c);
      printf("ERROR!!! necessary option \"action\" is missing in todo[%d]\n",
             i);
      for (;;)
        ;
    }
    char *s1 = MST_space_get_str(v1);
    if (!s1) {
      set_cons_color(0x0c);
      printf("ERROR!!! todo[%d].action is not a string\n", i);
      for (;;)
        ;
    }
    if (strcmp(s1, "output") == 0) {
      Var *v2 = MST_get_var("out", s);
      if (!v2) {
        set_cons_color(0x0c);
        printf("ERROR!!! necessary option \"out\" is missing in todo[%d]\n", i);
        for (;;)
          ;
      }
      char *s2 = MST_space_get_str(v2);
      if (!s2) {
        set_cons_color(0x0c);
        printf("ERROR!!! todo[%d].out is not a string\n", i);
        for (;;)
          ;
      }
      char *s3 = strdup(s2);
      if (s3 == NULL) {
        logk("init: unable to duplicate output action\n");
        break;
      }
      convert(s3);
      print(s3);
      free(s3);

    } else if (strcmp(s1, "run") == 0) {
      Var *v2 = MST_get_var("command_line", s);
      if (!v2) {
        set_cons_color(0x0c);
        printf("ERROR!!! necessary option \"command_line\" is missing in todo[%d]\n",
               i);
        for (;;)
          ;
      }
      char *s2 = MST_space_get_str(v2);
      if (!s2) {
        set_cons_color(0x0c);
        printf("ERROR!!! todo[%d].command_line is not a string\n", i);
        for (;;)
          ;
      }
      char *s3 = strdup(s2);
      if (s3 == NULL) {
        logk("init: unable to duplicate command line\n");
        break;
      }
      for (size_t i = 0; s3[i] != '\0'; i++) {
        if (s3[i] == ' ') {
          s3[i] = '\0';
					break;
				}
      }
      logkf("init: run %s\n", s2);
      int status = exec(s3, s2);
      logkf("init: command %s status=%d\n", s2, status);
      free(s3);
    }
  }
  logk("init: todo complete\n");
  MST_free(m);
  free(buffer);
  for (;;)
    ;
  return 0;
}
