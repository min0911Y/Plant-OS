#pragma once
#include <copi143-define.h>
#include <type.h>

// 都没用了

void io_hlt();
void io_sti();
void io_stihlt();
int  io_in8(int port);
int  io_in16(int port);
int  io_in32(int port);
void io_out8(int port, int data);
void io_out16(int port, int data);
void io_out32(int port, int data);
int  io_load_eflags();
void io_store_eflags(int eflags);
void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);
int  load_cr0(void);
void store_cr0(int cr0);

#define ADR_IDT      0x0026f800
#define LIMIT_IDT    0x000007ff
#define ADR_GDT      0x00270000
#define LIMIT_GDT    0x0000ffff
#define ADR_BOTPAK   0x100000
#define LIMIT_BOTPAK 0x0007ffff
#define AR_DATA32_RW 0x4092
#define AR_DATA16_RW 0x0092
#define AR_CODE32_ER 0x409a
#define AR_CODE16_ER 0x009a
#define AR_INTGATE32 0x008e
#define PIC0_ICW1    0x0020
#define PIC0_OCW2    0x0020
#define PIC0_IMR     0x0021
#define PIC0_ICW2    0x0021
#define PIC0_ICW3    0x0021
#define PIC0_ICW4    0x0021
#define PIC1_ICW1    0x00a0
#define PIC1_OCW2    0x00a0
#define PIC1_IMR     0x00a1
#define PIC1_ICW2    0x00a1
#define PIC1_ICW3    0x00a1
#define PIC1_ICW4    0x00a1
#define MEMMAN_FREES 4090

#define MEMMAN_ADDR 0x0005c0000
struct FREEINFO {
  u32 addr, size;
};
struct MEMMAN {
  int             frees, maxfrees, lostsize, losts;
  struct FREEINFO free[MEMMAN_FREES];
};
u32   memtest(u32 start, u32 end);
void  memman_init(struct MEMMAN *man);
u32   memman_total(struct MEMMAN *man);
u32   memman_alloc(struct MEMMAN *man, u32 size);
int   memman_free(struct MEMMAN *man, u32 addr, u32 size);
u32   memman_alloc_4k(struct MEMMAN *man, u32 size);
int   memman_free_4k(struct MEMMAN *man, u32 addr, u32 size);
void *page_malloc(int size);
void  page_free(void *p, int size);

typedef struct {
  void (*Read)(char drive, u8 *buffer, u32 number, u32 lba);
  void (*Write)(char drive, u8 *buffer, u32 number, u32 lba);
  int  flag;
  u32  size; // 大小
  char DriveName[50];
} vdisk;
void ide_read_sectors(u8 drive, u8 numsects, u32 lba, u16 es, u32 edi);
void ide_write_sectors(u8 drive, u8 numsects, u32 lba, u16 es, u32 edi);
void ide_initialize(u32 BAR0, u32 BAR1, u32 BAR2, u32 BAR3, u32 BAR4);
int  init_vdisk();
int  register_vdisk(vdisk vd);
int  logout_vdisk(char drive);
int  rw_vdisk(char drive, u32 lba, u8 *buffer, u32 number, int read);
bool have_vdisk(char drive);

struct IDEHardDiskInfomationBlock {
  char reserve1[2];
  u16  CylinesNum;
  char reserve2[2];
  u16  HeadersNum;
  u16  TrackBytes;
  u16  SectorBytes;
  u16  TrackSectors;
  char reserve3[6];
  char OEM[20];
  char reserve4[2];
  u16  BuffersBytes;
  u16  EECCheckSumLength;
  char Version[8];
  char ID[40];
};

struct FAT_CACHE {
  u32                  ADR_DISKIMG;
  struct FAT_FILEINFO *root_directory;
  struct LIST         *directory_list;
  struct LIST         *directory_max_list;
  struct LIST         *directory_clustno_list;
  int                 *fat;
  int                  FatMaxTerms;
  u32                  ClustnoBytes;
  u16                  RootMaxFiles;
  u32                  RootDictAddress;
  u32                  FileDataAddress;
  u32                  imgTotalSize;
  u16                  SectorBytes;
  u32                  Fat1Address, Fat2Address;
  u8                  *FatClustnoFlags;
  int                  type;
};
typedef struct {
  struct FAT_CACHE     dm;
  struct FAT_FILEINFO *dir;
} fat_cache;
#define get_dm(vfs)            ((fat_cache *)(vfs->cache))->dm
#define get_now_dir(vfs)       ((fat_cache *)(vfs->cache))->dir
#define get_clustno(high, low) (high << 16) | (low & 0xffff)
#define clustno_end(type)      0xfffffff & ((((1 << (type - 1)) - 1) << 1) + 1)
typedef enum {
  FLE,
  DIR,
  RDO,
  HID,
  SYS
} ftype;
typedef struct {
  char  name[255];
  ftype type;
  u32   size;
  u16   year, month, day;
  u16   hour, minute;
} vfs_file;

typedef struct vfs_t {
  struct List *path;
  void        *cache;
  char         FSName[255];
  int          disk_number;
  uint8_t      drive; // 大写（必须）
  vfs_file *(*FileInfo)(struct vfs_t *vfs, char *filename);
  struct List *(*ListFile)(struct vfs_t *vfs, char *dictpath);
  bool (*ReadFile)(struct vfs_t *vfs, char *path, char *buffer);
  bool (*WriteFile)(struct vfs_t *vfs, char *path, char *buffer, int size);
  bool (*DelFile)(struct vfs_t *vfs, char *path);
  bool (*DelDict)(struct vfs_t *vfs, char *path);
  bool (*CreateFile)(struct vfs_t *vfs, char *filename);
  bool (*CreateDict)(struct vfs_t *vfs, char *filename);
  bool (*RenameFile)(struct vfs_t *vfs, char *filename, char *filename_of_new);
  bool (*Attrib)(struct vfs_t *vfs, char *filename, ftype type);
  bool (*Format)(uint8_t disk_number);
  void (*InitFs)(struct vfs_t *vfs, uint8_t disk_number);
  void (*DeleteFs)(struct vfs_t *vfs);
  bool (*Check)(uint8_t disk_number);
  bool (*cd)(struct vfs_t *vfs, char *dictName);
  int (*FileSize)(struct vfs_t *vfs, char *filename);
  void (*CopyCache)(struct vfs_t *dest, struct vfs_t *src);
  int flag;
} vfs_t;
struct FAT_FILEINFO {
  u8   name[8], ext[3], type;
  char reserve;
  u8   create_time_tenth;
  u16  create_time, create_date, access_date, clustno_high;
  u16  update_time, update_date, clustno_low;
  u32  size;
};

struct TASK {
  int           drive_number;
  char          drive;
  struct vfs_t *nfs;
} __attribute__((packed));
#define vfs_now NowTask()->nfs

struct ListCtl {
  struct List *start;
  struct List *end;
  int          all;
};
struct List {
  struct ListCtl *ctl;
  struct List    *prev;
  uintptr_t       val;
  struct List    *next;
};
typedef struct List List;

void         AddVal(uintptr_t val, struct List *Obj);
struct List *FindForCount(size_t count, struct List *Obj);
void         DeleteVal(size_t count, struct List *Obj);
struct List *NewList();
void         Change(size_t count, struct List *Obj, uintptr_t val);
int          GetLastCount(struct List *Obj);
void         DeleteList(struct List *Obj);
typedef struct FILE {
  u32   mode;
  u32   fileSize;
  u8   *buffer;
  u32   bufferSize;
  u32   p;
  char *name;
} FILE;
int printf(const char *format, ...);
struct SEGMENT_DESCRIPTOR {
  short limit_low, base_low;
  char  base_mid, access_right;
  char  limit_high, base_high;
};
struct GATE_DESCRIPTOR {
  short offset_low, selector;
  char  dw_count, access_right;
  short offset_high;
};
struct pci_config_space_public {
  u16 VendorID;
  u16 DeviceID;
  u16 Command;
  u16 Status;
  u8  RevisionID;
  u8  ProgIF;
  u8  SubClass;
  u8  BaseClass;
  u8  CacheLineSize;
  u8  LatencyTimer;
  u8  HeaderType;
  u8  BIST;
  u32 BaseAddr[6];
  u32 CardbusCIS;
  u16 SubVendorID;
  u16 SubSystemID;
  u32 ROMBaseAddr;
  u8  CapabilitiesPtr;
  u8  Reserved[3];
  u32 Reserved1;
  u8  InterruptLine;
  u8  InterruptPin;
  u8  MinGrant;
  u8  MaxLatency;
};
struct TASK *NowTask();
#define BS_jmpBoot       0
#define BS_OEMName       3
#define BPB_BytsPerSec   11
#define BPB_SecPerClus   13
#define BPB_RsvdSecCnt   14
#define BPB_NumFATs      16
#define BPB_RootEntCnt   17
#define BPB_TotSec16     19
#define BPB_Media        21
#define BPB_FATSz16      22
#define BPB_SecPerTrk    24
#define BPB_NumHeads     26
#define BPB_HiddSec      28
#define BPB_TotSec32     32
#define BPB_FATSz32      36
#define BPB_ExtFlags     40
#define BPB_FSVer        42
#define BPB_RootClus     44
#define BPB_FSInfo       48
#define BPB_BkBootSec    50
#define BPB_Reserved     52
#define BPB_Fat32ExtByts 28
#define BS_DrvNum        36
#define BS_Reserved1     37
#define BS_BootSig       38
#define BS_VolD          39
#define BS_VolLab        43
#define BS_FileSysType   54
#define EOF              -1
#define SEEK_SET         0
#define SEEK_CUR         1
#define SEEK_END         2
#define BCD_HEX(n)       ((n >> 4) * 10) + (n & 0xf)
#define HEX_BCD(n)       ((n / 10) << 4) + (n % 10)
#define CMOS_CUR_SEC     0x0
#define CMOS_ALA_SEC     0x1
#define CMOS_CUR_MIN     0x2
#define CMOS_ALA_MIN     0x3
#define CMOS_CUR_HOUR    0x4
#define CMOS_ALA_HOUR    0x5
#define CMOS_WEEK_DAY    0x6
#define CMOS_MON_DAY     0x7
#define CMOS_CUR_MON     0x8
#define CMOS_CUR_YEAR    0x9
#define CMOS_DEV_TYPE    0x12
#define CMOS_CUR_CEN     0x32
#define cmos_index       0x70
#define cmos_data        0x71
#define PORT_KEYDAT      0x0060
#define PORT_KEYSTA      0x0064
#define PORT_KEYCMD      0x0064

List     *vfs_listfile(char *dictpath);
bool      vfs_readfile(char *path, char *buffer);
bool      vfs_writefile(char *path, char *buffer, int size);
uint32_t  vfs_filesize(char *filename);
bool      vfs_register_fs(vfs_t vfs);
bool      vfs_renamefile(char *filename, char *filename_of_new);
bool      vfs_change_path(char *dictName);
bool      vfs_deldir(char *dictname);
bool      vfs_delfile(char *filename);
bool      vfs_createfile(char *filename);
bool      vfs_createdict(char *filename);
void      vfs_getPath(char *buffer);
bool      vfs_change_disk(uint8_t drive);
bool      vfs_mount_disk(uint8_t disk_number, uint8_t drive);
uint32_t  vfs_filesize(char *filename);
bool      vfs_readfile(char *path, char *buffer);
bool      vfs_change_disk_for_task(uint8_t drive, struct TASK *task);
bool      vfs_format(uint8_t disk_number, char *FSName);
bool      vfs_check_mount(uint8_t drive);
bool      vfs_unmount_disk(uint8_t drive);
bool      vfs_attrib(char *filename, ftype type);
vfs_file *vfs_fileinfo(char *filename);
void      Register_fat_fileSys();

size_t   strlen(const char *s);
char    *strcpy(char *dest, const char *src);
void     insert_char(char *str, int pos, char ch);
void     delete_char(char *str, int pos);
int      strcmp(const char *s1, const char *s2);
void    *memcpy(void *s, const void *ct, size_t n);
void    *malloc(int size);
void     free(void *p);
void    *realloc(void *ptr, uint32_t size);
char    *strcat(char *dest, const char *src);
void     clean(char *s, int len);
u32      memtest_sub(u32 start, u32 end);
void    *memset(void *s, int c, size_t n);
uint32_t read_pci(uint8_t bus, uint8_t device, uint8_t function, uint8_t registeroffset);
void     clear();
void     init_gdtidt(void);
void     init_pic(void);
void     init_vfs();
void     init_floppy();
void     reg_pfs();
void     ahci_init();
int      memcmp(const void *s1, const void *s2, size_t n);
void     print(const char *str);
int      isdigit(int c);
int      isupper(int c);
void     assert(int expression); // 按说，assert应该是一个宏才对