#include <dos.h>
#include <fs.h>
#include <type.h>

typedef enum {
  /*! Success! */
  L9660_OK = 0,
  /*! read_sector callback returned false */
  L9660_EIO,
  /*! file system is bad */
  L9660_EBADFS,
  /*! specified name does not exist */
  L9660_ENOENT,
  /*! attempted to open a non-file (e.g. a directory) as a file */
  L9660_ENOTFILE,
  /*! attempted to open a non-directory (e.g. a file) as a directory
   *  may be returned by l9660_openat if e.g. you pass path "a/b" and
   *  "a" is a file
   */
  L9660_ENOTDIR,
} l9660_status;

typedef struct {
  u8 le[2];
} l9660_luint16;
typedef struct {
  u8 be[2];
} l9660_buint16;
typedef struct {
  u8 le[2], be[2];
} l9660_duint16;
typedef struct {
  u8 le[4];
} l9660_luint32;
typedef struct {
  u8 be[4];
} l9660_buint32;
typedef struct {
  u8 le[4], be[4];
} l9660_duint32;

/* Descriptor time format */
typedef struct {
  char d[17];
} l9660_desctime;

/* File time format */
typedef struct {
  char d[7];
} l9660_filetime;

/* Directory entry */
typedef struct {
  u8             length;
  u8             xattr_length;
  l9660_duint32  sector;
  l9660_duint32  size;
  l9660_filetime time;
  u8             flags;
  u8             unit_size;
  u8             gap_size;
  l9660_duint16  vol_seq_number;
  u8             name_len;
  char           name[/*name_len*/];
} l9660_dirent;

/* Volume descriptor header */
typedef struct {
  u8   type;
  char magic[5];
  u8   version;
} l9660_vdesc_header;

/* Primary volume descriptor */
typedef struct {
  l9660_vdesc_header hdr;
  char               pad0[1];
  char               system_id[32];
  char               volume_id[32];
  char               pad1[8];
  l9660_duint32      volume_space_size;
  char               pad2[32];
  l9660_duint16      volume_set_size;
  l9660_duint16      volume_seq_number;
  l9660_duint16      logical_block_size;
  l9660_duint32      path_table_size;
  l9660_luint32      path_table_le;
  l9660_luint32      path_table_opt_le;
  l9660_buint32      path_table_be;
  l9660_buint32      path_table_opt_be;
  union {
    l9660_dirent root_dir_ent;
    char         pad3[34];
  };
  char           volume_set_id[128];
  char           data_preparer_id[128];
  char           app_id[128];
  char           copyright_file[38];
  char           abstract_file[36];
  char           bibliography_file[37];
  l9660_desctime volume_created, volume_modified, volume_expires, volume_effective;
  u8             file_structure_version;
  char           pad4[1];
  char           app_reserved[512];
  char           reserved[653];
} l9660_vdesc_primary;

/* A generic volume descriptor (i.e. 2048 bytes) */
typedef union {
  l9660_vdesc_header hdr;
  char               _bits[2048];
} l9660_vdesc;

typedef struct l9660_file;

typedef struct l9660_fs {
#ifdef L9660_SINGLEBUFFER
  union {
    l9660_dirent root_dir_ent;
    char         root_dir_pad[34];
  };
#else
  /* Sector buffer to hold the PVD */
  l9660_vdesc pvd;
#endif

  /* read_sector func */
  bool (*read_sector)(struct l9660_fs *fs, void *buf, u32 sector);
  int disk_number;
} l9660_fs;

typedef struct {
#ifndef L9660_SINGLEBUFFER
  /* single sector buffer */
  char buf[2048];
#endif
  l9660_fs *fs;
  u32       first_sector;
  u32       position;
  u32       length;
} l9660_file;

typedef struct {
  /* directories are mostly just files with special accessors, but we like type
   * safetey */
  l9660_file file;
} l9660_dir;

typedef struct l9660_fs_status {
  l9660_fs *fs;
  l9660_dir root_dir;
  l9660_dir now_dir;

} l9660_fs_status_t;
#define L9660_SEEK_END -1
#define L9660_SEEK_SET 0
#define L9660_SEEK_CUR +1

void *malloc(int size);
void  free(void *p);

u32          l9660_tell(l9660_file *f);
l9660_status l9660_read(l9660_file *f, void *buf, size_t size, size_t *read);
l9660_status l9660_seek(l9660_file *f, int whence, int32_t offset);
l9660_status l9660_openat(l9660_file *child, l9660_dir *parent, const char *name);
l9660_status l9660_readdir(l9660_dir *dir, l9660_dirent **pdirent);
l9660_status l9660_opendirat(l9660_dir *dir, l9660_dir *parent, const char *path);
l9660_status l9660_fs_open_root(l9660_dir *dir, l9660_fs *fs);
l9660_status l9660_openfs(l9660_fs *fs, bool (*read_sector)(l9660_fs *fs, void *buf, u32 sector),
                          u8        disk_number);
bool         CDROM_Read(u32 lba, u32 number, void *buffer, char drive);

bool read_sector(l9660_fs *fs, void *buf, u32 sector);
#define l9660_seekdir(dir, pos) (l9660_seek(&(dir)->file, L9660_SEEK_SET, (pos)))
#define l9660_telldir(dir)      (l9660_tell(&(dir)->file))

#define SEEK_END L9660_SEEK_END
#define SEEK_SET L9660_SEEK_SET
#define SEEK_CUR L9660_SEEK_CUR

#define DENT_EXISTS      (1 << 0)
#define DENT_ISDIR       (1 << 1)
#define DENT_ASSOCIATED  (1 << 2)
#define DENT_RECORD      (1 << 3)
#define DENT_PROTECTION  (1 << 4)
#define DENT_MULTIEXTENT (1 << 5)

#define PVD(vdesc) ((l9660_vdesc_primary *)(vdesc))

#ifdef L9660_BIG_ENDIAN
#  define READ16(v) (((v).be[1]) | ((v).be[0] << 8))
#  define READ32(v) (((v).be[3]) | ((v).be[2] << 8) | ((v).be[1]) << 16 | ((v).be[0] << 24))
#else
#  define READ16(v) (((v).le[0]) | ((v).le[1] << 8))
#  define READ32(v) (((v).le[0]) | ((v).le[1] << 8) | ((v).le[2]) << 16 | ((v).le[3] << 24))
#endif

#ifndef L9660_SINGLEBUFFER
#  define HAVEBUFFER(f) (true)
#  define BUF(f)        ((f)->buf)
#else
#  define HAVEBUFFER(f) ((f) == last_file)
#  define BUF(f)        (gbuf)
static l9660_file *last_file;
static char        gbuf[2048];
#endif

#define get_now_dir(vfs)  ((l9660_fs_status_t *)(vfs->cache))->now_dir
#define get_root_dir(vfs) ((l9660_fs_status_t *)(vfs->cache))->root_dir
static char *strchrnul(const char *s, int c) {
  while (*s) {
    if ((*s++) == c) break;
  }
  return (char *)s;
}

static inline u16 fsectoff(l9660_file *f) {
  return f->position % 2048;
}

static inline u32 fsector(l9660_file *f) {
  return f->position / 2048;
}

static inline u32 fnextsectpos(l9660_file *f) {
  return (f->position + 2047) & ~2047;
}

l9660_status l9660_openfs(l9660_fs *fs, bool (*read_sector)(l9660_fs *fs, void *buf, u32 sector),
                          u8        disk_number) {
  fs->read_sector = read_sector;
  fs->disk_number = disk_number;
#ifndef L9660_SINGLEBUFFER
  l9660_vdesc_primary *pvd = PVD(&fs->pvd);
#else
  last_file                   = NULL;
  l9660_vdesc_primary *pvd    = PVD(gbuf);
#endif
  u32 idx = 0x10;
  while (true) {
    // Read next sector
    if (!read_sector(fs, pvd, idx)) return L9660_EIO;

    // Validate magic
    if (memcmp(pvd->hdr.magic, "CD001", 5) != 0) return L9660_EBADFS;

    if (pvd->hdr.type == 1)
      break; // Found PVD
    else if (pvd->hdr.type == 255)
      return L9660_EBADFS;
  }

#ifdef L9660_SINGLEBUFFER
  memcpy(&fs->root_dir_ent, &pvd->root_dir_ent, pvd->root_dir_ent.length);
#endif

  return L9660_OK;
}

l9660_status l9660_fs_open_root(l9660_dir *dir, l9660_fs *fs) {
  l9660_file *f = &dir->file;
#ifndef L9660_SINGLEBUFFER
  l9660_dirent *dirent = &PVD(&fs->pvd)->root_dir_ent;
#else
  l9660_dirent        *dirent = &fs->root_dir_ent;
#endif

  f->fs           = fs;
  f->first_sector = READ32(dirent->sector);
  f->length       = READ32(dirent->size);
  f->position     = 0;

  return L9660_OK;
}

static l9660_status buffer(l9660_file *f) {
#ifdef L9660_SINGLEBUFFER
  last_file = f;
#endif

  if (!f->fs->read_sector(f->fs, BUF(f), f->first_sector + f->position / 2048))
    return L9660_EIO;
  else
    return L9660_OK;
}

static l9660_status prebuffer(l9660_file *f) {
  if (!HAVEBUFFER(f) || (f->position % 2048) == 0)
    return buffer(f);
  else
    return L9660_OK;
}

static l9660_status openat_raw(l9660_file *child, l9660_dir *parent, const char *name, bool isdir) {
  l9660_status  rv;
  l9660_dirent *dent = NULL;
  if ((rv = l9660_seekdir(parent, 0))) return rv;

  do {
    const char *seg = name;
    name            = strchrnul(name, '/');
    size_t seglen   = name - seg;

    /* ISO9660 stores '.' as '\0' */
    if (seglen == 1 && *seg == '.') seg = "\0";

    /* ISO9660 stores ".." as '\1' */
    if (seglen == 2 && seg[0] == '.' && seg[1] == '.') {
      seg    = "\1";
      seglen = 1;
    }

    while (true) {
      if ((rv = l9660_readdir(parent, &dent))) return rv;

      /* EOD */
      if (!dent) return L9660_ENOENT;

#ifdef DEBUG
      print_dirent(dent);
#endif

      /* wrong length */
      if (seglen > dent->name_len) continue;

      /* check name */
      if (memcmp(seg, dent->name, seglen) != 0) continue;

      /* check for a revision tag */
      if (dent->name_len > seglen && dent->name[seglen] != ';') continue;

      /* all tests pass */
      break;
    }

    child->fs             = parent->file.fs;
    child->first_sector   = READ32(dent->sector) + dent->xattr_length;
    child->length         = READ32(dent->size);
    child->position       = 0;
    parent->file.position = 0;
    if (*name && (dent->flags & DENT_ISDIR) != 0) return L9660_ENOTDIR;

    parent = (l9660_dir *)child;
  } while (*name);

  if (isdir) {
    if ((dent->flags & DENT_ISDIR) == 0) return L9660_ENOTDIR;
  } else {
    if ((dent->flags & DENT_ISDIR) != 0) return L9660_ENOTFILE;
  }

  return L9660_OK;
}

l9660_status l9660_opendirat(l9660_dir *dir, l9660_dir *parent, const char *path) {
  return openat_raw(&dir->file, parent, path, true);
}

static inline unsigned aligneven(unsigned v) {
  return v + (v & 1);
}

l9660_status l9660_readdir(l9660_dir *dir, l9660_dirent **pdirent) {
  l9660_status rv;
  l9660_file  *f = &dir->file;

rebuffer:
  if (f->position >= f->length) {
    *pdirent    = NULL;
    f->position = 0;
    return L9660_OK;
  }

  if ((rv = prebuffer(f))) return rv;
  char *off = BUF(f) + fsectoff(f);
  if (*off == 0) {
    // Padded end of sector
    f->position = fnextsectpos(f);
    goto rebuffer;
  }

  l9660_dirent *dirent  = (l9660_dirent *)off;
  f->position          += aligneven(dirent->length);

  *pdirent = dirent;
  return L9660_OK;
}

l9660_status l9660_openat(l9660_file *child, l9660_dir *parent, const char *name) {
  return openat_raw(child, parent, name, false);
}

/*! Seek the file to \p offset from \p whence */
l9660_status l9660_seek(l9660_file *f, int whence, int32_t offset) {
  l9660_status rv;
  u32          cursect = fsector(f);

  switch (whence) {
  case SEEK_SET: f->position = offset; break;

  case SEEK_CUR: f->position = f->position + offset; break;

  case SEEK_END: f->position = f->length - offset; break;
  }

  if (fsector(f) != cursect && fsectoff(f) != 0) {
    if ((rv = buffer(f))) return rv;
  }

  return L9660_OK;
}

u32 l9660_tell(l9660_file *f) {
  return f->position;
}

l9660_status l9660_read(l9660_file *f, void *buf, size_t size, size_t *read) {
  l9660_status rv;

  if ((rv = prebuffer(f))) return rv;

  u16 rem = 2048 - fsectoff(f);
  if (rem > f->length - f->position) rem = f->length - f->position;
  if (rem < size) size = rem;

  memcpy(buf, BUF(f) + fsectoff(f), size);

  *read        = size;
  f->position += size;

  return L9660_OK;
}
char *strdup(const char *s) {
  size_t l = strlen(s);
  char  *d = malloc(l + 1);
  if (!d) return NULL;
  return memcpy(d, s, l + 1);
}
bool read_sector(l9660_fs *fs, void *buf, u32 sector) {
  return CDROM_Read(sector, 1, buf, fs->disk_number);
}

bool ISO_Check(u8 disk_number) {
  u8  *buffer = malloc(2049); // 假设扇区大小为 2048 字节
  bool ok     = CDROM_Read(16, 1, buffer, disk_number);

  if (buffer[0] == 0x01 && buffer[1] == 'C' && buffer[2] == 'D' && buffer[3] == '0' &&
      buffer[4] == '0' && buffer[5] == '1') {
    free(buffer);
    return true; // 是 ISO9660 文件系统
  } else {
    free(buffer);
    return false; // 不是 ISO9660 文件系统
  }
}

void ISO_InitFs(struct vfs_t *vfs, u8 disk_number) {
  l9660_fs_status_t *fs_m;
  fs_m = (l9660_fs_status_t *)malloc(sizeof(l9660_fs_status_t));

  fs_m->fs   = (l9660_fs *)malloc(sizeof(l9660_fs));
  vfs->cache = (void *)fs_m;
  l9660_openfs(fs_m->fs, read_sector, disk_number);
  l9660_fs_open_root(&fs_m->root_dir, fs_m->fs);
  fs_m->now_dir = fs_m->root_dir;
  logk("%08x", vfs->cache);
}

void ISO_CDFile() {}

void ISO_CopyCache(struct vfs_t *dest, struct vfs_t *src) {
  dest->cache = malloc(sizeof(l9660_fs_status_t));
  memcpy(dest->cache, src->cache, sizeof(l9660_fs_status_t));
}

int ISO_cd(struct vfs_t *vfs, char *dictname) {
  //
  //    strtoupper(dictname);
  //
  if (strcmp(dictname, "/") == 0) {
    while (vfs->path->ctl->all != 0) {
      free((void *)(FindForCount(vfs->path->ctl->all, vfs->path)->val));
      DeleteVal(vfs->path->ctl->all, vfs->path);
    }
    l9660_fs_status_t *fs_m = (l9660_fs_status_t *)vfs->cache;
    get_now_dir(vfs)        = fs_m->root_dir;
    return 1;
  }
  int          free_flag = 0;
  l9660_dir    finfo;
  l9660_status a;
RE:
  a = l9660_opendirat(&finfo, &get_now_dir(vfs), dictname);
  if (a) {
    if (free_flag) {
      free(dictname);
    } else {
      dictname = strdup(dictname);
      strtoupper(dictname);
      free_flag = 1;
      goto RE;
    }
    return 0;
  }
  get_now_dir(vfs) = finfo;
  if (strcmp(dictname, "..") != 0 && strcmp(dictname, ".") != 0) {
    char *dict = malloc(255);
    strcpy(dict, dictname);
    AddVal((uintptr_t)dict, vfs->path);
  }

  if (strcmp(dictname, "..") == 0) {
    free((void *)(FindForCount(vfs->path->ctl->all, vfs->path)->val));
    DeleteVal(vfs->path->ctl->all, vfs->path);
  }
  if (free_flag) { free(dictname); }
  return 1;
}

bool ISO_ReadFile(struct vfs_t *vfs, char *path, char *buffer) {
  l9660_file   file;
  l9660_status a;
  int          free_flag = 0;
RE:
  a = l9660_openat(&file, &get_now_dir(vfs), path);
  if (a) {
    if (free_flag) {
      free(path);
    } else {
      path = strdup(path);
      strtoupper(path);
      free_flag = 1;
      goto RE;
    }
    return false; // not found
  }
  while (true) {
    size_t read;
    l9660_read(&file, buffer, 128, &read);
    if (read == 0) break;
    buffer += read;
  }
  if (free_flag) { free(path); }
  return true;
}

List *ISO_ListFile(struct vfs_t *vfs, char *dictpath) {
  l9660_dir finfo;
  int       free_flag = 0;
  if (strcmp(dictpath, "") == 0)
    finfo = get_now_dir(vfs);
  else {
    l9660_status a;
  RE:
    a = l9660_opendirat(&finfo, &get_now_dir(vfs), dictpath);
    if (a) {
      if (free_flag) {
        free(dictpath);
      } else {
        dictpath = strdup(dictpath);
        strtoupper(dictpath);
        free_flag = 1;
        goto RE;
      }
      return NULL;
    }
  }

  List *result = NewList();
  while (true) {
    l9660_dirent *dent;
    l9660_readdir(&finfo, &dent);

    if (dent == 0) break;
    vfs_file *d = malloc(sizeof(vfs_file));
    clean((void *)d, sizeof(vfs_file));
    int j = 0;
    if (memcmp("\0", dent->name, dent->name_len) == 0) {
      if (finfo.file.first_sector == get_root_dir(vfs).file.first_sector) {
        free(d);
        continue;
      }
      d->name[j++] = '.';
      d->name[j]   = 0;
    } else if (memcmp("\1", dent->name, dent->name_len) == 0) {
      if (finfo.file.first_sector == get_root_dir(vfs).file.first_sector) {
        free(d);
        continue;
      }
      d->name[j++] = '.';
      d->name[j++] = '.';
      d->name[j]   = 0;
    } else
      for (; j < dent->name_len; j++) {
        if (dent->name[j] == ';') { break; }

        d->name[j] = dent->name[j];
      }
    d->name[j] = 0;
    d->type    = FLE;
    if (dent->flags & DENT_ISDIR) {
      d->type = DIR;
    } else {
      d->size = READ32(dent->size);
    }
    AddVal((uintptr_t)d, result);
  }
  if (free_flag) { free(dictpath); }
  return result;
}
int ISO_FileSize(struct vfs_t *vfs, char *filename) {
  l9660_file   file;
  l9660_status a;
  int          free_flag = 0;
RE:
  a = l9660_openat(&file, &get_now_dir(vfs), filename);
  if (a) {
    if (free_flag) {
      free(filename);
    } else {
      filename = strdup(filename);
      strtoupper(filename);
      free_flag = 1;
      goto RE;
    }
    return -1; // not found
  }
  if (free_flag) { free(filename); }
  return file.length;
}

void init_iso9660() {
  vfs_t fs;
  fs.flag  = 1;
  fs.cache = NULL;
  strcpy(fs.FSName, "ISO9660");

  fs.CopyCache  = ISO_CopyCache;
  fs.Check      = ISO_Check;
  fs.InitFs     = ISO_InitFs;
  fs.CreateFile = NULL;
  fs.CreateDict = NULL;
  fs.DelDict    = NULL;
  fs.DelFile    = NULL;
  fs.cd         = ISO_cd;
  fs.ReadFile   = ISO_ReadFile;
  fs.ListFile   = ISO_ListFile;
  fs.FileSize   = ISO_FileSize;

  vfs_register_fs(fs);
}