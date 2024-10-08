// Powerint DOS 386系统调用
// @ Copyright (C) 2022
// @ Author: zhouzhihao & min0911_
#ifndef SYS_CALL_H
#define SYS_CALL_H
#ifdef __cplusplus
extern "C" {
#endif
#include <ctypes.h>
#define T_DrawBox(x, y, w, h, c) Text_Draw_Box((y), (x), (h) + y, (w) + x, (c))
typedef enum { FLE, DIR, RDO, HID, SYS } ftype;
struct finfo_block {
  char name[255];
  ftype type;
  unsigned int size;
  unsigned short year, month, day;
  unsigned short hour, minute;
};
void putch(char ch);
unsigned int getch();
char input_char_inSM();
int get_xy();
void goto_xy(int x, int y);
void SwitchTo320X200X256();
void SwitchToText8025();
int get_mouse();
void Draw_Char(int x, int y, char ch, int color);
void Draw_Str(int x, int y, char *str, int color);
void sleep(int time);
void PrintChineseChar(int x, int y, int color, unsigned short cChar);
void PrintChineseStr(int x, int y, int color, unsigned char *cStr);
void print(char *str);
void api_free(void *ptr, int size);
int system(char *command);
int filesize(char *filename);
int api_ReadFile(char *filename, char *res);
void bmpview(char *filename);
void Draw_Box(int x, int y, int w, int h, int color);
void Draw_Px(int x, int y, int color);
void Text_Draw_Box(int y, int x, int h, int w, int color);
void api_beep(int point, int notes, int dup);
void GetCmdline(char *_Str);
int Get_System_Version();
int Copy(char *filePath1, char *filePath2);
int _kbhit();
int mkfile(char *filename);
int mkdir(char *filename);
void Edit_File(char *filename, char *dest, int len, int offset);
struct finfo_block *listfile(char *path);
void SwitchTo320X200X256_BIOS();
void SwitchToText8025_BIOS();
void TaskForever();
void SendMessage(int to_tid, void *data, unsigned int size);
void GetMessage(void *data, int from_tid);
unsigned int MessageLength(int from_tid);
int NowTaskID();
void _exit(unsigned _status);
void timer_alloc();
void timer_settime(unsigned int time);
int timer_out();
void timer_free();
int haveMsg();
void GetMessageAll(void *data);
char PhyMemGetByte(int addr);
void PhyMemSetByte(int addr, char data);
int format(unsigned drive, char *fs_name);
void *malloc(int size);
void free(void *p);
void *realloc(void *ptr, uint32_t size);
int get_hour_hex();
int get_min_hex();
int get_sec_hex();
int get_day_of_month();
int get_day_of_week();
int get_mon_hex();
int get_year();
int AddThread(char *name, unsigned int func, unsigned int stack);
void TaskLock();
void TaskUnlock();
void SubThread(unsigned int taskID);
int set_mode(int w, int h); // 设置显示模式（bpp不能设置，只能32位彩色）
void VBEDraw_Px(int x, int y, unsigned int color);
unsigned int VBEGet_Px(int x, int y);
void VBEGetBuffer(void *buffer);
void VBESetBuffer(int x,int y,int w,int h,void *buffer);
void VBEDraw_Box(int x, int y, int x1, int y1, int color);
char get_cons_color();
void set_cons_color(uint8_t c);
void start_keyboard_message();
uint8_t key_press_status();
uint8_t key_up_status();
uint8_t get_key_press();
uint8_t get_key_up();
unsigned api_heapsize();
void sbrk(unsigned size);
int api_current_drive();
int exec(char *filename, char *cmdline);
void clear();
int vfs_check_mount(uint8_t drive);
int vfs_mount(uint8_t disk_number,uint8_t drive);
void vfs_change_disk(uint8_t drive);
uint32_t mem_used();
uint32_t mem_total();
void tty_start_cur_moving();
void tty_stop_cur_moving();
void vfs_unmount_disk(uint8_t drive);
void api_rename(char *f1,char *f2);
void exit(unsigned status);
void logk(char *s);
int logkf(const char *format, ...);
int fork();
#ifdef __cplusplus
}
#endif
#endif
