#include <dos.h>
#include <limits.h>
#include <fs.h>
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
  uint8_t le[2];
} l9660_luint16;
typedef struct {
  uint8_t be[2];
} l9660_buint16;
typedef struct {
  uint8_t le[2], be[2];
} l9660_duint16;
typedef struct {
  uint8_t le[4];
} l9660_luint32;
typedef struct {
  uint8_t be[4];
} l9660_buint32;
typedef struct {
  uint8_t le[4], be[4];
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
  uint8_t length;
  uint8_t xattr_length;
  l9660_duint32 sector;
  l9660_duint32 size;
  l9660_filetime time;
  uint8_t flags;
  uint8_t unit_size;
  uint8_t gap_size;
  l9660_duint16 vol_seq_number;
  uint8_t name_len;
  char name[/*name_len*/];
} l9660_dirent;

/* Volume descriptor header */
typedef struct {
  uint8_t type;
  char magic[5];
  uint8_t version;
} l9660_vdesc_header;

/* Primary volume descriptor */
typedef struct {
  l9660_vdesc_header hdr;
  char pad0[1];
  char system_id[32];
  char volume_id[32];
  char pad1[8];
  l9660_duint32 volume_space_size;
  char pad2[32];
  l9660_duint16 volume_set_size;
  l9660_duint16 volume_seq_number;
  l9660_duint16 logical_block_size;
  l9660_duint32 path_table_size;
  l9660_luint32 path_table_le;
  l9660_luint32 path_table_opt_le;
  l9660_buint32 path_table_be;
  l9660_buint32 path_table_opt_be;
  union {
    l9660_dirent root_dir_ent;
    char pad3[34];
  };
  char volume_set_id[128];
  char data_preparer_id[128];
  char app_id[128];
  char copyright_file[38];
  char abstract_file[36];
  char bibliography_file[37];
  l9660_desctime volume_created, volume_modified, volume_expires,
      volume_effective;
  uint8_t file_structure_version;
  char pad4[1];
  char app_reserved[512];
  char reserved[653];
} l9660_vdesc_primary;

/* A generic volume descriptor (i.e. 2048 bytes) */
typedef union {
  l9660_vdesc_header hdr;
  char _bits[2048];
} l9660_vdesc;

typedef struct l9660_fs {
#ifdef L9660_SINGLEBUFFER
  union {
    l9660_dirent root_dir_ent;
    char root_dir_pad[34];
  };
#else
  /* Sector buffer to hold the PVD */
  l9660_vdesc pvd;
#endif

  /* read_sector func */
  bool (*read_sector)(struct l9660_fs *fs, void *buf, uint32_t sector);
  int disk_number;
} l9660_fs;

typedef struct {
#ifndef L9660_SINGLEBUFFER
  /* single sector buffer */
  char buf[2048];
#endif
  l9660_fs *fs;
  uint32_t first_sector;
  uint32_t position;
  uint32_t length;
} l9660_file;

typedef struct {
  /* directories are mostly just files with special accessors, but we like type
   * safetey */
  l9660_file file;
} l9660_dir;

typedef struct l9660_fs_status {
  l9660_fs *fs;
  l9660_dir root_dir;

} l9660_fs_status_t;
#define L9660_SEEK_END -1
#define L9660_SEEK_SET 0
#define L9660_SEEK_CUR +1

uint32_t l9660_tell(l9660_file *f);
l9660_status l9660_read(l9660_file *f, void *buf, size_t size, size_t *read);
l9660_status l9660_seek(l9660_file *f, int whence, int32_t offset);
l9660_status l9660_openat(l9660_file *child, l9660_dir *parent,
                          const char *name);
l9660_status l9660_readdir(l9660_dir *dir, l9660_dirent **pdirent);
l9660_status l9660_opendirat(l9660_dir *dir, l9660_dir *parent,
                             const char *path);
l9660_status l9660_fs_open_root(l9660_dir *dir, l9660_fs *fs);
l9660_status l9660_openfs(l9660_fs *fs,
                          bool (*read_sector)(l9660_fs *fs, void *buf,
                                              uint32_t sector),
                          uint8_t disk_number);
bool CDROM_Read(unsigned int lba, unsigned int number, void *buffer,
                char drive);

bool read_sector(l9660_fs *fs, void *buf, uint32_t sector);
#define l9660_seekdir(dir, pos)                                                \
  (l9660_seek(&(dir)->file, L9660_SEEK_SET, (pos)))
#define l9660_telldir(dir) (l9660_tell(&(dir)->file))

#define DENT_EXISTS (1 << 0)
#define DENT_ISDIR (1 << 1)
#define DENT_ASSOCIATED (1 << 2)
#define DENT_RECORD (1 << 3)
#define DENT_PROTECTION (1 << 4)
#define DENT_MULTIEXTENT (1 << 5)

#define PVD(vdesc) ((l9660_vdesc_primary *)(vdesc))

#ifdef L9660_BIG_ENDIAN
#define READ16(v) (((v).be[1]) | ((v).be[0] << 8))
#define READ32(v)                                                              \
  (((v).be[3]) | ((v).be[2] << 8) | ((v).be[1]) << 16 | ((v).be[0] << 24))
#else
#define READ16(v) (((v).le[0]) | ((v).le[1] << 8))
#define READ32(v)                                                              \
  (((v).le[0]) | ((v).le[1] << 8) | ((v).le[2]) << 16 | ((v).le[3] << 24))
#endif

#ifndef L9660_SINGLEBUFFER
#define HAVEBUFFER(f) (true)
#define BUF(f) ((f)->buf)
#else
#define HAVEBUFFER(f) ((f) == last_file)
#define BUF(f) (gbuf)
static l9660_file *last_file;
static char gbuf[2048];
#endif

static char *strchrnul(const char *s, int c) {
  while (*s) {
    if ((*s++) == c)
      break;
  }
  return (char *)s;
}

static inline uint16_t fsectoff(l9660_file *f) { return f->position % 2048; }

static inline uint32_t fsector(l9660_file *f) { return f->position / 2048; }

static inline uint32_t fnextsectpos(l9660_file *f) {
  return (f->position + 2047) & ~2047;
}

l9660_status l9660_openfs(l9660_fs *fs,
                          bool (*read_sector)(l9660_fs *fs, void *buf,
                                              uint32_t sector),
                          uint8_t disk_number) {
  fs->read_sector = read_sector;
  fs->disk_number = disk_number;
#ifndef L9660_SINGLEBUFFER
  l9660_vdesc_primary *pvd = PVD(&fs->pvd);
#else
  last_file = NULL;
  l9660_vdesc_primary *pvd = PVD(gbuf);
#endif
  uint32_t idx = 0x10;
  for (;;) {
    // Read next sector
    if (!read_sector(fs, pvd, idx))
      return L9660_EIO;

    // Validate magic
    if (memcmp(pvd->hdr.magic, "CD001", 5) != 0)
      return L9660_EBADFS;

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
  l9660_dirent *dirent = &fs->root_dir_ent;
#endif

  f->fs = fs;
  f->first_sector = READ32(dirent->sector);
  f->length = READ32(dirent->size);
  f->position = 0;

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

static l9660_status openat_raw(l9660_file *child, l9660_dir *parent,
                               const char *name, bool isdir) {
  l9660_status rv;
  l9660_dirent *dent = NULL;
  if ((rv = l9660_seekdir(parent, 0)))
    return rv;

  do {
    const char *seg = name;
    name = strchrnul(name, '/');
    size_t seglen = name - seg;

    /* ISO9660 stores '.' as '\0' */
    if (seglen == 1 && *seg == '.')
      seg = "\0";

    /* ISO9660 stores ".." as '\1' */
    if (seglen == 2 && seg[0] == '.' && seg[1] == '.') {
      seg = "\1";
      seglen = 1;
    }

    for (;;) {
      if ((rv = l9660_readdir(parent, &dent)))
        return rv;

      /* EOD */
      if (!dent)
        return L9660_ENOENT;

#ifdef DEBUG
      print_dirent(dent);
#endif

      /* wrong length */
      if (seglen > dent->name_len)
        continue;

      /* check name */
      if (memcmp(seg, dent->name, seglen) != 0)
        continue;

      /* check for a revision tag */
      if (dent->name_len > seglen && dent->name[seglen] != ';')
        continue;

      /* all tests pass */
      break;
    }

    child->fs = parent->file.fs;
    child->first_sector = READ32(dent->sector) + dent->xattr_length;
    child->length = READ32(dent->size);
    child->position = 0;
    parent->file.position = 0;
    if (*name && (dent->flags & DENT_ISDIR) != 0)
      return L9660_ENOTDIR;

    parent = (l9660_dir *)child;
  } while (*name);

  if (isdir) {
    if ((dent->flags & DENT_ISDIR) == 0)
      return L9660_ENOTDIR;
  } else {
    if ((dent->flags & DENT_ISDIR) != 0)
      return L9660_ENOTFILE;
  }

  return L9660_OK;
}

l9660_status l9660_opendirat(l9660_dir *dir, l9660_dir *parent,
                             const char *path) {
  return openat_raw(&dir->file, parent, path, true);
}

static inline unsigned aligneven(unsigned v) { return v + (v & 1); }

l9660_status l9660_readdir(l9660_dir *dir, l9660_dirent **pdirent) {
  l9660_status rv;
  l9660_file *f = &dir->file;

rebuffer:
  if (f->position >= f->length) {
    *pdirent = NULL;
    f->position = 0;
    return L9660_OK;
  }

  if ((rv = prebuffer(f)))
    return rv;
  char *off = BUF(f) + fsectoff(f);
  if (*off == 0) {
    // Padded end of sector
    f->position = fnextsectpos(f);
    goto rebuffer;
  }

  l9660_dirent *dirent = (l9660_dirent *)off;
  unsigned record_length = aligneven(dirent->length);
  unsigned sector_remaining = 2048 - fsectoff(f);
  if (dirent->length < sizeof(l9660_dirent) ||
      dirent->name_len > dirent->length - sizeof(l9660_dirent) ||
      record_length > sector_remaining || record_length > f->length - f->position) {
    return L9660_EBADFS;
  }
  f->position += record_length;

  *pdirent = dirent;
  return L9660_OK;
}

l9660_status l9660_openat(l9660_file *child, l9660_dir *parent,
                          const char *name) {
  return openat_raw(child, parent, name, false);
}

/*! Seek the file to \p offset from \p whence */
l9660_status l9660_seek(l9660_file *f, int whence, int32_t offset) {
  l9660_status rv;
  uint32_t cursect = fsector(f);

  switch (whence) {
  case L9660_SEEK_SET:
    f->position = offset;
    break;

  case L9660_SEEK_CUR:
    f->position = f->position + offset;
    break;

  case L9660_SEEK_END:
    f->position = f->length - offset;
    break;
  }

  if (fsector(f) != cursect && fsectoff(f) != 0) {
    if ((rv = buffer(f)))
      return rv;
  }

  return L9660_OK;
}

uint32_t l9660_tell(l9660_file *f) { return f->position; }

l9660_status l9660_read(l9660_file *f, void *buf, size_t size, size_t *read) {
  l9660_status rv;
  if (f == NULL || buf == NULL || read == NULL) {
    return L9660_EBADFS;
  }
  *read = 0;
  if (size == 0 || f->position >= f->length) {
    return L9660_OK;
  }

  if ((rv = prebuffer(f)))
    return rv;

  uint16_t rem = 2048 - fsectoff(f);
  if (rem > f->length - f->position)
    rem = f->length - f->position;
  if (rem < size)
    size = rem;

  memcpy(buf, BUF(f) + fsectoff(f), size);

  *read = size;
  f->position += size;

  return L9660_OK;
}
char *strdup(const char *s) {
  size_t l = strlen(s);
  char *d = malloc(l + 1);
  if (!d)
    return NULL;
  return memcpy(d, s, l + 1);
}
bool read_sector(l9660_fs *fs, void *buf, uint32_t sector) {
  return CDROM_Read(sector, 1, buf, fs->disk_number);
}

bool ISO_check(uint8_t disk_number) {
  unsigned char *buffer = malloc(2049); // 假设扇区大小为 2048 字节
  if (buffer == NULL) {
    return false;
  }
  bool ok = CDROM_Read(16, 1, buffer, disk_number);

  if (ok && buffer[0] == 0x01 && buffer[1] == 'C' && buffer[2] == 'D' &&
      buffer[3] == '0' && buffer[4] == '0' && buffer[5] == '1') {
    free(buffer);
    return true; // 是 ISO9660 文件系统
  } else {
    free(buffer);
    return false; // 不是 ISO9660 文件系统
  }
}


static l9660_fs_status_t *iso_state(vfs_t *vfs) {
  return (l9660_fs_status_t *)vfs_mount_data(vfs);
}

static int iso_normalize_name(const char *name, size_t length,
                              char *normalized, size_t capacity) {
  if (name == NULL || normalized == NULL || length == 0 || length >= 255 ||
      capacity <= length) {
    return VFS_ERROR_INVALID;
  }
  for (size_t index = 0; index < length; index++) {
    char ch = name[index];
    normalized[index] = ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
  }
  normalized[length] = '\0';
  return VFS_OK;
}

static void iso_file_from_node(vfs_t *vfs, const vfs_node_t *node,
                               l9660_file *file) {
  memset(file, 0, sizeof(*file));
  file->fs = iso_state(vfs)->fs;
  file->first_sector = node->id.value[1];
  file->length = node->id.value[2];
}

static void iso_node_from_file(const l9660_file *file, bool directory,
                               vfs_node_t *node) {
  memset(node, 0, sizeof(*node));
  node->id.value[0] = directory ? 1 : 2;
  node->id.value[1] = file->first_sector;
  node->id.value[2] = file->length;
  node->type = directory ? VFS_NODE_DIRECTORY : VFS_NODE_FILE;
  node->attributes = directory ? DIR : RDO;
  node->size = directory ? 0 : file->length;
}

static int iso_mount(vfs_t *vfs) {
  l9660_fs_status_t *state = malloc(sizeof(*state));
  if (state == NULL) {
    return VFS_ERROR_NO_MEMORY;
  }
  memset(state, 0, sizeof(*state));
  state->fs = malloc(sizeof(*state->fs));
  if (state->fs == NULL) {
    free(state);
    return VFS_ERROR_NO_MEMORY;
  }
  vfs_mount_set_data(vfs, state);
  if (l9660_openfs(state->fs, read_sector, vfs_mount_disk_number(vfs)) !=
          L9660_OK ||
      l9660_fs_open_root(&state->root_dir, state->fs) != L9660_OK) {
    free(state->fs);
    free(state);
    vfs_mount_set_data(vfs, NULL);
    return VFS_ERROR_IO;
  }
  return VFS_OK;
}

static void iso_unmount(vfs_t *vfs) {
  l9660_fs_status_t *state = iso_state(vfs);
  if (state == NULL) {
    return;
  }
  free(state->fs);
  free(state);
  vfs_mount_set_data(vfs, NULL);
}

static int iso_root(vfs_t *vfs, vfs_node_t *node) {
  iso_node_from_file(&iso_state(vfs)->root_dir.file, true, node);
  return VFS_OK;
}

static int iso_lookup(vfs_t *vfs, const vfs_node_t *directory_node,
                      const char *name, vfs_node_t *node) {
  if (directory_node == NULL || directory_node->type != VFS_NODE_DIRECTORY ||
      directory_node->id.value[0] != 1) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  l9660_dir parent;
  iso_file_from_node(vfs, directory_node, &parent.file);
  l9660_dir directory;
  if (l9660_opendirat(&directory, &parent, name) == L9660_OK) {
    iso_node_from_file(&directory.file, true, node);
    return VFS_OK;
  }
  l9660_file file;
  if (l9660_openat(&file, &parent, name) != L9660_OK) {
    return VFS_ERROR_NO_ENTRY;
  }
  iso_node_from_file(&file, false, node);
  return VFS_OK;
}

static int iso_read(vfs_t *vfs, const vfs_node_t *node, uint32_t offset,
                    void *buffer, uint32_t length) {
  if (node == NULL || node->type != VFS_NODE_FILE || offset > node->size) {
    return VFS_ERROR_NO_ENTRY;
  }
  if (length > node->size - offset) {
    length = node->size - offset;
  }
  l9660_file file;
  iso_file_from_node(vfs, node, &file);
  if (l9660_seek(&file, L9660_SEEK_SET, offset) != L9660_OK) {
    return VFS_ERROR_IO;
  }
  size_t completed = 0;
  while (completed < length) {
    uint32_t remaining = length - completed;
    if (fsectoff(&file) == 0 && remaining >= 2048) {
      uint32_t sectors = remaining / 2048;
      if (!CDROM_Read(file.first_sector + fsector(&file), sectors,
                      (uint8_t *)buffer + completed,
                      vfs_mount_disk_number(vfs))) {
        return completed == 0 ? VFS_ERROR_IO : (int)completed;
      }
      uint32_t bytes = sectors * 2048;
      file.position += bytes;
      completed += bytes;
      continue;
    }
    size_t read = 0;
    if (l9660_read(&file, (uint8_t *)buffer + completed, remaining, &read) !=
        L9660_OK) {
      return completed == 0 ? VFS_ERROR_IO : (int)completed;
    }
    if (read == 0) {
      break;
    }
    completed += read;
  }
  return completed;
}

static int iso_iterate(vfs_t *vfs, const vfs_node_t *directory_node,
                       uint32_t wanted, vfs_dir_entry_t *entry) {
  if (directory_node == NULL || directory_node->type != VFS_NODE_DIRECTORY) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  l9660_dir directory;
  iso_file_from_node(vfs, directory_node, &directory.file);
  uint32_t visible = 0;
  for (;;) {
    l9660_dirent *dent = NULL;
    if (l9660_readdir(&directory, &dent) != L9660_OK) {
      return VFS_ERROR_IO;
    }
    if (dent == NULL) {
      return 0;
    }
    if ((dent->name_len == 1 &&
         (dent->name[0] == '\0' || dent->name[0] == '\1'))) {
      continue;
    }
    if (visible++ != wanted) {
      continue;
    }
    uint32_t length = 0;
    while (length < dent->name_len && dent->name[length] != ';' &&
           length < sizeof(entry->name) - 1) {
      entry->name[length] = dent->name[length];
      length++;
    }
    entry->name[length] = '\0';
    l9660_file file = {
        .fs = iso_state(vfs)->fs,
        .first_sector = READ32(dent->sector),
        .length = READ32(dent->size),
    };
    iso_node_from_file(&file, (dent->flags & DENT_ISDIR) != 0, &entry->node);
    return 1;
  }
}

void init_iso9660(void) {
  static const vfs_filesystem_t filesystem = {
      .name = "ISO9660",
      .check = ISO_check,
      .mount = iso_mount,
      .unmount = iso_unmount,
      .root = iso_root,
      .normalize_name = iso_normalize_name,
      .lookup = iso_lookup,
      .read = iso_read,
      .iterate = iso_iterate,
  };
  vfs_register_fs(&filesystem);
}
