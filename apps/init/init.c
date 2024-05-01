// Plant OS 默认init程序
// Copyright (C) 2024 min0911

#include <mst.h>
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
  if (filesize("init.mst") == -1) {
    set_cons_color(0x0c);
    printf("ERROR!!! Couldn't find the file \"init.mst\"!");
    for (;;)
      ;
  }
  unsigned int size = filesize("init.mst");
  char *buffer = (char *)malloc(size + 1);
  api_ReadFile("init.mst", buffer);
  buffer[size] = 0;
  MST_Object *m = MST_init(buffer);
  Var *v = MST_get_var("todo", MST_get_root_space(m));
  if (!v) {
    set_cons_color(0x0c);
    printf("ERROR!!! necessary option \"todo\" is missing\n");
    for (;;)
      ;
  }
  Array *a = MST_space_get_array(v);
  if (!v) {
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
      convert(s3);
      print(s3);
      free(s3);

    } else if (strcmp(s1, "run") == 0) {
      Var *v2 = MST_get_var("path", s);
      if (!v2) {
        set_cons_color(0x0c);
        printf("ERROR!!! necessary option \"path\" is missing in todo[%d]\n",
               i);
        for (;;)
          ;
      }
      char *s2 = MST_space_get_str(v2);
      if (!s2) {
        set_cons_color(0x0c);
        printf("ERROR!!! todo[%d].path is not a string\n", i);
        for (;;)
          ;
      }
      char *s3 = strdup(s2);
      for (int i = 0; i < strlen(s3); i++) {
        if (s3[i] == ' ') {
          s3[i] = '\0';
					break;
				}
      }
      exec(s3, s2);
			free(s3);
	  }
  }
  for (;;)
    ;
  return 0;
}