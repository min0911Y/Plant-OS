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
void inthandler20(int cs, perf_irq_frame_t *frame);
// mtask.c
mtask *current_task();
mtask *get_task(unsigned tid);
mtask *create_task(uintptr_t entry, unsigned weight);
bool task_publish(mtask *task);
void task_abort_creation(mtask *task);
mtask *create_thread_task(uintptr_t entry, unsigned weight);
void task_set_default_drive(char drive);
void task_to_user_mode_elf(char *filename);
void task_kill(unsigned tid);
void task_exit(unsigned status);
int os_execute(char *filename, char *line);
int os_execute_shell(const char *line, size_t line_length);
void idle();
void init();
extern uint64_t global_time;
struct FIFO8 *task_get_key_fifo(mtask *task);
void task_fifo_sleep(mtask *task);
struct FIFO8 *task_get_mouse_fifo(mtask *task);
void task_lock();
void task_unlock();
void task_to_user_mode(unsigned eip, unsigned esp);
void task_set_fifo(mtask *task, struct FIFO8 *kfifo, struct FIFO8 *mfifo);
void os_execute_no_ret(char *filename, char *line);
uint32_t get_father_tid(mtask *t);
int waittid(uint32_t tid);
void task_fall_blocked(enum STATE state);
void task_fall_blocked_reason(enum STATE state, enum WAIT_REASON reason);
void task_run(mtask *task);
void mtask_run_now(mtask *obj);
int task_fork();
unsigned task_address_space_owner(unsigned pde);
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
void init_page(void);
void pf_set(unsigned int memsize);
int get_line_address(int t, int p, int o);
int get_page_from_line_address(int line_address);
void page2tpo(int page, int *t, int *p);
void tpo2page(int *page, int t, int p);
void *page_malloc_one();
void *page_malloc_one_count_from_4gb();
void *page_malloc_one_count_from_4gb_mark(unsigned tid);
void *page_malloc_one_no_mark();
void *page_malloc_one_mark(unsigned tid);
int get_pageinpte_address(int t, int p);
unsigned page_ref_count(unsigned paddr);
void page_ref_release(unsigned paddr);
unsigned page_used_count(unsigned physical_size);
void page_free_one(void *p);
int find_kpage(int line, int n);
void *page_malloc(int size);
void page_free(void *p, int size);
void page_map(void *target, void *start, void *end);
void change_page_task_id(int task_id, void *p, unsigned int size);
void page_set_physics_attr(uint32_t vaddr, void *paddr, uint32_t attr);
uint32_t page_get_attr(unsigned vaddr);
uint32_t page_get_phy(unsigned vaddr);
void copy_from_phy_to_line(unsigned phy, unsigned line, unsigned pde,
                           unsigned size);
uint32_t page_get_attr_pde(unsigned vaddr, unsigned pde);
void set_line_address(unsigned val, unsigned line, unsigned pde, unsigned size);
int page_link_pde(unsigned addr, unsigned pde);
uint32_t page_get_phy_pde(unsigned vaddr, unsigned pde);
bool page_share_range_pde(uint32_t source, uint32_t target, uint32_t size,
                          uint32_t source_pde, uint32_t target_pde);
bool page_unmap_shared_range_pde(uint32_t target, uint32_t size,
                                 uint32_t target_pde);
void page_links(unsigned start, unsigned numbers);
int page_link(unsigned addr);
int page_link_share(unsigned addr);
void pde_retain(unsigned addr);
// other.c
void insert_char(char *str, int pos, char ch); // str:字符串，pos:位置，ch:字符
void delete_char(char *str, int pos);          // str:字符串，pos:位置
char bcd2hex(char bcd);
char hex2bcd(char hex);
void getCPUBrand(char *cBrand); // cBrand 至少 49 字节
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
void sb16_remove_task(mtask *task);
unsigned int disk_Size(char drive);
void disk_read(unsigned int lba, unsigned int number, void *buffer, char drive);
void disk_write(unsigned int lba, unsigned int number, void *buffer,
                char drive);
bool CDROM_Read(unsigned int lba, unsigned int number, void *buffer,
                char drive);
bool DiskReady(char drive);
int getReadyDisk();
// kernelc.c
struct tty *now_tty();
void task_sr1();
void task_sr2();
void tty_stop_cursor_moving(struct tty *t);
void tty_start_curor_moving(struct tty *t);
// mem.c
unsigned int memtest(unsigned int start, unsigned int end);
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
void set_signal_handler(unsigned sig, unsigned handler);
// farcall.c
uint32_t call_across_page(uint32_t (*f)(void *arg), unsigned cr3, void *a);
// fartty.c
struct tty *fartty_alloc(void *vram, unsigned handle, unsigned cr3, int xsize,
                         int ysize);
#endif
