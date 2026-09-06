#ifndef _DOS_H
#define _DOS_H
#include <ctypes.h>
#include <define.h>
#include <drivers.h>
#include <fs.h>
#include <heap.h>
#include <interrupts.h>
#include <io.h>
#include <kasan.h>
#include <module.h>
#include <net.h>
#include <perf.h>
#include <smp.h>
#include <stddef.h>
#include <task_snapshot.h>
#include <stdio.h>
#include <string.h>

void init_pic(void);
// timer.c
void init_pit(void);
struct TIMER *timer_alloc(void);
bool timer_cancel(struct TIMER *timer);
void timer_cancel_for_task(mtask *task);
void timer_free(struct TIMER *timer);
void timer_init(struct TIMER *timer, struct FIFO8 *fifo, unsigned char data);
void timer_settime(struct TIMER *timer, unsigned int timeout);
// mtask.c
typedef struct {
  uint32_t next_tid;
} task_iterator_t;

mtask *current_task();
mtask *get_task(unsigned tid);
mtask *task_iter_next(task_iterator_t *iterator);
mtask *create_task(uintptr_t entry, unsigned weight);
bool task_publish(mtask *task);
void task_abort_creation(mtask *task);
mtask *create_thread_task(uintptr_t entry, unsigned weight);
void task_set_default_drive(char drive);
void task_to_user_mode_elf(char *filename);
void task_kill(unsigned tid);
void task_exit(unsigned status);
void idle();
void init();
struct FIFO8 *task_get_key_fifo(mtask *task);
void task_fifo_sleep(mtask *task);
struct FIFO8 *task_get_mouse_fifo(mtask *task);
void task_lock();
void task_unlock();
void task_set_fifo(mtask *task, struct FIFO8 *kfifo, struct FIFO8 *mfifo);
void os_execute_no_ret(char *filename, char *line);
uint32_t get_father_tid(mtask *t);
int waittid(uint32_t tid);
void task_fall_blocked(enum STATE state);
void task_fall_blocked_reason(enum STATE state, enum WAIT_REASON reason);
void task_run(mtask *task);
void mtask_run_now(mtask *obj);
int task_fork();
unsigned task_address_space_owner(arch_address_space_t address_space);
void task_next(void);
void scheduler_tick(void);
void scheduler_reschedule_interrupt(void);
void scheduler_preempt_if_needed(void);
__attribute__((noreturn)) void scheduler_start_secondary(uint32_t cpu);
void task_set_name(mtask *task, const char *name);
int task_snapshot(task_info_t *entries, uint32_t capacity, uint32_t *count);
bool task_pin_current(uint32_t cpu);
unsigned task_wake_tty(struct tty *tty);
void task_close_tty(struct tty *tty, struct tty *fallback);
void signal_deal(void);
// page.c
void init_page(const boot_info_t *boot_info);
bool page_reserve_physical_range(uintptr_t start, uint32_t size);
void pf_set(uintptr_t physical_size);
void *page_malloc_one();
void *page_malloc_one_no_mark();
unsigned page_ref_count(uintptr_t paddr);
void page_ref_release(uintptr_t paddr);
size_t page_used_count(uintptr_t physical_size);
void page_free_one(void *p);
void *page_malloc(int size);
void page_free(void *p, int size);
void change_page_task_id(uint32_t task_id, void *p, unsigned int size);
int page_link(uintptr_t address);
// other.c
void insert_char(char *str, int pos, char ch); // str:字符串，pos:位置，ch:字符
void delete_char(char *str, int pos);          // str:字符串，pos:位置
char bcd2hex(char bcd);
char hex2bcd(char hex);
char ascii2num(char c);
char num2ascii(char c);
void strtoupper(char *str);
int GetCHorEN(unsigned char *str);
void clean(char *s, int len);
// fifo.c
void fifo8_init(struct FIFO8 *fifo, int size, unsigned char *buf);
int fifo8_put(struct FIFO8 *fifo, unsigned char data);
int fifo8_get(struct FIFO8 *fifo);
int fifo8_status(struct FIFO8 *fifo);
// list.c
bool AddVal(uintptr_t val, struct List *Obj);
struct List *list_get(size_t count, struct List *Obj);
void DeleteVal(size_t count, struct List *Obj);
struct List *NewList();
int GetLastCount(struct List *Obj);
void DeleteList(struct List *Obj);
// init.c
void sysinit();
bool SetDrive(unsigned char *name);
unsigned int GetDriveCode(unsigned char *name);
bool DriveSemaphoreTake(unsigned int drive_code);
void DriveSemaphoreGive(unsigned int drive_code);
void vdisk_remove_task(unsigned tid);
uint64_t disk_Size(char drive);
bool disk_read(unsigned int lba, unsigned int number, void *buffer, char drive);
bool disk_write(unsigned int lba, unsigned int number, void *buffer,
                char drive);
bool CDROM_Read(unsigned int lba, unsigned int number, void *buffer,
                char drive);
bool DiskReady(char drive);
int getReadyDisk();
void init_mount_disk(void);
// kernelc.c
struct tty *now_tty();
void tty_stop_cursor_moving(struct tty *t);
void tty_start_curor_moving(struct tty *t);
// mem.c
void init_iso9660(void);
void reg_pfs(void);
int into_mtask(void);
// ipc.c
void ipc_header_init(IPC_Header *ipc);
void ipc_task_init(mtask *task);
void ipc_task_cleanup(mtask *task);
int ipc_send(uint32_t to_tid, uint32_t to_generation, uint32_t type, uint32_t id,
             const void *data, uint32_t size, uint32_t flags,
             uint32_t timeout_ms);
int ipc_recv(void *buf, uint32_t bufsize, ipc_msg_info_t *info,
             uint32_t from_filter, uint32_t flags, uint32_t timeout_ms);
int ipc_peek(ipc_msg_info_t *info, uint32_t from_filter);
int ipc_pending(void);
void ipc_tick(void);
int ipc_service_register(const char *name);
int ipc_service_unregister(const char *name);
int ipc_service_lookup(const char *name, uint32_t *generation);
// ipc.c（旧接口，保留兼容）
int send_ipc_message(int to_tid, void *data, unsigned int size, char type);
int send_ipc_message_by_name(char *tname, void *data, unsigned int size,
                             char type);
int get_ipc_message(void *data, int from_tid);
int get_ipc_message_by_name(void *data, char *tname);
int ipc_message_status();
unsigned int ipc_message_len(int from_tid);
bool have_msg();
int get_msg_all(void *data);
// arg.c
int Get_Arg(char *Arg, char *CmdLine, int Count);
int Get_Argc(char *CmdLine);
// time.c
unsigned time(void);
// rand.c
unsigned int rand(void);
void srand(unsigned long seed);
// md5.c
void md5s(char *hexbuf, int read_len, char *result);
void md5f(char *filename, unsigned char *result);
// lock.c
void lock(lock_t *key);
void unlock(lock_t *key);
void lock_init(lock_t *l);
// mount.c
int mount(char *fileName);
void unmount(char drive);
// signal.c
void set_signal_handler(unsigned sig, uintptr_t handler);
// farcall.c
// fartty.c
struct tty *fartty_alloc(void *vram, uintptr_t handle,
                         arch_address_space_t address_space, int xsize,
                         int ysize);
#endif
