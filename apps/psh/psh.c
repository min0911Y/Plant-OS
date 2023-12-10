#include <stdio.h>
#include <string.h>
#include <syscall.h>
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
int cmd_app(char *cmdline) {
  char *s = (char *)malloc(strlen(cmdline) + 1);
  for (int i = 0; i <= strlen(cmdline); i++) {
    s[i] = cmdline[i] == ' ' ? '\0' : cmdline[i];
  }
  if (filesize(s) == -1) {
    free(s);
    return 0;
  }
  exec(s, cmdline);
  free(s);
  printf("\n");
  return 1;
}
void run(char *line) {
  if (strlen(line) == 0) {
    return;
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
  } else if (strncmp("type ", line, 5) == 0) {
    char *s = (char *)malloc(256);
    get_arg(s, line, 1);
    if (filesize(s) == -1) {
      printf("File not find.\n");
      free(s);
      return;
    }
    char *p = (char *)malloc(filesize(s));
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
  } else if (strncmp("color ",line,6) == 0) {
    int c = strtol(line+6,NULL,16);
    T_DrawBox(0,0,tty_get_xsize(),tty_get_ysize(),c);
    set_cons_color(c);
  } else if (strncmp("mkdir ",line,6) == 0) {
    if(!mkdir(line+6)) {
      printf("Unable to create %s\n",line);
    }
  }
  else if (line[1] == ':' && line[2] == '\0') {
    if (!vfs_check_mount(line[0])) {
      if (!vfs_mount(line[0], line[0])) {
        printf("disk not ready!\n");
      } else {
        vfs_change_disk(line[0]);
      }
    } else {
      vfs_change_disk(line[0]);
    }
  } else if (!cmd_app(line)) {
    printf("bad command!\n");
  }
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
    run(argv[2]);
    return 0;
  }
  shell();
}