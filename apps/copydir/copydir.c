#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

void do_copy(char *dir, char *current_path) {
  char command[255];
  sprintf(command, "cd %s", dir);
  system(command);
  struct finfo_block *f = listfile("");

  for (int i = 0; f[i].name[0]; i++) {
    if (strcmp(f[i].name, ".") == 0 || strcmp(f[i].name, "..") == 0) {
      continue;
    }
    if (strcmp(f[i].name, "NULL") == 0) {
      continue;
    }
    if (f[i].type == DIR) {
      strcat(current_path, f[i].name);
      sprintf(command, "mkdir %s", current_path);
      system(command);
      strcat(current_path, "\\");
      do_copy(f[i].name, current_path);
    } else {
      printf("copy %s to %s%s\n", f[i].name, current_path, f[i].name);
      sprintf(command, "%s%s", current_path, f[i].name);
      Copy(f[i].name, command);
    }
  }
  free(f);
  system("cd ..");
  for (int i = strlen(current_path) - 2; i >= 0; i--) {
    if (current_path[i] == '\\') {
      current_path[i + 1] = '\0';
      break;
    }
  }
}
int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <Dest drive> <dir>\n", argv[0]);
    return 1;
  }
  char command[255];
  char cur = api_current_drive();
  sprintf(command, "rdrv %c", argv[1][0]);
  system(command);
  sprintf(command, "mkdir %s", argv[2]);
  system(command);
  sprintf(command, "%c:", cur);
  system(command);
  char current_path[255];
  sprintf(current_path, "%c:\\%s\\", argv[1][0], argv[2]);
  printf("Current path: %s\n", current_path);
  do_copy(argv[2], current_path);
}