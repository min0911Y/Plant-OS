// Powerint DOS 386系统调用
// @ Copyright (C) 2022
// @ Author: zhouzhihao & min0911_
#ifndef SYS_CALL_H
#define SYS_CALL_H
#ifdef __cplusplus
extern "C" {
#endif
#include <ctypes.h>
#include <input_event.h>
enum { SYSCALL_SIGNAL_RETURN = 0x65 };
#define T_DrawBox(x, y, w, h, c) Text_Draw_Box((y), (x), (h) + y, (w) + x, (c))
typedef enum { FLE, DIR, RDO, HID, SYS } ftype;
struct finfo_block {
  char name[255];
  ftype type;
  unsigned int size;
  unsigned short year, month, day;
  unsigned short hour, minute;
};
typedef struct {
  uint32_t type;
  uint32_t attributes;
  uint32_t size;
  uint32_t modified_time;
} vfs_file_stat_t;

enum vfs_open_flags {
  VFS_OPEN_READ = 1u << 0,
  VFS_OPEN_WRITE = 1u << 1,
  VFS_OPEN_CREATE = 1u << 2,
  VFS_OPEN_EXCLUSIVE = 1u << 3,
  VFS_OPEN_TRUNCATE = 1u << 4,
  VFS_OPEN_APPEND = 1u << 5,
};

enum vfs_syscall_operation {
  VFS_SYSCALL_OPEN,
  VFS_SYSCALL_CLOSE,
  VFS_SYSCALL_READ,
  VFS_SYSCALL_WRITE,
  VFS_SYSCALL_SEEK,
  VFS_SYSCALL_SYNC,
  VFS_SYSCALL_STAT,
  VFS_SYSCALL_FSTAT,
  VFS_SYSCALL_LIST_DIRECTORY,
  VFS_SYSCALL_MKDIR,
  VFS_SYSCALL_UNLINK,
  VFS_SYSCALL_RMDIR,
  VFS_SYSCALL_RENAME,
  VFS_SYSCALL_CHDIR,
  VFS_SYSCALL_GETCWD,
  VFS_SYSCALL_CURRENT_DRIVE,
  VFS_SYSCALL_MOUNT_CHECK,
  VFS_SYSCALL_MOUNT,
  VFS_SYSCALL_UNMOUNT,
  VFS_SYSCALL_CHANGE_DRIVE,
  VFS_SYSCALL_FORMAT,
  VFS_SYSCALL_COUNT,
};

typedef struct {
  uint32_t size;
  union {
    struct {
      uintptr_t path;
      uint32_t flags;
    } open;
    struct {
      int32_t descriptor;
    } descriptor;
    struct {
      int32_t descriptor;
      uintptr_t buffer;
      uint32_t length;
    } io;
    struct {
      int32_t descriptor;
      int32_t offset;
      int32_t whence;
    } seek;
    struct {
      uintptr_t path;
      uintptr_t status;
    } stat;
    struct {
      int32_t descriptor;
      uintptr_t status;
    } fstat;
    struct {
      uintptr_t path;
      uintptr_t entries;
      uint32_t capacity;
    } list;
    struct {
      uintptr_t path;
    } path;
    struct {
      uintptr_t source;
      uintptr_t destination;
    } rename;
    struct {
      uintptr_t buffer;
      uint32_t capacity;
    } cwd;
    struct {
      uint32_t disk;
      uint32_t drive;
    } mount;
    struct {
      uint32_t disk;
      uint32_t filesystem;
    } format;
  } arguments;
} vfs_syscall_request_t;

int vfs_syscall(uint32_t operation, const vfs_syscall_request_t *request);
typedef uintptr_t tty_t;
typedef struct module_handle {
  uint32_t id;
  char name[32];
  char path[256];
  uint32_t image_size;
  uint32_t section_count;
  uint32_t export_count;
} module_handle_t;

enum key_input {
  KEY_INPUT_UP = -1,
  KEY_INPUT_DOWN = -2,
  KEY_INPUT_LEFT = -3,
  KEY_INPUT_RIGHT = -4,
};

void putch(char ch);
int getch(void);
char input_char_inSM();
int get_xy();
void goto_xy(int x, int y);
int SwitchTo320X200X256(void);
int SwitchToText8025(void);
int get_mouse();
void Draw_Char(int x, int y, char ch, int color);
void Draw_Str(int x, int y, char *str, int color);
void sleep(int time);
void PrintChineseChar(int x, int y, int color, unsigned short cChar);
void PrintChineseStr(int x, int y, int color, unsigned char *cStr);
void print(char *str);
void scan(char *str, int length);
int system(char *command);
void bmpview(char *filename);
void Draw_Box(int x, int y, int w, int h, int color);
void Draw_Px(int x, int y, int color);
void Text_Draw_Box(int y, int x, int h, int w, int color);
void api_beep(int point, int notes, int dup);
int get_command_line(char **line, size_t *length);
int Get_System_Version();
int Copy(char *filePath1, char *filePath2);
int _kbhit();
int mkdir(const char *filename, ...);
int rmdir(const char *filename);
int chdir(const char *path);
int list_directory(const char *path, struct finfo_block **entries,
                   size_t *count);
int SwitchTo320X200X256_BIOS(void);
int SwitchToText8025_BIOS(void);
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
char *api_get_env(char *name, char *value);
int format(unsigned drive, char *fs_name);
void *malloc(size_t size);
void free(void *p);
void *realloc(void *ptr, size_t size);
int get_hour_hex();
int get_min_hex();
int get_sec_hex();
int get_day_of_month();
int get_day_of_week();
int get_mon_hex();
int get_year();
/* stack_top ends caller-owned writable storage. The architecture aligns the
 * C entry frame and passes argument to func, which must exit without returning. */
int AddThread(const char *name, uintptr_t func, uintptr_t stack_top,
              uintptr_t argument);
void TaskLock();
void TaskUnlock();
void SubThread(unsigned int taskID);
intptr_t set_mode(int w, int h);
void VBEDraw_Px(int x, int y, unsigned int color);
unsigned int VBEGet_Px(int x, int y);
void VBEGetBuffer(void *buffer);
void VBESetBuffer(int x,int y,int w,int h,void *buffer);
void VBEDraw_Box(int x, int y, int x1, int y1, int color);
char get_cons_color();
void set_cons_color(uint8_t c);
int start_keyboard_message(void);
uint8_t key_press_status();
uint8_t key_up_status();
uint8_t get_key_press();
uint8_t get_key_up();
size_t api_heapsize();
int sbrk(size_t size);
int api_current_drive();
int exec(char *filename, char *cmdline);
void clear();
int vfs_check_mount(uint8_t drive);
int vfs_mount(uint8_t disk_number,uint8_t drive);
int vfs_change_disk(uint8_t drive);
size_t mem_used(void);
size_t mem_total(void);
void tty_start_cur_moving();
void tty_stop_cur_moving();
int tty_get_xsize(void);
int tty_get_ysize(void);
int vfs_unmount_disk(uint8_t drive);
void exit(unsigned status);
void logk(char *s);
int logkf(const char *format, ...);
int fork();
int waittid(unsigned tid);
int mouse_enable();
int mouse_dat_status();
int mouse_read(mouse_event_t *event);
typedef enum {
  INPUT_WAIT_MOUSE = 1u << 0,
  INPUT_WAIT_KEY_PRESS = 1u << 1,
  INPUT_WAIT_KEY_RELEASE = 1u << 2,
  INPUT_WAIT_ALL = INPUT_WAIT_MOUSE | INPUT_WAIT_KEY_PRESS |
                   INPUT_WAIT_KEY_RELEASE,
} input_wait_event_t;
int input_wait(uint32_t events);
void api_yield(void);
tty_t tty_alloc(void *vram, uintptr_t handle, unsigned xsize, unsigned ysize);
void tty_set(unsigned tid,tty_t tty);
void tty_free(tty_t tty);
int tty_notify_input(tty_t tty);
int shared_memory_map_to(unsigned target_tid, unsigned target_generation,
                         const void *source, void *target, unsigned size);
int shared_memory_unmap(void *target, unsigned size);
void task_set_level_higher(unsigned tid);
void task_set_level_normal(unsigned tid);
int use_keyboard();
void abi_alloc_init(void);
int module_load(char *path);
int module_unload(char *name);
int module_list(module_handle_t *out, int max_count);
#ifdef __cplusplus
}
#endif
#endif
