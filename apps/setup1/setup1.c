#include <mst.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#define All_Kernel_files_count 10
#define All_App_files_count 30
#define All_Res_files_count 21
#define Line_X 205
#define Line_Y 186
#define Left_Up 201
#define Left_Down 200
#define Right_Up 187
#define Right_Down 188
#define T_DrawBox(x, y, w, h, c) Text_Draw_Box((y), (x), (h) + y, (w) + x, (c))

static bool copy_file(char *source, char *destination) {
  int size = filesize(source);
  if (size < 0) {
    return false;
  }
  char *buffer = size == 0 ? NULL : malloc((size_t)size);
  if (size != 0 && buffer == NULL) {
    return false;
  }
  if ((size != 0 && !api_readfile(source, buffer)) ||
      !Edit_File(destination, buffer, size, 0)) {
    free(buffer);
    return false;
  }
  free(buffer);
  return true;
}

void Box(int x, int y, int w, int h) {
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
void Set_Loading(int percentage) {
  T_DrawBox(6, 19, 67, 1, 0x1f);
  float p = ((float)percentage / 100.0) * 67;
  int percent = (int)p;
  T_DrawBox(6, 19, percent, 1, 0x5f);
}
void putSpace(int x, int y, int w, int h) {
  goto_xy(x, y);
  for (int i = 0; i < h; i++) {
    goto_xy(x, y + i);
    for (int j = 0; j < w; j++) {
      putch(' ');
    }
  }
}
int OKCancelMsg(char *msg) {
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
int OKMsg(char *msg) {
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
void setState(char *msg) {
  putSpace(0, 24, 80, 1);
  goto_xy(0, 24);
  print(msg);
  T_DrawBox(0, 24, 80, 1, 0x70);
}
int get_array_len(Array *arr) {
  if (arr == NULL) {
    return -1;
  }
  int r = 0;
  for (; MST_array_get_data(arr, r); r++)
    ;
  return r;
}
void set(int current, int total) {
  if (total <= 0) {
    Set_Loading(100);
    return;
  }
  Set_Loading((int)((float)((float)(current) / (float)total) * 100.0));
}
enum step_result { STEP_ERROR = -1, STEP_DONE, STEP_CONTINUE };
static enum step_result run_next_step(SPACE *next, MST_Object *m) {
  if (next == NULL || m == NULL) {
    return STEP_ERROR;
  }
  char *state = MST_get_string_in_space(m, "state", next);
  char *info = MST_get_string_in_space(m, "info", next);
  char *disk_name = MST_get_string_in_space(m, "disk_name", next);
  if (info == NULL || disk_name == NULL) {
    return STEP_ERROR;
  }
  if (state != NULL) {
    setState(state);
  }
  if (OKCancelMsg(info)) {
    Box(0, 0, 80, 24);
    goto_xy(26, 0);
    print("Powerint DOS 386 Installation");
    T_DrawBox(26, 0, 29, 1, 0x4f);
    size_t disk_name_length = strlen(disk_name);
    if (disk_name_length > (size_t)UINT_MAX - sizeof("Please insert ")) {
      return STEP_ERROR;
    }
    char *tip = malloc(sizeof("Please insert ") + disk_name_length);
    if (tip == NULL) {
      return STEP_ERROR;
    }
    sprintf(tip, "Please insert %s", disk_name);
    OKMsg(tip);
    free(tip);
    if (system("remount_drive A:") != 0 || system("C:") != 0) {
      return STEP_ERROR;
    }
    return STEP_CONTINUE;
  }
  return STEP_DONE;
}

static enum step_result run_step(SPACE *step, MST_Object *m) {
  if (step == NULL || m == NULL) {
    return STEP_ERROR;
  }
  Var *files = MST_get_var("files", step);
  Array *files_arr = files == NULL ? NULL : MST_space_get_array(files);
  int files_in_total = get_array_len(files_arr);
  if (files_in_total < 0) {
    return STEP_ERROR;
  }
  Set_Loading(0);
  for (int i = 0; i < files_in_total; i++) {
    char *entry = MST_get_string_in_array(m, i, files_arr);
    if (entry == NULL) {
      SPACE *directory = MST_get_space_in_array(m, i, files_arr);
      char *name = directory == NULL
                       ? NULL
                       : MST_get_string_in_space(m, "dir", directory);
      if (name == NULL || !mkdir(name)) {
        return STEP_ERROR;
      }
      continue;
    }
    setState(entry);
    size_t entry_length = strlen(entry);
    if (entry_length > (size_t)UINT_MAX - 4) {
      return STEP_ERROR;
    }
    char *source = malloc(entry_length + 4);
    char *destination = malloc(entry_length + 4);
    if (source == NULL || destination == NULL) {
      free(source);
      free(destination);
      return STEP_ERROR;
    }
    sprintf(source, "A:\\%s", entry);
    sprintf(destination, "C:\\%s", entry);
    bool copied = copy_file(source, destination);
    free(source);
    free(destination);
    if (!copied) {
      return STEP_ERROR;
    }
    set(i + 1, files_in_total);
  }
  if (!MST_get_var("next", step)) {
    return STEP_DONE;
  }
  return run_next_step(MST_get_space_in_space(m, "next", step), m);
}
int main() {
  char source_drive = toupper(api_current_drive());
  if (source_drive < 'A' || source_drive > 'Z') {
    return 1;
  }
  int len = filesize("setup.mst");
  if (len < 0 || len == INT_MAX) {
    int c = get_cons_color();
    set_cons_color(0x0c);
    printf("Can't find setup.mst.\n");
    set_cons_color(c);
    return 1;
  }
  char *config = (char *)malloc(len + 1);
  if (config == NULL || (len != 0 && !api_readfile("setup.mst", config))) {
    free(config);
    printf("Unable to read setup.mst.\n");
    return 1;
  }
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
    int i = getch();
    if (i == '\n') {
      break;
    } else if (i == -1) {
      T_DrawBox(35, 6, 9, 1, 0x4f);
      T_DrawBox(35, 7, 9, 1, 0x0f);
      strcpy(fs_choice, "FAT");
    } else if (i == -2) {
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
  if (!format('C', fs_choice)) {
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
  SPACE *root = MST_get_root_space(m);
  Var *step_var = root == NULL ? NULL : MST_get_var("step", root);
  Array *step = step_var == NULL ? NULL : MST_space_get_array(step_var);
  int all_steps = get_array_len(step);
  if (all_steps < 0) {
    goto fail;
  }
  for (int i = 0; i < all_steps; i++) {
    enum step_result step_result =
        run_step(MST_get_space_in_array(m, i, step), m);
    if (step_result == STEP_ERROR) {
      goto fail;
    }
    if (step_result == STEP_DONE) {
      break;
    }
  }
  Set_Loading(0);
  setState("Config --- Create env.cfg");
  if (!mkfile("env.cfg")) {
    goto fail;
  }
  Set_Loading(25);
  setState("Config --- Write env.cfg");
  static char env_config[] = "\"path\" = \"C:\\bin;C:;\"";
  if (!Edit_File("env.cfg", env_config, sizeof(env_config) - 1, 0)) {
    goto fail;
  }
  Set_Loading(50);
  setState("Config --- Create sys.cfg");
  if (!mkfile("sys.cfg")) {
    goto fail;
  }
  Set_Loading(75);
  setState("Config --- Write sys.cfg");
  static char system_config[] =
      "\"network\" = \"enable\"\n\"video_mode\" = \"HIGHTEXTMODE\"";
  if (!Edit_File("sys.cfg", system_config, sizeof(system_config) - 1, 0)) {
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
