#include <mst.h>
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

void cpy(char *filename,char *filename1) {
  if(filesize(filename) == -1) return;
  int sz = filesize(filename);
  char *buffer = malloc(sz);
  api_ReadFile(filename,buffer);
  mkfile(filename1);
  Edit_File(filename1,buffer,sz,0);
  free(buffer);
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
  int r = 0;
  for (; MST_array_get_data(arr, r); r++)
    ;
  return r;
}
void set(int current, int total) {
  Set_Loading((int)((float)((float)(current) / (float)total) * 100.0));
}
int do_next(SPACE *next, MST_Object *m) {
  if (MST_get_var("state", next)) {
    setState(MST_get_string_in_space(m, "state", next));
  }
  if (OKCancelMsg(MST_get_string_in_space(m, "info", next))) {
    Box(0, 0, 80, 24);
    goto_xy(26, 0);
    print("Powerint DOS 386 Installation");
    T_DrawBox(26, 0, 29, 1, 0x4f);
    char tip[512];
    sprintf(tip,"Please insert %s",MST_get_string_in_space(m, "disk_name", next));
    OKMsg(tip);
    system("rdrv A");
    system("C:");
    return 1;
  }
  return 0;
}

int do_step(SPACE *step, MST_Object *m) {
  Array *files_arr = MST_space_get_array(MST_get_var("files", step));
  int files_in_total = get_array_len(files_arr);
  Set_Loading(0);
  for (int i = 0; i < files_in_total; i++) {
    char *b = MST_get_string_in_array(m, i, files_arr);
    if(b == 0x0) {
      SPACE *d = MST_get_space_in_array(m, i, files_arr);
      mkdir(MST_get_string_in_space(m,"dir",d));
      continue;
    }
    setState(MST_get_string_in_array(m, i, files_arr));
    char *path1[255];
    char *path2[255];
    sprintf(path1, "A:\\%s", MST_get_string_in_array(m, i, files_arr));
    sprintf(path2, "C:\\%s", MST_get_string_in_array(m, i, files_arr));
    cpy(path1, path2);
    set(i + 1, files_in_total);
  }
  if (!MST_get_var("next", step)) {
    return 0;
  }
  return do_next(MST_get_space_in_space(m, "next", step), m);
}
int main() {
  int len = filesize("setup.mst");
  if (len == -1) {
    int c = get_cons_color();
    set_cons_color(0x0c);
    printf("Can't find setup.mst.\n");
    set_cons_color(c);
    return 1;
  }
  char *config = (char *)malloc(len + 1);
  api_ReadFile("setup.mst", config);
  config[len] = 0;
  MST_Object *m = MST_init(config);
  if (m->err) {
    int c = get_cons_color();
    set_cons_color(0x0c);
    printf("Parse error:%s\n", MST_strerror(m));
    set_cons_color(c);
    MST_free(m);
    return 1;
  }
  system("cls");
  system("color 1f");
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
  char *fs_choice = malloc(4);
  strcpy(fs_choice, "FAT");
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
  if (!format('C', fs_choice)) {
    OKMsg("Disk Read Error.");
    system("color 07");
    system("cls");
    return 0;
  }
  system("rdrv C:");
  Set_Loading(100);
  int CopyFilesCount = 0;
  Array *step = MST_space_get_array(MST_get_var("step", MST_get_root_space(m)));
  int all_steps = get_array_len(step);
  for (int i = 0; i < all_steps; i++) {
    if(!do_step(MST_get_space_in_array(m,i,step),m)) {
      break;
    }
  }
  Set_Loading(0);
  setState("Config --- Create env.cfg");
  mkfile("env.cfg");
  Set_Loading(25);
  setState("Config --- Write env.cfg");
  Edit_File("env.cfg", "\"path\" = \"C:\\bin;C:;\"", 22, 0);
  Set_Loading(50);
  setState("Config --- Create sys.cfg");
  mkfile("sys.cfg");
  Set_Loading(75);
  setState("Config --- Write sys.cfg");
  Edit_File("sys.cfg", "\"network\" = \"enable\"\n\"video_mode\" = \"HIGHTEXTMODE\"", 52, 0);
  Set_Loading(100);
  OKMsg("Press Enter to Reboot Your computer.");
  system("reboot");
  for (;;)
    ;
}
