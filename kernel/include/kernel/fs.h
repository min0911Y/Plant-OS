#pragma once
#include "list.h"
#include <define.h>
#include <type.h>
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
  List *path;
  void *cache;
  char  FSName[255];
  int   disk_number;
  u8    drive; // 大写（必须）
  vfs_file *(*FileInfo)(struct vfs_t *vfs, char *filename);
  List *(*ListFile)(struct vfs_t *vfs, char *dictpath);
  bool (*ReadFile)(struct vfs_t *vfs, char *path, char *buffer);
  bool (*WriteFile)(struct vfs_t *vfs, char *path, char *buffer, int size);
  bool (*DelFile)(struct vfs_t *vfs, char *path);
  bool (*DelDict)(struct vfs_t *vfs, char *path);
  bool (*CreateFile)(struct vfs_t *vfs, char *filename);
  bool (*CreateDict)(struct vfs_t *vfs, char *filename);
  bool (*RenameFile)(struct vfs_t *vfs, char *filename, char *filename_of_new);
  bool (*Attrib)(struct vfs_t *vfs, char *filename, ftype type);
  bool (*Format)(u8 disk_number);
  void (*InitFs)(struct vfs_t *vfs, u8 disk_number);
  void (*DeleteFs)(struct vfs_t *vfs);
  bool (*Check)(u8 disk_number);
  bool (*cd)(struct vfs_t *vfs, char *dictName);
  int (*FileSize)(struct vfs_t *vfs, char *filename);
  void (*CopyCache)(struct vfs_t *dest, struct vfs_t *src);
  int flag;
} vfs_t;
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
struct FAT_FILEINFO {
  u8   name[8], ext[3], type;
  char reserve;
  u8   create_time_tenth;
  u16  create_time, create_date, access_date, clustno_high;
  u16  update_time, update_date, clustno_low;
  u32  size;
};
#define rmfarptr2ptr(x) ((x).seg * 0x10 + (x).offset)
typedef struct FILE {
  u32   mode;
  u32   fileSize;
  u8   *buffer;
  u32   bufferSize;
  u32   p;
  char *name;
} FILE;
struct DLL_STRPICENV {
  int work[16384];
};
struct RGB {
  u8 b, g, r, t;
};
struct paw_info {
  u8   reserved[12]; // 12 bytes reserved(0xFF)
  char oem[3];       // PRA
  int  xsize;        // xsize
  int  ysize;        // ysize
};
extern u32 Path_Addr;
struct FAT_CACHE {
  u32                  ADR_DISKIMG;
  struct FAT_FILEINFO *root_directory;
  struct List         *directory_list;
  struct List         *directory_clustno_list;
  struct List         *directory_max_list;
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

#define vfs_now current_task()->nfs