#include <mst.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#define Line_X 205
#define Line_Y 186
#define Left_Up 201
#define Left_Down 200
#define Right_Up 187
#define Right_Down 188
#define T_DrawBox(x, y, w, h, c) Text_Draw_Box((y), (x), (h) + y, (w) + x, (c))

static void Box(int x, int y, int w, int h) {
  goto_xy(x, y);
  putch(Left_Up);
  w--;
  h--;
  for (int i = 0; i < w; i++) {
    putch(Line_X);
  }
  goto_xy(x, y + 1);
  for (int i = 0; i < h; i++) {
    goto_xy(x, y + 1 + i);
    putch(Line_Y);
  }
  goto_xy(x, y + 1 + h - 1);
  putch(Left_Down);
  for (int i = 0; i < w; i++) {
    putch(Line_X);
  }
  goto_xy(x + w, y);
  putch(Right_Up);
  for (int i = 0; i < h; i++) {
    goto_xy(x + w, y + 1 + i);
    putch(Line_Y);
  }
  goto_xy(x + w, y + 1 + h - 1);
  putch(Right_Down);
}
static void Set_Loading(int percentage) {
  T_DrawBox(6, 19, 67, 1, 0x1f);
  float p = ((float)percentage / 100.0) * 67;
  int percent = (int)p;
  T_DrawBox(6, 19, percent, 1, 0x5f);
}
static void putSpace(int x, int y, int w, int h) {
  goto_xy(x, y);
  for (int i = 0; i < h; i++) {
    goto_xy(x, y + i);
    for (int j = 0; j < w; j++) {
      putch(' ');
    }
  }
}
static int OKCancelMsg(char *msg) {
  putSpace(40 - (strlen(msg) + 15) / 2, 4, strlen(msg) + 20, 8);
  Box(40 - (strlen(msg) + 15) / 2, 4, strlen(msg) + 15, 8);
  goto_xy(40 - (strlen(msg) + 15) / 2 + 1, 4);
  print("Message");
  goto_xy(40 - (strlen(msg) + 15) / 2 + 1, 6);
  print(msg);
  goto_xy(40 - (strlen(msg) + 15) / 2 + 8, 10);
  print("OK");
  goto_xy(40 - (strlen(msg) + 15) / 2 + 15, 10);
  print("CANCEL");
  T_DrawBox(40 - (strlen(msg) + 15) / 2 + 6, 10, 6, 1, 0x4f);
  T_DrawBox(40 - (strlen(msg) + 15) / 2 + 13, 10, 10, 1, 0x0f);
  unsigned int c = 1;
  int res = 1;
  while (c != '\n') {
    c = getch();
    switch (c) {
    case -3:
      T_DrawBox(40 - (strlen(msg) + 15) / 2 + 6, 10, 6, 1, 0x4f);
      T_DrawBox(40 - (strlen(msg) + 15) / 2 + 13, 10, 10, 1, 0x0f);
      res = 1;
      break;
    case -4:
      res = 0;
      T_DrawBox(40 - (strlen(msg) + 15) / 2 + 6, 10, 6, 1, 0x0f);
      T_DrawBox(40 - (strlen(msg) + 15) / 2 + 13, 10, 10, 1, 0x4f);
      break;
    default:
      break;
    }
  }
  putSpace(40 - (strlen(msg) + 15) / 2, 4, strlen(msg) + 15, 8);
  return res;
}
static int OKMsg(char *msg) {
  putSpace(40 - (strlen(msg) + 15) / 2, 4, strlen(msg) + 15, 8);
  Box(40 - (strlen(msg) + 15) / 2, 4, strlen(msg) + 15, 8);
  goto_xy(40 - (strlen(msg) + 15) / 2 + 1, 4);
  print("Message");
  goto_xy(40 - (strlen(msg) + 15) / 2 + 1, 6);
  print(msg);
  goto_xy(40 - (strlen(msg) + 15) / 2 + 8, 10);
  print("OK");

  T_DrawBox(40 - (strlen(msg) + 15) / 2 + 6, 10, 6, 1, 0x4f);
  // T_DrawBox(40 - (strlen(msg) + 15) / 2 + 13, 10, 10, 1, 0x0f);
  unsigned int c = 1;
  int res = 1;
  while (c != '\n') {
    c = getch();
  }
  putSpace(40 - (strlen(msg) + 15) / 2, 4, strlen(msg) + 15, 8);
  return res;
}
static void setState(char *msg) {
  putSpace(0, 24, 80, 1);
  goto_xy(0, 24);
  print(msg);
  T_DrawBox(0, 24, 80, 1, 0x70);
}
static int get_array_len(Array *arr) {
  if (arr == NULL) {
    return -1;
  }
  int r = 0;
  for (; MST_array_get_data(arr, r); r++)
    ;
  return r;
}
static void set(int current, int total) {
  if (total <= 0) {
    Set_Loading(100);
    return;
  }
  Set_Loading((int)((float)((float)(current) / (float)total) * 100.0));
}
static bool copy_manifest(Array *files, MST_Object *m, char source_drive,
                          const char *filesystem) {
  int files_in_total = get_array_len(files);
  if (files_in_total < 0) {
    return false;
  }
  SPACE *loader = MST_get_space_in_array(m, 0, files);
  char *loader_path = loader == NULL
                          ? NULL
                          : MST_get_string_in_space(m, "path", loader);
  if (loader_path == NULL || strcmp(loader_path, "DOSLDR.bin") != 0) {
    return false;
  }
  bool fat = strcmp(filesystem, "FAT") == 0;
  Set_Loading(0);
  for (int i = 0; i < files_in_total; i++) {
    SPACE *entry = MST_get_space_in_array(m, i, files);
    char *kind =
        entry == NULL ? NULL : MST_get_string_in_space(m, "type", entry);
    char *path =
        entry == NULL ? NULL : MST_get_string_in_space(m, "path", entry);
    char *fat_path =
        entry == NULL ? NULL : MST_get_string_in_space(m, "fat", entry);
    char *destination_name = fat ? fat_path : path;
    if (kind == NULL || path == NULL || fat_path == NULL ||
        destination_name == NULL) {
      return false;
    }
    setState(path);
    if (strcmp(kind, "dir") == 0) {
      if (mkdir(destination_name) != 0) {
        return false;
      }
      set(i + 1, files_in_total);
      continue;
    }
    if (strcmp(kind, "file") != 0) {
      return false;
    }
    char *source_name = MST_get_string_in_space(m, "source", entry);
    if (source_name == NULL) {
      return false;
    }
    size_t source_length = strlen(source_name);
    size_t destination_length = strlen(destination_name);
    if (source_length > (size_t)UINT_MAX - 4 ||
        destination_length > (size_t)UINT_MAX - 4) {
      return false;
    }
    char *source = malloc(source_length + 4);
    char *destination = malloc(destination_length + 4);
    if (source == NULL || destination == NULL) {
      free(source);
      free(destination);
      return false;
    }
    sprintf(source, "%c:\\%s", source_drive, source_name);
    sprintf(destination, "C:\\%s", destination_name);
    bool copied = Copy(source, destination) == 0;
    free(source);
    free(destination);
    if (!copied) {
      return false;
    }
    set(i + 1, files_in_total);
  }
  return true;
}
int main() {
  char source_drive = toupper(api_current_drive());
  if (source_drive < 'A' || source_drive > 'Z') {
    return 1;
  }
  struct stat status;
  int len = stat("setup.mst", &status) == 0 ? (int)status.st_size : -1;
  if (len < 0 || len == INT_MAX) {
    int c = get_cons_color();
    set_cons_color(0x0c);
    printf("Can't find setup.mst.\n");
    set_cons_color(c);
    return 1;
  }
  char *config = (char *)malloc(len + 1);
  FILE *stream = fopen("setup.mst", "rb");
  if (config == NULL || stream == NULL ||
      (len != 0 && fread(config, 1, len, stream) != (size_t)len)) {
    if (stream != NULL) {
      fclose(stream);
    }
    free(config);
    printf("Unable to read setup.mst.\n");
    return 1;
  }
  fclose(stream);
  config[len] = 0;
  MST_Object *m = MST_init(config);
  if (m == NULL || m->err) {
    int c = get_cons_color();
    set_cons_color(0x0c);
    printf("Parse error:%s\n", m == NULL ? "out of memory" : MST_strerror(m));
    set_cons_color(c);
    if (m != NULL) {
      MST_free(m);
    }
    free(config);
    return 1;
  }
  SPACE *root = MST_get_root_space(m);
  Var *files_var = root == NULL ? NULL : MST_get_var("files", root);
  Array *files = files_var == NULL ? NULL : MST_space_get_array(files_var);
  if (files == NULL) {
    printf("Invalid setup manifest.\n");
    MST_free(m);
    free(config);
    return 1;
  }
  if (system("cls") != 0 || system("color 1f") != 0) {
    MST_free(m);
    free(config);
    return 1;
  }
  Box(0, 0, 80, 24);
  T_DrawBox(0, 24, 80, 1, 0x70);
  goto_xy(26, 0);
  print("Powerint DOS 386 Installation");
  T_DrawBox(26, 0, 29, 1, 0x4f);
  setState("Welcome to Powerint DOS 386 Installation");
  goto_xy(5, 17);
  print("Installing...");
  Box(5, 18, 69, 3);
  if (!OKCancelMsg("Do you want to Install Powerint DOS?")) {
    system("color 07");
    system("cls");
    MST_free(m);
    free(config);
    return 0;
  }
  Set_Loading(0);
  setState("Install: Format Disk");
  Box(34, 4, 11, 5);
  goto_xy(35, 5);
  print("Choose fs");
  goto_xy(35, 6);
  print("FAT");
  goto_xy(35, 7);
  print("PFS");
  T_DrawBox(35, 6, 9, 1, 0x4f);
  T_DrawBox(35, 7, 9, 1, 0x0f);
  char fs_choice[4] = "FAT";
  for (;;) {
    int key = getch();
    if (key == '\n') {
      break;
    }
    if (key == -1) {
      T_DrawBox(35, 6, 9, 1, 0x4f);
      T_DrawBox(35, 7, 9, 1, 0x0f);
      strcpy(fs_choice, "FAT");
    } else if (key == -2) {
      T_DrawBox(35, 6, 9, 1, 0x0f);
      T_DrawBox(35, 7, 9, 1, 0x4f);
      strcpy(fs_choice, "PFS");
    }
  }
  putSpace(34, 4, 11, 5);
  T_DrawBox(34, 4, 11, 5, 0x1f);
  bool target_was_mounted = vfs_check_mount('C');
  if (toupper(api_current_drive()) == 'C') {
    OKMsg("The target disk is in use.");
    goto fail;
  }
  if (format('C', fs_choice) != 0) {
    if (target_was_mounted && !vfs_check_mount('C')) {
      vfs_mount('C', 'C');
    }
    OKMsg("Disk Read Error.");
    goto fail;
  }
  if (!vfs_mount('C', 'C') || !vfs_change_disk('C')) {
    OKMsg("Unable to mount formatted disk.");
    goto fail;
  }
  Set_Loading(100);
  if (!copy_manifest(files, m, source_drive, fs_choice)) {
    goto fail;
  }
  Set_Loading(0);
  setState("Config --- Create env.cfg");
  FILE *config_file = fopen("env.cfg", "wb");
  if (config_file == NULL) {
    goto fail;
  }
  Set_Loading(25);
  setState("Config --- Write env.cfg");
  static char env_config[] = "\"path\" = \"C:;C:\\bin;\"";
  if (fwrite(env_config, 1, sizeof(env_config) - 1, config_file) !=
          sizeof(env_config) - 1 ||
      fclose(config_file) != 0) {
    goto fail;
  }
  Set_Loading(50);
  setState("Config --- Create sys.cfg");
  config_file = fopen("sys.cfg", "wb");
  if (config_file == NULL) {
    goto fail;
  }
  Set_Loading(75);
  setState("Config --- Write sys.cfg");
  static char system_config[] =
      "\"network\" = \"enable\"\n\"video_mode\" = \"TEXTMODE\"";
  if (fwrite(system_config, 1, sizeof(system_config) - 1, config_file) !=
          sizeof(system_config) - 1 ||
      fclose(config_file) != 0) {
    goto fail;
  }
  Set_Loading(100);
  OKMsg("Press Enter to Reboot Your computer.");
  MST_free(m);
  free(config);
  if (system("reboot") != 0) {
    return 1;
  }
  for (;;)
    ;

fail:
  if (!vfs_check_mount(source_drive)) {
    vfs_mount(source_drive, source_drive);
  }
  vfs_change_disk(source_drive);
  system("color 07");
  system("cls");
  MST_free(m);
  free(config);
  return 1;
}
