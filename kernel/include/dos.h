#pragma once
#include <cmd.h>
#include <ctypes.h>
#include <drivers.h>
#include <fs.h>
#include <interrupts.h>
#include <io.h>
#include <kernel.h>
#include <net.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// gdtidt.c
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, u32 limit, int base, int ar);
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar);
void init_pic();
void init_gdtidt();
void register_intr_handler(int num, int addr);

// timer.c
void          init_pit();
struct TIMER *timer_alloc();
void          timer_free(struct TIMER *timer);
void          timer_init(struct TIMER *timer, struct FIFO8 *fifo, u8 data);
void          timer_settime(struct TIMER *timer, u32 timeout);
void          inthandler20(int cs, int *esp);

// mtask.c
#define SA_RPL_MASK      0xFFFC
#define SA_TI_MASK       0xFFFB
#define SA_TIL           4 // 设置此项，将从LDT中寻找
#define SA_RPL0          0
#define SA_RPL1          1
#define SA_RPL2          2
#define SA_RPL3          3
#define GET_SEL(cs, rpl) ((cs & SA_RPL_MASK & SA_TI_MASK) | (rpl))
mtask        *current_task();
mtask        *get_task(unsigned tid);
mtask        *create_task(unsigned eip, unsigned esp, unsigned ticks, unsigned floor);
void          task_to_user_mode_elf(char *filename);
void          task_kill(unsigned tid);
void          task_exit(unsigned status);
int           os_execute(char *filename, char *line);
int           os_execute_shell(char *line);
void          idle();
void          init();
extern u64    global_time;
void          set_cr3(u32 pde);
struct FIFO8 *task_get_key_fifo(mtask *task);
void          task_fifo_sleep(mtask *task);
struct FIFO8 *task_get_mouse_fifo(mtask *task);
void          task_lock();
void          task_unlock();
void          task_to_user_mode(unsigned eip, unsigned esp);
void          task_set_fifo(mtask *task, struct FIFO8 *kfifo, struct FIFO8 *mfifo);
void          os_execute_no_ret(char *filename, char *line);
u32           get_father_tid(mtask *t);
int           waittid(u32 tid);
void          task_fall_blocked(enum STATE state);
void          task_run(mtask *task);
void          mtask_run_now(mtask *obj);
int           task_fork();
int           into_mtask();

// page.c
void  C_init_page();
void  pf_set(u32 memsize);
int   get_line_address(int t, int p, int o);
int   get_page_from_line_address(int line_address);
void  page2tpo(int page, int *t, int *p);
void  tpo2page(int *page, int t, int p);
void *page_malloc_one();
void *page_malloc_one_count_from_4gb();
void *page_malloc_one_no_mark();
void *page_malloc_one_mark(unsigned tid);
int   get_pageinpte_address(int t, int p);
void  page_free_one(void *p);
int   find_kpage(int line, int n);
void *page_malloc(int size);
void  page_free(void *p, int size);
void  page_map(void *target, void *start, void *end);
void  change_page_task_id(int task_id, void *p, u32 size);
void  PF(unsigned edi, unsigned esi, unsigned ebp, unsigned esp, unsigned ebx, unsigned edx,
         unsigned ecx, unsigned eax, unsigned gs, unsigned fs, unsigned es, unsigned ds,
         unsigned error, unsigned eip, unsigned cs, unsigned eflags);
void  page_set_physics_attr(u32 vaddr, void *paddr, u32 attr);
u32   page_get_attr(unsigned vaddr);
u32   page_get_phy(unsigned vaddr);
void  copy_from_phy_to_line(unsigned phy, unsigned line, unsigned pde, unsigned size);
u32   page_get_attr_pde(unsigned vaddr, unsigned pde);
void  set_line_address(unsigned val, unsigned line, unsigned pde, unsigned size);
void  page_link_pde(unsigned addr, unsigned pde);
u32   page_get_phy_pde(unsigned vaddr, unsigned pde);
void  page_links(unsigned start, unsigned numbers);

// nasmfunc.asm
void int32(u8 intnum, regs16_t *regs);
void floppy_int(void);
int  get_eip();
void farjmp(int eip, int cs);
void farcall(int eip, int cs);
void asm_inthandler36();
void asm_inthandler72();
void asm_gui_api();
void asm_net_api();
void asm_inthandler2c();
void asm_inthandler20();
void asm_inthandler21();
void asm_ide_irq();
void io_cli();
void io_sti();
void io_stihlt();
void load_tr(int tr);
void io_out8(int port, int data);
void io_out16(int port, int data);
void io_out32(int port, int data);
int  io_in8(int port);
int  io_in16(int port);
int  io_in32(int port);
int  io_load_eflags(void);
void io_store_eflags(int eflags);
void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);
int  load_cr0(void);
void store_cr0(int cr0);
void gensound(int notes, int dup);
int  get_cpu1();
int  get_cpu2();
int  get_cpu3();
int  get_cpu4(int);
int  get_cpu5(int);
int  get_cpu6(int);
int  get_cpu7(int);
void asm_error0();
void asm_error1();
void asm_error3();
void asm_error4();
void asm_error5();
void asm_error6();
void asm_error7();
void asm_error8();
void asm_error9();
void asm_error10();
void asm_error11();
void asm_error12();
void asm_error13();
void asm_error14();
void asm_error16();
void asm_error17();
void asm_error18();
void null_inthandler();
void PCNET_ASM_INTHANDLER(void);
void asm_sb16_handler(int *esp);
void __init_PIT();
void init_float();

// other.c
void INT(u8 intnum, regs16_t *regs);
void ERROR0(u32 eip);
void ERROR1(u32 eip);
void ERROR3(u32 eip);
void ERROR4(u32 eip);
void ERROR5(u32 eip);
void ERROR6(u32 eip);
void ERROR7(u32 eip);
void ERROR8(u32 eip);
void ERROR9(u32 eip);
void ERROR10(u32 eip);
void ERROR11(u32 eip);
void ERROR12(u32 eip);
void ERROR13(u32 eip);
void ERROR16(u32 eip);
void ERROR17(u32 eip);
void ERROR18(u32 eip);
void ERROR19(u32 eip);
void ERROR(int CODE, char *TIPS);
void KILLAPP(int ec, int eip);
void KILLAPP0(int ec, int tn);
void insert_char(char *str, int pos, char ch); // str:字符串，pos:位置，ch:字符
void delete_char(char *str, int pos);          // str:字符串，pos:位置
char bcd2hex(char bcd);
char hex2bcd(char hex);
void getCPUBrand(char *cBrand);
char ascii2num(char c);
char num2ascii(char c);
void strtoupper(char *str);
int  GetCHorEN(u8 *str);
void clean(char *s, int len);
void disableExp();
void EnableExp();
u32  get_cr0();
void set_cr0(u32 cr0);
char GetExpFlag();
void ClearExpFlag();
void SetCatchEip(u32 eip);
void SwitchPublic();
void SwitchPrivate();

// syscall.c
void inthandler36(int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax);

// fifo.c
void fifo8_init(struct FIFO8 *fifo, int size, u8 *buf);
int  fifo8_put(struct FIFO8 *fifo, u8 data);
int  fifo8_get(struct FIFO8 *fifo);
int  fifo8_status(struct FIFO8 *fifo);

// list.c
void         AddVal(uintptr_t val, struct List *Obj);
struct List *FindForCount(size_t count, struct List *Obj);
void         DeleteVal(size_t count, struct List *Obj);
struct List *NewList();
void         Change(size_t count, struct List *Obj, uintptr_t val);
int          GetLastCount(struct List *Obj);
void         DeleteList(struct List *Obj);

// init.c
void sysinit();
bool SetDrive(u8 *name);
u32  GetDriveCode(u8 *name);
bool DriveSemaphoreTake(u32 drive_code);
void DriveSemaphoreGive(u32 drive_code);
u32  disk_Size(char drive);
void Disk_Read(u32 lba, u32 number, void *buffer, char drive);
void Disk_Write(u32 lba, u32 number, void *buffer, char drive);
bool CDROM_Read(u32 lba, u32 number, void *buffer, char drive);
bool DiskReady(char drive);
int  getReadyDisk();

// kernelc.c
void        shell(void);
void        shell_handler();
struct tty *now_tty();
void        task_sr1();
void        task_sr2();
void        tty_stop_cursor_moving(struct tty *t);
void        tty_start_curor_moving(struct tty *t);

// mem.c
u32          memtest(u32 start, u32 end);
freeinfo    *make_next_freeinfo(memory *mem);
free_member *mem_insert(int pos, freeinfo *finf);
free_member *mem_add(freeinfo *finf);
void         mem_delete(int pos, freeinfo *finf);
u32          mem_get_all_finf(freeinfo *finf);
void         mem_defragmenter(freeinfo *finf);
u32          mem_partition(freeinfo *finf, u32 start, u32 end, u32 index);
int          mem_free_finf(memory *mem, freeinfo *finf, void *p, u32 size);
void        *mem_alloc_finf(memory *mem, freeinfo *finf, u32 size, freeinfo *if_nomore);
void        *mem_alloc(memory *mem, u32 size);
void         mem_free(memory *mem, void *p, u32 size);
memory      *memory_init(u32 start, u32 size);
void        *malloc(int size);
void         free(void *p);
void        *realloc(void *ptr, u32 size);

// ipc.c
int  send_ipc_message(int to_tid, void *data, u32 size, char type);
int  send_ipc_message_by_name(char *tname, void *data, u32 size, char type);
int  get_ipc_message(void *data, int from_tid);
int  get_ipc_message_by_name(void *data, char *tname);
int  ipc_message_status();
u32  ipc_message_len(int from_tid);
bool have_msg();
int  get_msg_all(void *data);

// arg.c
int Get_Arg(char *Arg, char *CmdLine, int Count);
int Get_Argc(char *CmdLine);

// time.c
unsigned time(void);

// rand.c
u32  rand(void);
void srand(u32 seed);

// md5.c
void md5s(char *hexbuf, int read_len, char *result);
void md5f(char *filename, u8 *result);

// lock.c
bool cas(int *ptr, int old, int New);
void lock(lock_t *key);
void unlock(lock_t *key);
void lock_init(lock_t *l);

// mount.c
int  mount(char *fileName);
void unmount(char drive);

// signal.c
void set_signal_handler(unsigned sig, unsigned handler);

// farcall.c
u32 call_across_page(u32 (*f)(void *arg), unsigned cr3, void *a);

// fartty.c
struct tty *fartty_alloc(void *vram, unsigned handle, unsigned cr3, int xsize, int ysize);

int isdigit(int c);