#include <arg.h>
#include <mst.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
char *_path;
MST_Object *env;
char *env_read(char *name);
void env_write(char *name, char *val);
void env_init(int m) {
  if (filesize("env.cfg") == -1) {
    if (m == 1) {
      _path = "";
      return;
    }
    mkfile("env.cfg");
    Edit_File("env.cfg", "# created by psh", 16, 0);
  }
  char *buff = (char *)malloc(filesize("env.cfg") + 1);
  api_ReadFile("env.cfg", buff);
  env = MST_init(buff);
  if (env->err) {
    MST_free(env);
    env = NULL;
    sleep(500);
  }
  free(buff);

retry:
  if (!(_path = env_read("path"))) {
    env_write("path", "");
    goto retry;
  }
}
void env_write(char *name, char *val) {
  if (MST_get_var(name, MST_get_root_space(env)) == NULL) {
    MST_add_var_to_space(env, MST_get_root_space(env),
                         MST_var_make_string(name, val));
  } else {
    MST_change_var_for_name(env, MST_var_make_string(name, val), name,
                            MST_get_root_space(env));
  }
}
char *env_read(char *name) {
  if (MST_get_var(name, MST_get_root_space(env)) == NULL) {
    return NULL;
  } else {
    MST_get_string_in_space(env, name, MST_get_root_space(env));
  }
}
void env_save() {
  char path[12];
  sprintf(path, "%c:/env.cfg", api_current_drive());
  if (filesize(path) == -1) {
    return;
  }
  char *s = MST_build_to_string(env);
  Edit_File(path, s, strlen(s), 0);
  free(s);
}
void env_reload() {
  MST_free(env);
  env_init(0);
}
// 首先 我们要解析环境变量的字符串
// 环境变量的字符串是以分号分隔的
void Path_GetPath(int count, char *ptr, char *PATH_ADDR) {
  // count 获取第几个环境变量？
  // ptr   储存在哪里？
  // PATH_ADDR 环境变量的信息在哪里？
  // 我们要解析环境变量的字符串
  int str_base = 0;
  for (int i = 0, j = 0;; i++) {
    if (PATH_ADDR[i] == ';') {
      ++j;
    }
    if (j == count) {
      str_base = i;
      break;
    }
    if (i >= strlen(PATH_ADDR)) {
      // 没找到
      return;
    }
  }
  if (PATH_ADDR[str_base] == ';') {
    str_base++;
  }
  // 找到了
  // copy
  int i;
  for (i = 0; PATH_ADDR[str_base + i] != ';'; i++) {
    ptr[i] = PATH_ADDR[str_base + i];
  }
  ptr[i] = '\0';
}
int Path_GetPathCount(char *PATH_ADDR) {
  int count = 0;
  for (int i = 0; i < strlen(PATH_ADDR); i++) {
    if (PATH_ADDR[i] == ';') {
      count++;
    }
  }
  return count;
}
static void GetFullPath(char *result, char *name, char *dictpath) {
  strcpy(result, dictpath);
  strcat(result, "\\");
  strcat(result, name);
}
bool Path_Find_File(char *fileName, char *PATH_ADDR) {
  char path_result1[255];
  char path_result2[255];
  for (int i = 0; i < Path_GetPathCount(PATH_ADDR); i++) {
    Path_GetPath(i, path_result1, PATH_ADDR);
    GetFullPath(path_result2, fileName, path_result1);
    int size = filesize(path_result2);
    if (size != -1) {
      return true;
    }
  }
  return false;
}
void Path_Find_FileName(char *Result, char *fileName, char *PATH_ADDR) {
  char path_result1[255];
  char path_result2[255];
  for (int i = 0; i < Path_GetPathCount(PATH_ADDR); i++) {
    Path_GetPath(i, path_result1, PATH_ADDR);
    GetFullPath(path_result2, fileName, path_result1);
    int size = filesize(path_result2);
    if (size != -1) {
      strcpy(Result, path_result2);
    }
  }
}
unsigned div_round_up(unsigned num, unsigned size) {
  return (num + size - 1) / size;
}
void dir_deal() {
  struct finfo_block *f = listfile("");
  for (int i = 0; f[i].name[0]; i++) {
    if (f[i].type == DIR) {
      int c = get_cons_color();
      set_cons_color(0x0a);
      printf("%s ", f[i].name);
      set_cons_color(c);
    } else {
      printf("%s ", f[i].name);
    }
  }
  printf("\n");
  free(f);
}
int cmd_app(char *cmdline, int *ok) {
  int result = 0;
  int flag = 0;
  char *s = (char *)malloc(strlen(cmdline) + 10);
  for (int i = 0; i <= strlen(cmdline); i++) {
    s[i] = cmdline[i] == ' ' ? '\0' : cmdline[i];
  }
RETRY:
  if (filesize(s) == -1) {
    if (!Path_Find_File(s, _path)) {
      *ok = 0;
    } else {
      char *s1 = (char *)malloc(strlen(s) + 1024);
      Path_Find_FileName(s1, s, _path);
      result = exec(s1, cmdline);
      free(s1);
      *ok = 1;
    }
  } else {
    result = exec(s, cmdline);
    *ok = 1;
  }
  if (flag == 0 && *ok == 0)
    goto S;
  free(s);
  if (*ok)
    printf("\n");
  return result;
S:
  strcat(s, ".bin");
  flag = 1;
  goto RETRY;
}
int run(char *line) {
  int result = 0;
  if (strlen(line) == 0) {
    return 1;
  }
  if (strcmp("cls", line) == 0) {
    clear();
  } else if (strcmp("dir", line) == 0) {
    dir_deal();
  } else if (strcmp("mem", line) == 0) {
    printf("Used/Total: %u/%u\n", mem_used(),
           div_round_up(mem_total(), 0x1000));
  } else if (strncmp("del ", line, 4) == 0) {
    char *s = (char *)malloc(256);
    get_arg(s, line, 1);
    if (vfs_delfile(s) == 0) {
      printf("File not find.\n");
    }
    free(s);
  } else if (strncmp("cd ", line, 3) == 0) {
    char *s = (char *)malloc(256);
    get_arg(s, line, 1);
    if (vfs_change_path(s) == 0) {
      printf("Invalid path.\n");
    }
    free(s);
  } else if (strncmp("mkfile ", line, 7) == 0) {
    char *s = (char *)malloc(256);
    get_arg(s, line, 1);
    mkfile(s);
    free(s);
  } else if (strncmp("type ", line, 5) == 0) {
    char *s = (char *)malloc(256);
    get_arg(s, line, 1);
    if (filesize(s) == -1) {
      printf("File not find.\n");
      free(s);
      return 1;
    }
    unsigned char *p = (char *)malloc(filesize(s));
    api_ReadFile(s, p);
    for (int i = 0; i != filesize(s); i++) {
      printf("%c", p[i]);
    }
    printf("\n");
    free(s);
    free(p);
  } else if (strcmp("pause", line) == 0) {
    printf("Pree any key to continue. . .");
    getch();
    printf("\n");
  } else if (strncmp("rdrv ", line, 5) == 0) {
    if (!vfs_check_mount(line[5])) {
      if (!vfs_mount(line[5], line[5])) {
        printf("Disk not ready!\n");
      } else {
        vfs_change_disk(line[5]);
      }
    } else {
      vfs_unmount_disk(line[5]);
      if (!vfs_mount(line[5], line[5])) {
        printf("Disk not ready!\n");
      } else {
        vfs_change_disk(line[5]);
      }
    }
  } else if (strncmp("color ", line, 6) == 0) {
    int c = strtol(line + 6, NULL, 16);
    T_DrawBox(0, 0, tty_get_xsize(), tty_get_ysize(), c);
    set_cons_color(c);
  } else if (strncmp("mkdir ", line, 6) == 0) {
    if (!mkdir(line + 6)) {
      printf("Unable to create %s\n", line);
    }
  } else if (strncmp("format ", line, 7) == 0) {
    if (get_argc(line) != 3) {
      printf("format <drive> <fsname>\nfsname can be FAT and PFS\n");
      return 1;
    }
    char *s = (char *)malloc(256);
    char *s1 = (char *)malloc(256);
    get_arg(s, line, 1);
    get_arg(s1, line, 2);
    format(s[0], s1);
    free(s);
    free(s1);
  } else if (line[1] == ':' && line[2] == '\0') {
    if (!vfs_check_mount(line[0])) {
      if (!vfs_mount(line[0], line[0])) {
        printf("disk not ready!\n");
      } else {
        vfs_change_disk(line[0]);
      }
    } else {
      vfs_change_disk(line[0]);
    }
  } else {
    int ok;
    result = cmd_app(line, &ok);
    if (ok == 0) {
      printf("bad command!\n");
      return 1;
    }
  }
  return result;
}
void shell() {
  printf("Plant OS 0.8a\n");
  for (;;) {
    char cwd[255];
    api_getcwd(cwd);
    printf("psh|%s ~ ", cwd);
    char buf[500];
    scan(buf, 500);
    run(buf);
  }
}
int main(int argc, char **argv) {
  if (argc > 1) {
    if (argc != 3) {
      printf("fatal error.\n");
      return 1;
    }
    env_init(1);
    return run(argv[2]);
  }
  env_init(0);
  shell();
}
