// fat.c : fat文件系统的实现
#include <dos.h>
#include <fs.h>
#include <limits.h>
typedef struct {
  struct FAT_CACHE dm;
  uint32_t root_cluster;
} fat_state_t;

#define fat_state(vfs) ((fat_state_t *)vfs_mount_data(vfs))
#define get_dm(vfs) (fat_state(vfs)->dm)

static void fat_delete_fs(struct vfs_mount *vfs);
void file_savefat(int *fat, int clustno, int length, vfs_t *vfs);
static uint32_t fat_eoc_marker(int type) {
  if (type == 12) {
    return 0x0fffu;
  }
  if (type == 16) {
    return 0xffffu;
  }
  return 0x0fffffffu;
}
static bool fat_is_eoc(int type, uint32_t value) {
  if (type == 12) {
    return (value & 0x0fffu) >= 0x0ff8u;
  }
  if (type == 16) {
    return (value & 0xffffu) >= 0xfff8u;
  }
  return (value & 0x0fffffffu) >= 0x0ffffff8u;
}
static bool fat_data_cluster_valid(vfs_t *vfs, int cluster) {
  if (cluster < 2 || cluster >= get_dm(vfs).FatMaxTerms ||
      get_dm(vfs).SectorBytes != 512 || get_dm(vfs).ClustnoBytes == 0) {
    return false;
  }
  uint64_t start = (uint64_t)get_dm(vfs).FileDataAddress +
                   (uint64_t)(cluster - 2) * get_dm(vfs).ClustnoBytes;
  return start + get_dm(vfs).ClustnoBytes <= get_dm(vfs).imgTotalSize;
}
static int fat_find_free_cluster(vfs_t *vfs) {
  for (int cluster = 2; cluster < get_dm(vfs).FatMaxTerms; cluster++) {
    if (get_dm(vfs).fat[cluster] == 0 &&
        fat_data_cluster_valid(vfs, cluster)) {
      return cluster;
    }
  }
  return -1;
}
static bool fat_chain_length(vfs_t *vfs, int start, int *length) {
  int current = start;
  int count = 1;
  while (true) {
    if (!fat_data_cluster_valid(vfs, current)) {
      return false;
    }
    if (fat_is_eoc(get_dm(vfs).type, get_dm(vfs).fat[current])) {
      *length = count;
      return true;
    }
    if (count >= get_dm(vfs).FatMaxTerms) {
      return false;
    }
    current = get_dm(vfs).fat[current];
    count++;
  }
}
static bool fat_chain_last(vfs_t *vfs, int start, int *last) {
  int current = start;
  for (int count = 0; count < get_dm(vfs).FatMaxTerms; count++) {
    if (!fat_data_cluster_valid(vfs, current)) {
      return false;
    }
    if (fat_is_eoc(get_dm(vfs).type, get_dm(vfs).fat[current])) {
      *last = current;
      return true;
    }
    current = get_dm(vfs).fat[current];
  }
  return false;
}
static inline int get_fat_date(unsigned short year, unsigned short month,
                               unsigned short day) {
  year -= 1980;
  unsigned short date = 0;
  date |= (year & 0x7f) << 9;
  date |= (month & 0x0f) << 5;
  date |= (day & 0x1f);
  return date;
}
static inline int get_fat_time(unsigned short hour, unsigned short minute) {
  unsigned short time = 0;
  time |= (hour & 0x1f) << 11;
  time |= (minute & 0x3f) << 5;
  return time;
}
static inline unsigned short fat_read_u16(const void *ptr) {
  const unsigned char *p = ptr;
  return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}
static inline unsigned int fat_read_u32(const void *ptr) {
  const unsigned char *p = ptr;
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
         ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static inline int fat_name_matches(const struct FAT_FILEINFO *info,
                                   const char target[11]) {
  return memcmp(info->name, target, 8) == 0 &&
         memcmp(info->ext, target + 8, 3) == 0;
}
static bool fat_is_reserved_marker(int type, uint32_t value) {
  if (type == 12) {
    return value >= 0x0ff0u && value < 0x0ff8u;
  }
  if (type == 16) {
    return value >= 0xfff0u && value < 0xfff8u;
  }
  return value >= 0x0ffffff0u && value < 0x0ffffff8u;
}
static bool read_fat(const unsigned char *img, size_t img_size, int *fat,
                     unsigned char *allocated, int count, int type) {
  if (img == NULL || fat == NULL || allocated == NULL || count < 2) {
    return false;
  }
  memset(allocated, 0, count);
  for (int i = 0; i < count; i++) {
    size_t offset;
    uint32_t value;
    if (type == 12) {
      offset = (size_t)i + (size_t)i / 2;
      if (offset + 1 >= img_size) {
        return false;
      }
      value = i & 1 ? ((uint32_t)img[offset] >> 4) |
                          ((uint32_t)img[offset + 1] << 4)
                    : (uint32_t)img[offset] |
                          ((uint32_t)img[offset + 1] << 8);
      value &= 0x0fffu;
    } else if (type == 16) {
      offset = (size_t)i * 2;
      if (offset + 1 >= img_size) {
        return false;
      }
      value = fat_read_u16(img + offset);
    } else if (type == 32) {
      offset = (size_t)i * 4;
      if (offset + 3 >= img_size) {
        return false;
      }
      value = fat_read_u32(img + offset) & 0x0fffffffu;
    } else {
      return false;
    }
    fat[i] = (int)value;
    allocated[i] = value != 0;
    if (value != 0 && !fat_is_eoc(type, value) &&
        !fat_is_reserved_marker(type, value) &&
        (value < 2 || value >= (uint32_t)count)) {
      return false;
    }
  }
  allocated[0] = true;
  allocated[1] = true;
  return true;
}
int get_directory_max(struct FAT_FILEINFO *directory, vfs_t *vfs) {
  if (directory == get_dm(vfs).root_directory) {
    return get_dm(vfs).RootMaxFiles;
  }
  for (int i = 1; list_get(i, get_dm(vfs).directory_list) != NULL; i++) {
    struct List *l = list_get(i, get_dm(vfs).directory_list);
    if ((struct FAT_FILEINFO *)l->val == directory) {
      return (int)list_get(i, get_dm(vfs).directory_max_list)->val;
    }
  }
  return 0;
}
void file_saveinfo(struct FAT_FILEINFO *directory, vfs_t *vfs) {
  if (directory == get_dm(vfs).root_directory && get_dm(vfs).type == 32) {
    int cluster = fat_state(vfs)->root_cluster;
    unsigned count = get_dm(vfs).RootMaxFiles * 32 / get_dm(vfs).ClustnoBytes;
    for (unsigned i = 0; i < count; i++) {
      if (!fat_data_cluster_valid(vfs, cluster)) {
        return;
      }
      disk_write((get_dm(vfs).FileDataAddress +
                  (cluster - 2) * get_dm(vfs).ClustnoBytes) /
                     get_dm(vfs).SectorBytes,
                 get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                 (char *)directory + i * get_dm(vfs).ClustnoBytes,
                 vfs_mount_disk_number(vfs));
      cluster = get_dm(vfs).fat[cluster];
    }
  } else if (directory == get_dm(vfs).root_directory) {
    disk_write(get_dm(vfs).RootDictAddress / get_dm(vfs).SectorBytes,
               get_dm(vfs).RootMaxFiles * 32 / get_dm(vfs).SectorBytes,
               (void *)directory, vfs_mount_disk_number(vfs));
  } else {
    for (int i = 1; list_get(i, get_dm(vfs).directory_list) != NULL; i++) {
      struct List *list = list_get(i, get_dm(vfs).directory_list);
      if (list->val == (uintptr_t)directory) {
        list = list_get(i, get_dm(vfs).directory_clustno_list);
        int k = (int)list_get(i, get_dm(vfs).directory_max_list)->val * 32 /
                get_dm(vfs).ClustnoBytes;
        for (int j = list->val, l = 0; l != k; l++) {
          if (!fat_data_cluster_valid(vfs, j)) {
            return;
          }
          disk_write((get_dm(vfs).FileDataAddress +
                      (j - 2) * get_dm(vfs).ClustnoBytes) /
                         get_dm(vfs).SectorBytes,
                     get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                     (char *)directory + l * get_dm(vfs).ClustnoBytes,
                     vfs_mount_disk_number(vfs));
          j = get_dm(vfs).fat[j];
        }
        break;
      }
    }
  }
}
void file_savefat(int *fat, int clustno, int count, vfs_t *vfs) {
  if (fat == NULL || vfs == NULL || clustno < 0 || count <= 0 ||
      clustno > get_dm(vfs).FatMaxTerms - count) {
    return;
  }
  unsigned char *img =
      (unsigned char *)(uintptr_t)get_dm(vfs).ADR_DISKIMG +
      get_dm(vfs).Fat1Address;
  size_t first_byte;
  size_t end_byte;
  if (get_dm(vfs).type == 12) {
    for (int i = 0; i < count; i++) {
      if ((clustno + i) % 2 == 0) {
        img[(clustno + i) * 3 / 2 + 0] = fat[clustno + i] & 0xff;
        img[(clustno + i) * 3 / 2 + 1] =
            (fat[clustno + i] >> 8 | (img[(clustno + i) * 3 / 2 + 1] & 0xf0)) &
            0xff;
      } else if ((clustno + i) % 2 != 0) {
        img[(clustno + i - 1) * 3 / 2 + 1] =
            ((img[(clustno + i - 1) * 3 / 2 + 1] & 0x0f) | fat[clustno + i]
                                                               << 4) &
            0xff;
        img[(clustno + i - 1) * 3 / 2 + 2] = (fat[clustno + i] >> 4) & 0xff;
      }
    }
    first_byte = (size_t)clustno * 3 / 2;
    end_byte = ((size_t)(clustno + count) * 3 + 1) / 2;
  } else if (get_dm(vfs).type == 16) {
    for (int i = 0; i < count; i++) {
      img[(clustno + i) * 2 + 0] = fat[clustno + i] & 0xff;
      img[(clustno + i) * 2 + 1] = (fat[clustno + i] >> 8) & 0xff;
    }
    first_byte = (size_t)clustno * 2;
    end_byte = (size_t)(clustno + count) * 2;
  } else if (get_dm(vfs).type == 32) {
    for (int i = 0; i < count; i++) {
      img[(clustno + i) * 4 + 0] = fat[clustno + i] & 0xff;
      img[(clustno + i) * 4 + 1] = (fat[clustno + i] >> 8) & 0xff;
      img[(clustno + i) * 4 + 2] = (fat[clustno + i] >> 16) & 0xff;
      img[(clustno + i) * 4 + 3] = fat[clustno + i] >> 24;
    }
    first_byte = (size_t)clustno * 4;
    end_byte = (size_t)(clustno + count) * 4;
  } else {
    return;
  }
  size_t first_sector = first_byte / get_dm(vfs).SectorBytes;
  size_t sector_count =
      (end_byte + get_dm(vfs).SectorBytes - 1) / get_dm(vfs).SectorBytes -
      first_sector;
  void *source = img + first_sector * get_dm(vfs).SectorBytes;
  disk_write(get_dm(vfs).Fat1Address / get_dm(vfs).SectorBytes + first_sector,
             sector_count, source, vfs_mount_disk_number(vfs));
  disk_write(get_dm(vfs).Fat2Address / get_dm(vfs).SectorBytes + first_sector,
             sector_count, source, vfs_mount_disk_number(vfs));
}
enum { FAT_LFN_UNITS = 255, FAT_NAME_BYTES = 255 };
static const uint8_t fat_lfn_offsets[13] = {1,  3,  5,  7,  9,  14, 16,
                                            18, 20, 22, 24, 28, 30};

typedef struct {
  uint16_t units[FAT_LFN_UNITS];
  unsigned length;
  unsigned slots;
  char alias[11];
  uint8_t case_flags;
} fat_name_t;

static unsigned char fat_upper(unsigned char ch) {
  return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

static bool fat_name_equal(const char *a, const char *b) {
  while (*a && fat_upper(*a) == fat_upper(*b)) {
    a++;
    b++;
  }
  return fat_upper(*a) == fat_upper(*b);
}

/* FAT exposes UTF-8 within the existing VFS directory-name capacity. */
static bool fat_decode_name(const char *name, fat_name_t *result) {
  size_t bytes = strlen(name);
  if (!bytes || bytes >= FAT_NAME_BYTES || name[bytes - 1] == ' ' ||
      name[bytes - 1] == '.') {
    return false;
  }
  result->length = 0;
  const uint8_t *p = (const uint8_t *)name;
  while (*p) {
    uint32_t ch = *p++;
    unsigned count = 0;
    uint32_t minimum = 0;
    if (ch >= 0x80) {
      if (ch >= 0xc2 && ch <= 0xdf) {
        count = 1;
        ch &= 31;
        minimum = 0x80;
      } else if (ch >= 0xe0 && ch <= 0xef) {
        count = 2;
        ch &= 15;
        minimum = 0x800;
      } else if (ch >= 0xf0 && ch <= 0xf4) {
        count = 3;
        ch &= 7;
        minimum = 0x10000;
      } else {
        return false;
      }
      while (count--) {
        if ((*p & 0xc0) != 0x80) {
          return false;
        }
        ch = (ch << 6) | (*p++ & 63);
      }
      if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff)) {
        return false;
      }
    }
    if (ch < 32 || (ch < 128 && strchr("\"*/:<>?\\|", ch))) {
      return false;
    }
    unsigned needed = ch >= 0x10000 ? 2 : 1;
    if (result->length + needed > FAT_LFN_UNITS) {
      return false;
    }
    if (needed == 2) {
      ch -= 0x10000;
      result->units[result->length++] = 0xd800 | (ch >> 10);
      ch = 0xdc00 | (ch & 1023);
    }
    result->units[result->length++] = ch;
  }
  return true;
}

static uint8_t fat_checksum(const struct FAT_FILEINFO *entry) {
  const uint8_t *name = (const uint8_t *)entry;
  uint8_t sum = 0;
  for (unsigned i = 0; i < 11; i++) {
    sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
  }
  return sum;
}

static void fat_short_display(const struct FAT_FILEINFO *entry, char *name) {
  unsigned length = 0;
  for (unsigned i = 0; i < 11; i++) {
    if (i == 8 && entry->ext[0] != ' ') {
      name[length++] = '.';
    }
    uint8_t ch = ((const uint8_t *)entry)[i];
    if (ch == ' ') {
      continue;
    }
    if (i == 0 && ch == 5) {
      ch = 0xe5;
    }
    if ((entry->reserve & (i < 8 ? 8 : 16)) && ch >= 'A' && ch <= 'Z') {
      ch += 'a' - 'A';
    }
    name[length++] = ch;
  }
  name[length] = 0;
}

/* Only a complete, adjacent sequence with a matching alias owns LFN slots. */
static unsigned fat_long_name(struct FAT_FILEINFO *directory, unsigned slot,
                              char *name) {
  uint16_t units[260];
  uint8_t checksum = fat_checksum(&directory[slot]);
  unsigned total = 0;
  for (unsigned ordinal = 1; ordinal <= 20 && ordinal <= slot; ordinal++) {
    const uint8_t *raw = (const uint8_t *)&directory[slot - ordinal];
    if (raw[11] != 15 || raw[12] || raw[13] != checksum ||
        fat_read_u16(raw + 26) || (raw[0] & ~0x40) != ordinal) {
      return 0;
    }
    for (unsigned i = 0; i < 13; i++) {
      units[(ordinal - 1) * 13 + i] = fat_read_u16(raw + fat_lfn_offsets[i]);
    }
    if (raw[0] & 0x40) {
      total = ordinal;
      break;
    }
  }
  if (!total) {
    return 0;
  }
  unsigned length = 0, bytes = 0;
  bool ended = false;
  for (unsigned i = 0; i < total * 13; i++) {
    uint32_t ch = units[i];
    if (ended) {
      if (ch != 0xffff) {
        return 0;
      }
      continue;
    }
    if (!ch) {
      ended = true;
      continue;
    }
    if (++length > FAT_LFN_UNITS || ch == 0xffff) {
      return 0;
    }
    if (ch >= 0xd800 && ch <= 0xdbff) {
      if (++i >= total * 13 || units[i] < 0xdc00 || units[i] > 0xdfff ||
          ++length > FAT_LFN_UNITS) {
        return 0;
      }
      ch = 0x10000 + ((ch - 0xd800) << 10) + units[i] - 0xdc00;
    } else if (ch >= 0xdc00 && ch <= 0xdfff) {
      return 0;
    }
    unsigned count = ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
    if (ch < 32 || (ch < 128 && strchr("\"*/:<>?\\|", ch))) {
      return 0;
    }
    if (!name) {
      continue;
    }
    if (bytes + count >= FAT_NAME_BYTES) {
      return 0;
    }
    if (count == 1) {
      name[bytes++] = ch;
      continue;
    }
    name[bytes++] = (count == 2   ? 0xc0
                     : count == 3 ? 0xe0
                                  : 0xf0) |
                    (ch >> (6 * (count - 1)));
    while (--count) {
      name[bytes++] = 0x80 | ((ch >> (6 * (count - 1))) & 63);
    }
  }
  if (!length || (length + 12) / 13 != total || units[length - 1] == ' ' ||
      units[length - 1] == '.') {
    return 0;
  }
  if (name) {
    name[bytes] = 0;
  }
  return total;
}

static struct FAT_FILEINFO *
fat_search(const char *name, struct FAT_FILEINFO *directory, int max) {
  char display[FAT_NAME_BYTES];
  for (int i = 0; i < max && directory[i].name[0]; i++) {
    struct FAT_FILEINFO *entry = &directory[i];
    if (entry->name[0] == 0xe5 || (entry->type & 8)) {
      continue;
    }
    if (fat_long_name(directory, i, display) && fat_name_equal(name, display)) {
      return entry;
    }
    fat_short_display(entry, display);
    if (fat_name_equal(name, display)) {
      return entry;
    }
  }
  return NULL;
}

static bool fat_alias_character(uint8_t ch) {
  return ch > 32 && ch < 127 && !strchr("\"*+,./:;<=>?[\\]|", ch);
}

static bool fat_prepare_name(const char *name, struct FAT_FILEINFO *directory,
                             int max, fat_name_t *result) {
  if (!fat_decode_name(name, result)) {
    return false;
  }
  memset(result->alias, ' ', 11);
  result->case_flags = 0;
  const char *dot = strrchr(name, '.');
  size_t length = strlen(name);
  size_t base = dot ? (size_t)(dot - name) : length;
  size_t extension = dot ? length - base - 1 : 0;
  bool short_name = base > 0 && base <= 8 && extension <= 3;
  for (size_t i = 0; i < length; i++) {
    if (name + i != dot && !fat_alias_character(name[i])) {
      short_name = false;
    }
  }
  if (short_name) {
    for (unsigned i = 0; i < base; i++) {
      result->alias[i] = fat_upper(name[i]);
    }
    for (unsigned i = 0; i < extension; i++) {
      result->alias[8 + i] = fat_upper(dot[1 + i]);
    }
    bool mixed = false;
    for (unsigned part = 0; part < 2; part++) {
      const char *text = part ? dot ? dot + 1 : "" : name;
      size_t count = part ? extension : base;
      bool lower = false, upper = false;
      for (size_t i = 0; i < count; i++) {
        lower |= text[i] >= 'a' && text[i] <= 'z';
        upper |= text[i] >= 'A' && text[i] <= 'Z';
      }
      mixed |= lower && upper;
      if (lower) {
        result->case_flags |= part ? 16 : 8;
      }
    }
    result->slots = mixed ? (result->length + 12) / 13 : 0;
    if (mixed) {
      result->case_flags = 0;
    }
    return true;
  }
  char stem[8];
  unsigned stem_length = 0;
  for (unsigned i = 0; i < base && stem_length < 8; i++) {
    uint8_t ch = name[i];
    if (ch == ' ' || ch == '.') {
      continue;
    }
    stem[stem_length++] = fat_alias_character(ch) ? fat_upper(ch) : '_';
  }
  if (!stem_length) {
    stem[stem_length++] = '_';
  }
  for (unsigned i = 0; i < extension && i < 3; i++) {
    uint8_t ch = dot[1 + i];
    result->alias[8 + i] = fat_alias_character(ch) ? fat_upper(ch) : '_';
  }
  for (unsigned serial = 1; serial <= 999999; serial++) {
    char digits[7];
    unsigned n = 0, value = serial;
    do {
      digits[n++] = '0' + value % 10;
      value /= 10;
    } while (value);
    unsigned prefix = stem_length < 7 - n ? stem_length : 7 - n;
    memset(result->alias, ' ', 8);
    memcpy(result->alias, stem, prefix);
    result->alias[prefix++] = '~';
    while (n) {
      result->alias[prefix++] = digits[--n];
    }
    bool used = false;
    for (int i = 0; i < max && directory[i].name[0]; i++) {
      if (directory[i].name[0] != 0xe5 && !(directory[i].type & 8) &&
          fat_name_matches(&directory[i], result->alias)) {
        used = true;
        break;
      }
    }
    if (!used) {
      result->slots = (result->length + 12) / 13;
      return true;
    }
  }
  return false;
}

static void fat_store_name(struct FAT_FILEINFO *directory, unsigned start,
                           const fat_name_t *name, struct FAT_FILEINFO entry) {
  memcpy(&entry, name->alias, 11);
  entry.reserve = name->case_flags;
  uint8_t checksum = fat_checksum(&entry);
  for (unsigned i = 0; i < name->slots; i++) {
    unsigned ordinal = name->slots - i;
    uint8_t *raw = (uint8_t *)&directory[start + i];
    memset(raw, 0, 32);
    raw[0] = ordinal | (i == 0 ? 0x40 : 0);
    raw[11] = 15;
    raw[13] = checksum;
    for (unsigned j = 0; j < 13; j++) {
      unsigned index = (ordinal - 1) * 13 + j;
      uint16_t ch = index < name->length    ? name->units[index]
                    : index == name->length ? 0
                                            : 0xffff;
      raw[fat_lfn_offsets[j]] = ch;
      raw[fat_lfn_offsets[j] + 1] = ch >> 8;
    }
  }
  directory[start + name->slots] = entry;
}

static void fat_erase_entry(struct FAT_FILEINFO *directory, unsigned slot) {
  unsigned slots = fat_long_name(directory, slot, NULL);
  for (unsigned i = slot - slots; i <= slot; i++) {
    directory[i].name[0] = 0xe5;
  }
}

static bool fat_ensure_clusters(vfs_t *vfs, int start, uint32_t required);

static int fat_reserve_entries(vfs_t *vfs, struct FAT_FILEINFO **parent,
                               unsigned needed) {
  struct FAT_FILEINFO *directory = *parent;
  int max = get_directory_max(directory, vfs);
  unsigned run = 0;
  bool end = false;
  for (int i = 0; i < max; i++) {
    end |= directory[i].name[0] == 0;
    run = end || directory[i].name[0] == 0xe5 ? run + 1 : 0;
    if (run == needed) {
      if (end && i + 1 < max) {
        directory[i + 1].name[0] = 0;
      }
      return i + 1 - needed;
    }
  }
  bool root = directory == get_dm(vfs).root_directory;
  if (root && get_dm(vfs).type != 32) {
    return -1;
  }
  struct List *buffer_entry = NULL, *size_entry = NULL;
  int cluster = fat_state(vfs)->root_cluster;
  if (!root) {
    for (int i = 1; (buffer_entry = list_get(i, get_dm(vfs).directory_list));
         i++) {
      if (buffer_entry->val == (uintptr_t)directory) {
        size_entry = list_get(i, get_dm(vfs).directory_max_list);
        cluster = list_get(i, get_dm(vfs).directory_clustno_list)->val;
        break;
      }
    }
    if (!size_entry) {
      return -1;
    }
  }
  unsigned per_cluster = get_dm(vfs).ClustnoBytes / 32;
  uint64_t new_max = ((uint64_t)max + needed - run + per_cluster - 1) /
                     per_cluster * per_cluster;
  if (new_max > INT_MAX / 32) {
    return -1;
  }
  struct FAT_FILEINFO *grown = malloc(new_max * 32);
  if (!grown) {
    return -1;
  }
  memcpy(grown, directory, max * 32);
  memset(grown + max, 0, (new_max - max) * 32);
  if (!fat_ensure_clusters(vfs, cluster, new_max / per_cluster)) {
    free(grown);
    return -1;
  }
  if (root) {
    get_dm(vfs).root_directory = grown;
    get_dm(vfs).RootMaxFiles = new_max;
  } else {
    buffer_entry->val = (uintptr_t)grown;
    size_entry->val = new_max;
  }
  free(directory);
  *parent = grown;
  return max - run;
}

int format(char drive) {
  if (!DiskReady(drive)) {
    return 0;
  }
  uint64_t disk_size = disk_Size(drive);
  if (disk_size / 512 > UINT_MAX) {
    return 0;
  }
  uint32_t total_sectors = disk_size / 512;
  if (total_sectors < 32) {
    return 0;
  }

  int type = disk_size <= 2097152U ? 12
             : disk_size <= 2147483648U ? 16
                                        : 32;
  FILE *boot_file = fopen(type == 32 ? "/boot32.bin" : "/boot.bin", "r");
  uint8_t *boot = malloc(512);
  if (boot_file == NULL || boot == NULL ||
      fread(boot, 1, 512, boot_file) != 512) {
    if (boot_file != NULL) {
      fclose(boot_file);
    }
    free(boot);
    return 0;
  }
  fclose(boot_file);

  const uint32_t reserved_sectors = 1;
  const uint32_t fat_count = 2;
  const uint32_t root_entries = type == 32 ? 0 : 224;
  const uint32_t root_sectors = (root_entries * 32 + 511) / 512;
  uint32_t sectors_per_cluster = type == 32 ? 8 : 1;
  uint32_t fat_sectors = 0;
  uint32_t cluster_count = 0;
  for (; sectors_per_cluster <= 128; sectors_per_cluster <<= 1) {
    fat_sectors = 1;
    for (;;) {
      uint32_t metadata_sectors =
          reserved_sectors + fat_count * fat_sectors + root_sectors;
      if (metadata_sectors >= total_sectors) {
        break;
      }
      cluster_count =
          (total_sectors - metadata_sectors) / sectors_per_cluster;
      uint32_t fat_bytes =
          type == 12 ? ((cluster_count + 2) * 3 + 1) / 2
                     : (cluster_count + 2) * (type / 8);
      uint32_t required_sectors = (fat_bytes + 511) / 512;
      if (required_sectors == fat_sectors) {
        break;
      }
      fat_sectors = required_sectors;
    }
    bool valid = type == 12 ? cluster_count < 4085
                 : type == 16 ? cluster_count >= 4085 && cluster_count < 65525
                              : cluster_count >= 65525 &&
                                    cluster_count < 0x0ffffff5;
    if (valid) {
      break;
    }
  }
  if (sectors_per_cluster > 128 || fat_sectors == 0 ||
      (type != 32 && fat_sectors > USHRT_MAX)) {
    free(boot);
    return 0;
  }

  boot[BPB_SecPerClus] = sectors_per_cluster;
  *(uint16_t *)(boot + BPB_BytsPerSec) = 512;
  *(uint16_t *)(boot + BPB_RsvdSecCnt) = reserved_sectors;
  boot[BPB_NumFATs] = fat_count;
  *(uint16_t *)(boot + BPB_RootEntCnt) = root_entries;
  *(uint16_t *)(boot + BPB_TotSec16) =
      total_sectors <= USHRT_MAX ? total_sectors : 0;
  *(uint32_t *)(boot + BPB_TotSec32) = total_sectors;
  boot[BPB_Media] = drive < 'C' ? 0xf0 : 0xf8;
  *(uint16_t *)(boot + BPB_FATSz16) = type == 32 ? 0 : fat_sectors;

  uint32_t extension = type == 32 ? BPB_Fat32ExtByts : 0;
  if (type == 32) {
    *(uint32_t *)(boot + BPB_FATSz32) = fat_sectors;
    *(uint16_t *)(boot + BPB_ExtFlags) = 0;
    *(uint16_t *)(boot + BPB_FSVer) = 0;
    *(uint32_t *)(boot + BPB_RootClus) = 2;
    *(uint16_t *)(boot + BPB_FSInfo) = 0;
    *(uint16_t *)(boot + BPB_BkBootSec) = 0;
    memset(boot + BPB_Reserved, 0, 12);
  }
  boot[BS_DrvNum + extension] = drive < 'C' ? 0 : drive - 'C' + 0x80;
  boot[BS_BootSig + extension] = 0x29;
  srand(time());
  *(uint32_t *)(boot + BS_VolD + extension) = rand();
  memcpy(boot + BS_VolLab + extension, "POWERINTDOS", 11);
  memcpy(boot + BS_FileSysType + extension,
         type == 12 ? "FAT12   " : type == 16 ? "FAT16   " : "FAT32   ", 8);

  uint32_t fat_bytes = fat_sectors * 512;
  uint8_t *fat = malloc(fat_bytes);
  uint32_t directory_sectors = type == 32 ? sectors_per_cluster : root_sectors;
  uint8_t *directory = malloc(directory_sectors * 512);
  if (fat == NULL || directory == NULL) {
    free(directory);
    free(fat);
    free(boot);
    return 0;
  }
  memset(fat, 0, fat_bytes);
  memset(directory, 0, directory_sectors * 512);
  if (type == 12) {
    fat[0] = boot[BPB_Media];
    fat[1] = 0xff;
    fat[2] = 0xff;
  } else if (type == 16) {
    ((uint16_t *)fat)[0] = 0xff00 | boot[BPB_Media];
    ((uint16_t *)fat)[1] = 0xffff;
  } else {
    ((uint32_t *)fat)[0] = 0x0fffff00 | boot[BPB_Media];
    ((uint32_t *)fat)[1] = 0x0fffffff;
    ((uint32_t *)fat)[2] = 0x0fffffff;
  }

  uint32_t fat1_sector = reserved_sectors;
  uint32_t fat2_sector = fat1_sector + fat_sectors;
  uint32_t directory_sector = fat2_sector + fat_sectors;
  disk_write(0, 1, boot, drive);
  disk_write(fat1_sector, fat_sectors, fat, drive);
  disk_write(fat2_sector, fat_sectors, fat, drive);
  disk_write(directory_sector, directory_sectors, directory, drive);

  uint8_t verification[512];
  disk_read(0, 1, verification, drive);
  bool written = memcmp(verification, boot, 512) == 0;
  if (written) {
    disk_read(fat1_sector, 1, verification, drive);
    written = memcmp(verification, fat, 512) == 0;
  }
  if (written) {
    disk_read(fat2_sector, 1, verification, drive);
    written = memcmp(verification, fat, 512) == 0;
  }
  if (written) {
    disk_read(directory_sector, 1, verification, drive);
    written = memcmp(verification, directory, 512) == 0;
  }
  free(directory);
  free(fat);
  free(boot);
  return written;
}
bool fat_init_fs(vfs_t *vfs, uint8_t disk_number) {
  void *boot_sector = NULL;
  fat_state_t *state = malloc(sizeof(*state));
  if (state == NULL) {
    return false;
  }
  memset(state, 0, sizeof(*state));
  vfs_mount_set_data(vfs, state);
  boot_sector = malloc(512);
  if (boot_sector == NULL) {
    goto fail;
  }
  unsigned char *boot = boot_sector;
  disk_read(0, 1, boot_sector, disk_number);

  if (memcmp(boot + BS_FileSysType, "FAT12   ", 8) == 0) {
    get_dm(vfs).type = 12;
  } else if (memcmp(boot + BS_FileSysType, "FAT16   ", 8) == 0) {
    get_dm(vfs).type = 16;
  } else if (memcmp(boot + BS_FileSysType + BPB_Fat32ExtByts, "FAT32   ", 8) ==
             0) {
    get_dm(vfs).type = 32;
  } else {
    goto fail;
  }
  unsigned short sector_bytes = fat_read_u16(boot + BPB_BytsPerSec);
  unsigned short root_max_files = fat_read_u16(boot + BPB_RootEntCnt);
  unsigned short reserved_sector_count = fat_read_u16(boot + BPB_RsvdSecCnt);
  unsigned short fat16_size = fat_read_u16(boot + BPB_FATSz16);
  unsigned short total_sector_16 = fat_read_u16(boot + BPB_TotSec16);
  unsigned int total_sector_32 = fat_read_u32(boot + BPB_TotSec32);
  unsigned int fat32_size = fat_read_u32(boot + BPB_FATSz32);
  unsigned int root_cluster = fat_read_u32(boot + BPB_RootClus);
  unsigned char sectors_per_cluster = boot[BPB_SecPerClus];
  unsigned char num_fats = boot[BPB_NumFATs];
  if (sector_bytes != 512 || sectors_per_cluster == 0 ||
      (sectors_per_cluster & (sectors_per_cluster - 1)) != 0 ||
      num_fats == 0) {
    goto fail;
  }

  uint64_t fat_sectors = get_dm(vfs).type == 32 ? fat32_size : fat16_size;
  uint64_t total_sectors =
      total_sector_16 != 0 ? total_sector_16 : total_sector_32;
  uint64_t root_directory_bytes = (uint64_t)root_max_files * 32;
  uint64_t root_directory_sectors =
      (root_directory_bytes + sector_bytes - 1) / sector_bytes;
  uint64_t cluster_bytes = (uint64_t)sector_bytes * sectors_per_cluster;
  uint64_t root_directory_sector =
      (uint64_t)reserved_sector_count + (uint64_t)num_fats * fat_sectors;
  uint64_t first_data_sector = root_directory_sector + root_directory_sectors;
  if (fat_sectors == 0 || total_sectors == 0 || cluster_bytes == 0 ||
      first_data_sector >= total_sectors ||
      (get_dm(vfs).type == 32 && root_cluster < 2)) {
    goto fail;
  }

  uint64_t root_sector = root_directory_sector;
  uint64_t root_entries = root_max_files;
  if (get_dm(vfs).type == 32) {
    root_sector = first_data_sector +
                  (uint64_t)(root_cluster - 2) * sectors_per_cluster;
    root_entries = cluster_bytes / 32;
  }
  uint64_t fat1_address = (uint64_t)reserved_sector_count * sector_bytes;
  uint64_t fat2_address =
      ((uint64_t)reserved_sector_count + fat_sectors) * sector_bytes;
  uint64_t file_data_address = first_data_sector * sector_bytes;
  uint64_t root_address = root_sector * sector_bytes;
  uint64_t image_size = total_sectors * sector_bytes;
  uint64_t fat_terms = fat_sectors * sector_bytes * 8 / get_dm(vfs).type;
  uint64_t cache_size_64 = root_address + root_entries * 32;
  uint64_t cache_sectors =
      (cache_size_64 + sector_bytes - 1) / sector_bytes;
  uint64_t cache_allocation_size = cache_sectors * sector_bytes;
  if (root_sector >= total_sectors ||
      (get_dm(vfs).type == 32 &&
       root_sector + sectors_per_cluster > total_sectors) ||
      root_entries == 0 ||
      root_entries > USHRT_MAX ||
      fat1_address > UINT_MAX || fat2_address > UINT_MAX ||
      file_data_address > UINT_MAX || root_address > UINT_MAX ||
      image_size > UINT_MAX || fat_terms == 0 ||
      fat_terms > INT_MAX / (int)sizeof(int) ||
      cache_allocation_size > INT_MAX) {
    goto fail;
  }

  get_dm(vfs).SectorBytes = sector_bytes;
  get_dm(vfs).RootMaxFiles = root_entries;
  state->root_cluster = root_cluster;
  get_dm(vfs).ClustnoBytes = (unsigned int)cluster_bytes;
  get_dm(vfs).Fat1Address = (unsigned int)fat1_address;
  get_dm(vfs).Fat2Address = (unsigned int)fat2_address;
  get_dm(vfs).FileDataAddress = (unsigned int)file_data_address;
  get_dm(vfs).RootDictAddress = (unsigned int)root_address;
  get_dm(vfs).imgTotalSize = (unsigned int)image_size;
  get_dm(vfs).FatMaxTerms = (int)fat_terms;

  uint32_t sec = (uint32_t)cache_sectors;
  get_dm(vfs).ADR_DISKIMG =
      (uintptr_t)malloc((int)cache_allocation_size);
  if (get_dm(vfs).ADR_DISKIMG == 0) {
    goto fail;
  }

  disk_read(0, sec, (void *)(uintptr_t)get_dm(vfs).ADR_DISKIMG, disk_number);

  get_dm(vfs).fat = malloc(get_dm(vfs).FatMaxTerms * sizeof(int));
  get_dm(vfs).FatClustnoFlags = malloc(get_dm(vfs).FatMaxTerms * sizeof(char));
  if (get_dm(vfs).fat == NULL || get_dm(vfs).FatClustnoFlags == NULL) {
    goto fail;
  }
  if (!read_fat((unsigned char *)(get_dm(vfs).ADR_DISKIMG +
                                  (unsigned int)get_dm(vfs).Fat1Address),
                fat_sectors * sector_bytes, get_dm(vfs).fat,
                get_dm(vfs).FatClustnoFlags, get_dm(vfs).FatMaxTerms,
                get_dm(vfs).type)) {
    goto fail;
  }
  if (get_dm(vfs).type == 32) {
    int clusters;
    if (!fat_chain_length(vfs, root_cluster, &clusters) ||
        (uint64_t)clusters * cluster_bytes > INT_MAX) {
      goto fail;
    }
    get_dm(vfs).RootMaxFiles = clusters * (cluster_bytes / 32);
  }
  get_dm(vfs).root_directory = malloc(get_dm(vfs).RootMaxFiles * 32);
  if (!get_dm(vfs).root_directory) {
    goto fail;
  }
  if (get_dm(vfs).type == 32) {
    int cluster = root_cluster;
    unsigned count = get_dm(vfs).RootMaxFiles * 32 / cluster_bytes;
    for (unsigned i = 0; i < count; i++) {
      disk_read((get_dm(vfs).FileDataAddress + (cluster - 2) * cluster_bytes) /
                    sector_bytes,
                sectors_per_cluster,
                (char *)get_dm(vfs).root_directory + i * cluster_bytes,
                disk_number);
      cluster = get_dm(vfs).fat[cluster];
    }
  } else {
    memcpy(get_dm(vfs).root_directory,
           (void *)(get_dm(vfs).ADR_DISKIMG + get_dm(vfs).RootDictAddress),
           get_dm(vfs).RootMaxFiles * 32);
  }
  get_dm(vfs).directory_list = (struct List *)NewList();
  get_dm(vfs).directory_clustno_list = (struct List *)NewList();
  get_dm(vfs).directory_max_list = (struct List *)NewList();
  if (get_dm(vfs).directory_list == NULL ||
      get_dm(vfs).directory_clustno_list == NULL ||
      get_dm(vfs).directory_max_list == NULL) {
    goto fail;
  }
  struct FAT_FILEINFO *finfo = get_dm(vfs).root_directory;

  for (int i = 0; i != get_dm(vfs).RootMaxFiles; i++) {
    if ((finfo[i].type & 0x18) == 0x10 && finfo[i].name[0] != 0xe5) {
      if (!AddVal(get_clustno(finfo[i].clustno_high, finfo[i].clustno_low),
                  (struct List *)get_dm(vfs).directory_clustno_list)) {
        goto fail;
      }
      int k;
      if (!fat_chain_length(
              vfs, get_clustno(finfo[i].clustno_high, finfo[i].clustno_low),
              &k)) {
        goto fail;
      }
      if (k <= 0 || get_dm(vfs).ClustnoBytes > (unsigned int)INT_MAX / k) {
        goto fail;
      }
      void *directory_alloc = malloc(k * get_dm(vfs).ClustnoBytes);
      if (directory_alloc == NULL) {
        goto fail;
      }
      for (int j = get_clustno(finfo[i].clustno_high, finfo[i].clustno_low),
               l = 0;
           l != k; l++) {
        if (!fat_data_cluster_valid(vfs, j)) {
          free(directory_alloc);
          goto fail;
        }
        uint32_t sec1 =
            (get_dm(vfs).FileDataAddress + (j - 2) * get_dm(vfs).ClustnoBytes) /
            get_dm(vfs).SectorBytes;
        disk_read(sec1, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                  (char *)directory_alloc + l * get_dm(vfs).ClustnoBytes,
                  disk_number);
        j = get_dm(vfs).fat[j];
      }
      if (!AddVal((uintptr_t)directory_alloc,
                  (struct List *)get_dm(vfs).directory_list)) {
        free(directory_alloc);
        goto fail;
      }
      if (!AddVal(k * get_dm(vfs).ClustnoBytes / 32,
                  (struct List *)get_dm(vfs).directory_max_list)) {
        goto fail;
      }
    }
    if (finfo[i].name[0] == 0) {
      break;
    }
  }

  for (int i = 1;
       list_get(i, (struct List *)get_dm(vfs).directory_list) != NULL;
       i++) {
    struct List *list =
        list_get(i, (struct List *)get_dm(vfs).directory_list);
    finfo = (struct FAT_FILEINFO *)list->val;
    for (int j = 0; j != get_directory_max(finfo, vfs); j++) {
      if ((finfo[j].type & 0x18) == 0x10 && finfo[j].name[0] != 0xe5 &&
          strncmp(".", (char *)finfo[j].name, 1) != 0 &&
          strncmp("..", (char *)finfo[j].name, 2) != 0) {
        if (!AddVal(get_clustno(finfo[j].clustno_high, finfo[j].clustno_low),
                    (struct List *)get_dm(vfs).directory_clustno_list)) {
          goto fail;
        }
        int k;
        if (!fat_chain_length(
                vfs,
                get_clustno(finfo[j].clustno_high, finfo[j].clustno_low),
                &k)) {
          goto fail;
        }
        if (k <= 0 || get_dm(vfs).ClustnoBytes > (unsigned int)INT_MAX / k) {
          goto fail;
        }
        void *directory_alloc = malloc(k * get_dm(vfs).ClustnoBytes);
        if (directory_alloc == NULL) {
          goto fail;
        }
        for (int m = get_clustno(finfo[j].clustno_high, finfo[j].clustno_low),
                 l = 0;
             l != k; l++) {
          if (!fat_data_cluster_valid(vfs, m)) {
            free(directory_alloc);
            goto fail;
          }
          uint32_t sec1 = (get_dm(vfs).FileDataAddress +
                           (m - 2) * get_dm(vfs).ClustnoBytes) /
                          get_dm(vfs).SectorBytes;
          disk_read(sec1, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                    (char *)directory_alloc + l * get_dm(vfs).ClustnoBytes,
                    disk_number);
          m = get_dm(vfs).fat[m];
        }
        if (!AddVal((uintptr_t)directory_alloc,
                    (struct List *)get_dm(vfs).directory_list)) {
          free(directory_alloc);
          goto fail;
        }
        if (!AddVal(k * get_dm(vfs).ClustnoBytes / 32,
                    (struct List *)get_dm(vfs).directory_max_list)) {
          goto fail;
        }
      }
      if (finfo[j].name[0] == 0) {
        break;
      }
    }
  }
  free(boot_sector);
  return true;

fail:
  free(boot_sector);
  fat_delete_fs(vfs);
  return false;
}
static uint32_t fat_directory_cluster(vfs_t *vfs,
                                      struct FAT_FILEINFO *directory) {
  if (directory == get_dm(vfs).root_directory) {
    return 0;
  }
  for (int index = 1;; index++) {
    struct List *entry = list_get(index, get_dm(vfs).directory_list);
    if (entry == NULL) {
      return UINT_MAX;
    }
    if ((struct FAT_FILEINFO *)(uintptr_t)entry->val == directory) {
      return list_get(index, get_dm(vfs).directory_clustno_list)->val;
    }
  }
}

static struct FAT_FILEINFO *fat_directory_by_cluster(vfs_t *vfs,
                                                      uint32_t cluster) {
  if (cluster == 0) {
    return get_dm(vfs).root_directory;
  }
  for (int index = 1;; index++) {
    struct List *entry =
        list_get(index, get_dm(vfs).directory_clustno_list);
    if (entry == NULL) {
      return NULL;
    }
    if (entry->val == cluster) {
      return (struct FAT_FILEINFO *)(uintptr_t)
          list_get(index, get_dm(vfs).directory_list)->val;
    }
  }
}

static struct FAT_FILEINFO *fat_node_directory(vfs_t *vfs,
                                                const vfs_node_t *node) {
  return node != NULL && node->type == VFS_NODE_DIRECTORY &&
                 node->id.value[0] == 1
             ? fat_directory_by_cluster(vfs, node->id.value[1])
             : NULL;
}

static struct FAT_FILEINFO *fat_node_entry(vfs_t *vfs,
                                            const vfs_node_t *node,
                                            struct FAT_FILEINFO **parent) {
  if (node == NULL || node->type != VFS_NODE_FILE || node->id.value[0] != 2) {
    return NULL;
  }
  struct FAT_FILEINFO *directory =
      fat_directory_by_cluster(vfs, node->id.value[1]);
  uint32_t slot = node->id.value[2];
  if (directory == NULL || slot >= (uint32_t)get_directory_max(directory, vfs) ||
      directory[slot].name[0] == 0 || directory[slot].name[0] == 0xe5) {
    return NULL;
  }
  if (parent != NULL) {
    *parent = directory;
  }
  return &directory[slot];
}

static ftype fat_entry_attribute(const struct FAT_FILEINFO *entry) {
  if (entry->type == 0x01) {
    return RDO;
  }
  if (entry->type == 0x02) {
    return HID;
  }
  if (entry->type == 0x04) {
    return SYS;
  }
  return FLE;
}

static void fat_make_node(vfs_t *vfs, struct FAT_FILEINFO *parent,
                          uint32_t slot, vfs_node_t *node) {
  struct FAT_FILEINFO *entry = &parent[slot];
  memset(node, 0, sizeof(*node));
  if ((entry->type & 0x18) == 0x10) {
    node->type = VFS_NODE_DIRECTORY;
    node->id.value[0] = 1;
    node->id.value[1] =
        get_clustno(entry->clustno_high, entry->clustno_low);
    node->attributes = DIR;
  } else {
    node->type = VFS_NODE_FILE;
    node->id.value[0] = 2;
    node->id.value[1] = fat_directory_cluster(vfs, parent);
    node->id.value[2] = slot;
    node->size = entry->size;
    node->attributes = fat_entry_attribute(entry);
  }
}

static int fat_normalize_name(const char *name, size_t length,
                              char *normalized, size_t capacity) {
  if (!name || !normalized || !length || length >= capacity ||
      length >= FAT_NAME_BYTES) {
    return VFS_ERROR_INVALID;
  }
  memcpy(normalized, name, length);
  normalized[length] = 0;
  fat_name_t parsed;
  return fat_decode_name(normalized, &parsed) ? VFS_OK : VFS_ERROR_INVALID;
}

static int fat_root(vfs_t *vfs, vfs_node_t *node) {
  (void)vfs;
  memset(node, 0, sizeof(*node));
  node->id.value[0] = 1;
  node->type = VFS_NODE_DIRECTORY;
  node->attributes = DIR;
  return VFS_OK;
}

static int fat_lookup(vfs_t *vfs, const vfs_node_t *directory_node,
                      const char *name, vfs_node_t *node) {
  struct FAT_FILEINFO *directory = fat_node_directory(vfs, directory_node);
  if (directory == NULL) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  struct FAT_FILEINFO *entry =
      fat_search(name, directory, get_directory_max(directory, vfs));
  if (entry == NULL) {
    return VFS_ERROR_NO_ENTRY;
  }
  fat_make_node(vfs, directory, entry - directory, node);
  return VFS_OK;
}

static bool fat_cluster_at(vfs_t *vfs, int start, uint32_t index,
                           int *cluster) {
  int current = start;
  for (uint32_t position = 0; position < index; position++) {
    if (!fat_data_cluster_valid(vfs, current) ||
        fat_is_eoc(get_dm(vfs).type, get_dm(vfs).fat[current])) {
      return false;
    }
    current = get_dm(vfs).fat[current];
  }
  if (!fat_data_cluster_valid(vfs, current)) {
    return false;
  }
  *cluster = current;
  return true;
}

static void fat_write_cluster(vfs_t *vfs, int cluster, const void *buffer) {
  disk_write((get_dm(vfs).FileDataAddress +
              (cluster - 2) * get_dm(vfs).ClustnoBytes) /
                 get_dm(vfs).SectorBytes,
             get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
             (void *)buffer, vfs_mount_disk_number(vfs));
}

static void fat_read_cluster(vfs_t *vfs, int cluster, void *buffer) {
  disk_read((get_dm(vfs).FileDataAddress +
             (cluster - 2) * get_dm(vfs).ClustnoBytes) /
                get_dm(vfs).SectorBytes,
            get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes, buffer,
            vfs_mount_disk_number(vfs));
}

static bool fat_ensure_clusters(vfs_t *vfs, int start, uint32_t required) {
  int existing;
  if (!fat_chain_length(vfs, start, &existing)) {
    return false;
  }
  if ((uint32_t)existing >= required) {
    return true;
  }
  uint32_t additional = required - existing;
  if (additional > (uint32_t)INT_MAX / sizeof(int)) {
    return false;
  }
  int *allocated = malloc(additional * sizeof(int));
  if (allocated == NULL) {
    return false;
  }
  uint32_t found = 0;
  for (int cluster = 2;
       cluster < get_dm(vfs).FatMaxTerms && found < additional; cluster++) {
    if (get_dm(vfs).fat[cluster] == 0 && fat_data_cluster_valid(vfs, cluster)) {
      allocated[found++] = cluster;
    }
  }
  if (found != additional) {
    free(allocated);
    return false;
  }
  uint8_t *zero = malloc(get_dm(vfs).ClustnoBytes);
  if (zero == NULL) {
    free(allocated);
    return false;
  }
  memset(zero, 0, get_dm(vfs).ClustnoBytes);
  int last;
  if (!fat_chain_last(vfs, start, &last)) {
    free(zero);
    free(allocated);
    return false;
  }
  for (uint32_t index = 0; index < additional; index++) {
    get_dm(vfs).fat[last] = allocated[index];
    last = allocated[index];
    get_dm(vfs).fat[last] = fat_eoc_marker(get_dm(vfs).type);
    get_dm(vfs).FatClustnoFlags[last] = true;
    fat_write_cluster(vfs, last, zero);
  }
  file_savefat(get_dm(vfs).fat, 0, get_dm(vfs).FatMaxTerms, vfs);
  free(zero);
  free(allocated);
  return true;
}

static bool fat_transfer(vfs_t *vfs, int start, uint32_t offset, void *buffer,
                         uint32_t length, bool write, bool zero) {
  if (length == 0) {
    return true;
  }
  struct FAT_CACHE *disk = &get_dm(vfs);
  uint32_t cluster_size = disk->ClustnoBytes;
  uint32_t cluster_index = offset / cluster_size;
  uint32_t cluster_offset = offset % cluster_size;
  int cluster;
  if (!fat_cluster_at(vfs, start, cluster_index, &cluster)) {
    return false;
  }
  uint8_t *bounce = NULL;
  uint32_t completed = 0;
  while (completed < length) {
    uint32_t chunk = cluster_size - cluster_offset;
    if (chunk > length - completed) {
      chunk = length - completed;
    }
    if (cluster_offset == 0 && chunk == cluster_size && !zero) {
      uint32_t run = 1;
      int last = cluster;
      uint32_t full_clusters = (length - completed) / cluster_size;
      while (run < full_clusters) {
        int next = disk->fat[last];
        if (next != last + 1 || !fat_data_cluster_valid(vfs, next)) {
          break;
        }
        last = next;
        run++;
      }
      uint32_t sector =
          (disk->FileDataAddress + (cluster - 2) * cluster_size) /
          disk->SectorBytes;
      uint32_t sectors = run * cluster_size / disk->SectorBytes;
      if (write) {
        disk_write(sector, sectors, (uint8_t *)buffer + completed,
                   vfs_mount_disk_number(vfs));
      } else {
        disk_read(sector, sectors, (uint8_t *)buffer + completed,
                  vfs_mount_disk_number(vfs));
      }
      completed += run * cluster_size;
      cluster = last;
    } else {
      if (bounce == NULL) {
        bounce = malloc(cluster_size);
        if (bounce == NULL) {
          return false;
        }
      }
      if (!write) {
        fat_read_cluster(vfs, cluster, bounce);
        memcpy((uint8_t *)buffer + completed, bounce + cluster_offset, chunk);
      } else if (cluster_offset == 0 && chunk == cluster_size) {
        memset(bounce, 0, cluster_size);
        fat_write_cluster(vfs, cluster, bounce);
      } else {
        fat_read_cluster(vfs, cluster, bounce);
        if (zero) {
          memset(bounce + cluster_offset, 0, chunk);
        } else {
          memcpy(bounce + cluster_offset, (uint8_t *)buffer + completed,
                 chunk);
        }
        fat_write_cluster(vfs, cluster, bounce);
      }
      completed += chunk;
    }
    cluster_offset = 0;
    if (completed < length) {
      int next = disk->fat[cluster];
      if (fat_is_eoc(disk->type, next) ||
          !fat_data_cluster_valid(vfs, next)) {
        free(bounce);
        return false;
      }
      cluster = next;
    }
  }
  free(bounce);
  return true;
}

static int fat_read(vfs_t *vfs, const vfs_node_t *node, uint32_t offset,
                    void *buffer, uint32_t length) {
  struct FAT_FILEINFO *entry = fat_node_entry(vfs, node, NULL);
  if (entry == NULL || offset > entry->size) {
    return VFS_ERROR_NO_ENTRY;
  }
  if (length > entry->size - offset) {
    length = entry->size - offset;
  }
  int start = get_clustno(entry->clustno_high, entry->clustno_low);
  return fat_transfer(vfs, start, offset, buffer, length, false, false)
             ? (int)length
             : VFS_ERROR_IO;
}

static int fat_resize(vfs_t *vfs, vfs_node_t *node, uint32_t size) {
  struct FAT_FILEINFO *parent;
  struct FAT_FILEINFO *entry = fat_node_entry(vfs, node, &parent);
  if (entry == NULL) {
    return VFS_ERROR_NO_ENTRY;
  }
  uint32_t cluster_size = get_dm(vfs).ClustnoBytes;
  uint32_t required = size == 0 ? 1 : (size - 1) / cluster_size + 1;
  int start = get_clustno(entry->clustno_high, entry->clustno_low);
  int existing;
  if (!start && !entry->size) {
    if (!size) {
      return VFS_OK;
    }
    start = fat_find_free_cluster(vfs);
    if (start < 0) {
      return VFS_ERROR_NO_SPACE;
    }
    get_dm(vfs).fat[start] = fat_eoc_marker(get_dm(vfs).type);
    get_dm(vfs).FatClustnoFlags[start] = true;
    if (!fat_ensure_clusters(vfs, start, required)) {
      get_dm(vfs).fat[start] = 0;
      get_dm(vfs).FatClustnoFlags[start] = false;
      return VFS_ERROR_NO_SPACE;
    }
    file_savefat(get_dm(vfs).fat, start, 1, vfs);
    entry->clustno_low = start;
    entry->clustno_high = (unsigned)start >> 16;
  }
  if (!fat_chain_length(vfs, start, &existing)) {
    return VFS_ERROR_IO;
  }
  if ((uint32_t)existing < required &&
      !fat_ensure_clusters(vfs, start, required)) {
    return VFS_ERROR_NO_SPACE;
  }
  if ((uint32_t)existing > required) {
    int last;
    if (!fat_cluster_at(vfs, start, required - 1, &last)) {
      return VFS_ERROR_IO;
    }
    int current = get_dm(vfs).fat[last];
    get_dm(vfs).fat[last] = fat_eoc_marker(get_dm(vfs).type);
    while (!fat_is_eoc(get_dm(vfs).type, current)) {
      int next = get_dm(vfs).fat[current];
      get_dm(vfs).fat[current] = 0;
      get_dm(vfs).FatClustnoFlags[current] = false;
      current = next;
    }
    if (fat_data_cluster_valid(vfs, current)) {
      get_dm(vfs).fat[current] = 0;
      get_dm(vfs).FatClustnoFlags[current] = false;
    }
    file_savefat(get_dm(vfs).fat, 0, get_dm(vfs).FatMaxTerms, vfs);
  }
  if (size > entry->size &&
      !fat_transfer(vfs, start, entry->size, NULL, size - entry->size, true,
                    true)) {
    return VFS_ERROR_IO;
  }
  if (size < entry->size && size % cluster_size != 0 &&
      !fat_transfer(vfs, start, size, NULL, cluster_size - size % cluster_size,
                    true, true)) {
    return VFS_ERROR_IO;
  }
  entry->size = size;
  entry->update_time = get_fat_time(get_hour_hex(), get_min_hex());
  entry->update_date =
      get_fat_date(get_year(), get_mon_hex(), get_day_of_month());
  file_saveinfo(parent, vfs);
  node->size = size;
  return VFS_OK;
}

static int fat_write(vfs_t *vfs, vfs_node_t *node, uint32_t offset,
                     const void *buffer, uint32_t length) {
  struct FAT_FILEINFO *entry = fat_node_entry(vfs, node, NULL);
  if (entry == NULL) {
    return VFS_ERROR_NO_ENTRY;
  }
  if (offset > UINT_MAX - length) {
    return VFS_ERROR_OVERFLOW;
  }
  uint32_t end = offset + length;
  uint32_t previous = entry->size;
  if (end > previous) {
    int status = fat_resize(vfs, node, end);
    if (status < 0) {
      return status;
    }
    entry = fat_node_entry(vfs, node, NULL);
  }
  int start = get_clustno(entry->clustno_high, entry->clustno_low);
  if (!fat_transfer(vfs, start, offset, (void *)buffer, length, true, false)) {
    if (end > previous) {
      fat_resize(vfs, node, previous);
    }
    return VFS_ERROR_IO;
  }
  return length;
}

static int fat_truncate(vfs_t *vfs, vfs_node_t *node, uint32_t size) {
  return fat_resize(vfs, node, size);
}

static int fat_create(vfs_t *vfs, const vfs_node_t *directory_node,
                      const char *name, vfs_node_type_t type,
                      vfs_node_t *node) {
  struct FAT_FILEINFO *directory = fat_node_directory(vfs, directory_node);
  if (directory == NULL) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  int max = get_directory_max(directory, vfs);
  if (fat_search(name, directory, max)) {
    return VFS_ERROR_EXISTS;
  }
  fat_name_t parsed;
  if (!fat_prepare_name(name, directory, max, &parsed)) {
    return VFS_ERROR_INVALID;
  }
  unsigned bytes = get_dm(vfs).ClustnoBytes;
  struct FAT_FILEINFO *buffer = malloc(bytes);
  if (!buffer) {
    return VFS_ERROR_NO_MEMORY;
  }
  memset(buffer, 0, bytes);
  int slot = fat_reserve_entries(vfs, &directory, parsed.slots + 1);
  int cluster = slot < 0 ? -1 : fat_find_free_cluster(vfs);
  if (cluster < 0) {
    free(buffer);
    return VFS_ERROR_NO_SPACE;
  }
  struct FAT_FILEINFO entry = {0};
  entry.type = type == VFS_NODE_DIRECTORY ? 16 : 32;
  entry.clustno_low = cluster;
  entry.clustno_high = (unsigned)cluster >> 16;
  entry.update_time = get_fat_time(get_hour_hex(), get_min_hex());
  entry.update_date =
      get_fat_date(get_year(), get_mon_hex(), get_day_of_month());
  if (type == VFS_NODE_DIRECTORY) {
    buffer[0] = entry;
    memset(&buffer[0], ' ', 11);
    buffer[0].name[0] = '.';
    buffer[1] = buffer[0];
    buffer[1].name[1] = '.';
    unsigned parent_cluster = fat_directory_cluster(vfs, directory);
    buffer[1].clustno_low = parent_cluster;
    buffer[1].clustno_high = parent_cluster >> 16;
    if (!AddVal(cluster, get_dm(vfs).directory_clustno_list)) {
      goto fail_directory;
    }
    if (!AddVal((uintptr_t)buffer, get_dm(vfs).directory_list)) {
      DeleteVal(get_dm(vfs).directory_clustno_list->ctl->all,
                get_dm(vfs).directory_clustno_list);
      goto fail_directory;
    }
    if (!AddVal(bytes / 32, get_dm(vfs).directory_max_list)) {
      DeleteVal(get_dm(vfs).directory_list->ctl->all,
                get_dm(vfs).directory_list);
      DeleteVal(get_dm(vfs).directory_clustno_list->ctl->all,
                get_dm(vfs).directory_clustno_list);
      goto fail_directory;
    }
  }
  fat_write_cluster(vfs, cluster, buffer);
  get_dm(vfs).fat[cluster] = fat_eoc_marker(get_dm(vfs).type);
  get_dm(vfs).FatClustnoFlags[cluster] = true;
  file_savefat(get_dm(vfs).fat, cluster, 1, vfs);
  fat_store_name(directory, slot, &parsed, entry);
  file_saveinfo(directory, vfs);
  fat_make_node(vfs, directory, slot + parsed.slots, node);
  if (type != VFS_NODE_DIRECTORY) {
    free(buffer);
  }
  return VFS_OK;
fail_directory:
  free(buffer);
  return VFS_ERROR_NO_MEMORY;
}

static int fat_remove_directory(vfs_t *vfs, struct FAT_FILEINFO *parent,
                                const char *name) {
  struct FAT_FILEINFO *entry =
      fat_search(name, parent, get_directory_max(parent, vfs));
  if (entry == NULL) {
    return VFS_ERROR_NO_ENTRY;
  }
  uint32_t cluster = get_clustno(entry->clustno_high, entry->clustno_low);
  struct FAT_FILEINFO *directory = fat_directory_by_cluster(vfs, cluster);
  if (directory == NULL) {
    return VFS_ERROR_IO;
  }
  int maximum = get_directory_max(directory, vfs);
  for (int index = 2; index < maximum && directory[index].name[0] != 0;
       index++) {
    if (directory[index].name[0] != 0xe5 && !(directory[index].type & 8)) {
      return VFS_ERROR_NOT_EMPTY;
    }
  }
  int current = cluster;
  while (fat_data_cluster_valid(vfs, current)) {
    int next = get_dm(vfs).fat[current];
    get_dm(vfs).fat[current] = 0;
    get_dm(vfs).FatClustnoFlags[current] = false;
    if (fat_is_eoc(get_dm(vfs).type, next)) {
      break;
    }
    current = next;
  }
  fat_erase_entry(parent, entry - parent);
  file_saveinfo(parent, vfs);
  file_savefat(get_dm(vfs).fat, 0, get_dm(vfs).FatMaxTerms, vfs);
  for (int index = 1;; index++) {
    struct List *cluster_entry =
        list_get(index, get_dm(vfs).directory_clustno_list);
    if (cluster_entry == NULL) {
      break;
    }
    if (cluster_entry->val == cluster) {
      free((void *)(uintptr_t)list_get(index, get_dm(vfs).directory_list)->val);
      DeleteVal(index, get_dm(vfs).directory_list);
      DeleteVal(index, get_dm(vfs).directory_clustno_list);
      DeleteVal(index, get_dm(vfs).directory_max_list);
      break;
    }
  }
  return VFS_OK;
}

static int fat_remove(vfs_t *vfs, const vfs_node_t *directory_node,
                      const char *name, vfs_node_type_t type) {
  struct FAT_FILEINFO *directory = fat_node_directory(vfs, directory_node);
  if (directory == NULL) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  if (type == VFS_NODE_DIRECTORY) {
    return fat_remove_directory(vfs, directory, name);
  }
  struct FAT_FILEINFO *entry =
      fat_search(name, directory, get_directory_max(directory, vfs));
  if (!entry) {
    return VFS_ERROR_NO_ENTRY;
  }
  if (entry->type & 1) {
    return VFS_ERROR_READ_ONLY;
  }
  int cluster = get_clustno(entry->clustno_high, entry->clustno_low);
  int count;
  if (cluster && !fat_chain_length(vfs, cluster, &count)) {
    return VFS_ERROR_IO;
  }
  fat_erase_entry(directory, entry - directory);
  file_saveinfo(directory, vfs);
  while (fat_data_cluster_valid(vfs, cluster)) {
    int next = get_dm(vfs).fat[cluster];
    get_dm(vfs).fat[cluster] = 0;
    get_dm(vfs).FatClustnoFlags[cluster] = false;
    cluster = next;
  }
  file_savefat(get_dm(vfs).fat, 0, get_dm(vfs).FatMaxTerms, vfs);
  return VFS_OK;
}

static int fat_rename(vfs_t *vfs, const vfs_node_t *source_directory,
                      const char *source_name,
                      const vfs_node_t *destination_directory,
                      const char *destination_name) {
  if (memcmp(&source_directory->id, &destination_directory->id,
             sizeof(source_directory->id)) != 0) {
    return VFS_ERROR_NOT_SUPPORTED;
  }
  struct FAT_FILEINFO *directory = fat_node_directory(vfs, source_directory);
  if (directory == NULL) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  int max = get_directory_max(directory, vfs);
  struct FAT_FILEINFO *entry = fat_search(source_name, directory, max);
  if (!entry) {
    return VFS_ERROR_NO_ENTRY;
  }
  if (entry->type & 1) {
    return VFS_ERROR_READ_ONLY;
  }
  struct FAT_FILEINFO *target = fat_search(destination_name, directory, max);
  if (target && target != entry) {
    return VFS_ERROR_EXISTS;
  }
  if (!strcmp(source_name, destination_name)) {
    return VFS_OK;
  }
  unsigned old_slot = entry - directory;
  struct FAT_FILEINFO saved = *entry;
  fat_name_t parsed;
  if (!fat_prepare_name(destination_name, directory, max, &parsed)) {
    return VFS_ERROR_INVALID;
  }
  unsigned old_slots = fat_long_name(directory, old_slot, NULL);
  int slot = parsed.slots <= old_slots
                 ? (int)(old_slot - parsed.slots)
                 : fat_reserve_entries(vfs, &directory, parsed.slots + 1);
  if (slot < 0) {
    return VFS_ERROR_NO_SPACE;
  }
  fat_erase_entry(directory, old_slot);
  fat_store_name(directory, slot, &parsed, saved);
  file_saveinfo(directory, vfs);
  return VFS_OK;
}

static int fat_iterate(vfs_t *vfs, const vfs_node_t *directory_node,
                       uint32_t wanted, vfs_dir_entry_t *result) {
  struct FAT_FILEINFO *directory = fat_node_directory(vfs, directory_node);
  if (directory == NULL) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  uint32_t visible = 0;
  int maximum = get_directory_max(directory, vfs);
  for (int index = 0; index < maximum; index++) {
    struct FAT_FILEINFO *entry = &directory[index];
    if (entry->name[0] == 0) {
      return 0;
    }
    if (entry->name[0] == 0xe5 || (entry->type & 8)) {
      continue;
    }
    char name[255];
    if (!fat_long_name(directory, index, name)) {
      fat_short_display(entry, name);
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      continue;
    }
    if (visible++ == wanted) {
      strcpy(result->name, name);
      fat_make_node(vfs, directory, index, &result->node);
      return 1;
    }
  }
  return 0;
}

static int fat_sync(vfs_t *vfs) {
  file_savefat(get_dm(vfs).fat, 0, get_dm(vfs).FatMaxTerms, vfs);
  return VFS_OK;
}

static void fat_delete_fs(struct vfs_mount *vfs) {
  if (fat_state(vfs) == NULL) {
    return;
  }
  free((void *)(uintptr_t)get_dm(vfs).ADR_DISKIMG);
  free(get_dm(vfs).fat);
  free(get_dm(vfs).FatClustnoFlags);
  free(get_dm(vfs).root_directory);
  if (get_dm(vfs).directory_clustno_list != NULL) {
    DeleteList(get_dm(vfs).directory_clustno_list);
  }
  if (get_dm(vfs).directory_max_list != NULL) {
    DeleteList(get_dm(vfs).directory_max_list);
  }
  if (get_dm(vfs).directory_list != NULL) {
    for (int i = 1; list_get(i, get_dm(vfs).directory_list) != NULL; i++) {
      free((void *)(uintptr_t)list_get(i, get_dm(vfs).directory_list)->val);
    }
    DeleteList(get_dm(vfs).directory_list);
  }
  free(fat_state(vfs));
  vfs_mount_set_data(vfs, NULL);
}
bool fat_check(uint8_t disk_number) {
  if (vdisk_type(disk_number) != VDISK_TYPE_BLOCK) {
    return false;
  }
  uint8_t *boot_sec = malloc(512);
  if (boot_sec == NULL) {
    return false;
  }
  disk_read(0, 1, boot_sec, disk_number);
  logk("disk number = %02x\n", disk_number);
  if (fat_read_u16(boot_sec + BPB_BytsPerSec) == 512 &&
      (memcmp(boot_sec + BS_FileSysType, "FAT12   ", 8) == 0 ||
      memcmp(boot_sec + BS_FileSysType, "FAT16   ", 8) == 0 ||
      memcmp(boot_sec + BS_FileSysType + BPB_Fat32ExtByts, "FAT32   ", 8) ==
          0)) {
    free(boot_sec);
    return true;
  }
  free(boot_sec);
  return false;
}
bool fat_format(uint8_t disk_number) { return format(disk_number); }
static int fat_mount(vfs_t *vfs) {
  return fat_init_fs(vfs, vfs_mount_disk_number(vfs)) ? VFS_OK
                                                       : VFS_ERROR_IO;
}

void Register_fat_fileSys(void) {
  static const vfs_filesystem_t filesystem = {
      .name = "FAT",
      .check = fat_check,
      .format = fat_format,
      .mount = fat_mount,
      .unmount = fat_delete_fs,
      .root = fat_root,
      .normalize_name = fat_normalize_name,
      .lookup = fat_lookup,
      .read = fat_read,
      .write = fat_write,
      .truncate = fat_truncate,
      .create = fat_create,
      .remove = fat_remove,
      .rename = fat_rename,
      .iterate = fat_iterate,
      .sync = fat_sync,
  };
  vfs_register_fs(&filesystem);
}
