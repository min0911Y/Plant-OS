// fat.c : fat文件系统的实现
#include <dos.h>
#include <fs.h>
#include <limits.h>
typedef struct {
  struct FAT_CACHE dm;
  struct FAT_FILEINFO *operation_directory;
} fat_state_t;

#define fat_state(vfs) ((fat_state_t *)vfs_mount_data(vfs))
#define get_dm(vfs) (fat_state(vfs)->dm)
#define get_now_dir(vfs) (fat_state(vfs)->operation_directory)

static void fat_delete_fs(struct vfs_mount *vfs);
void file_savefat(int *fat, int clustno, int length, vfs_t *vfs);
static int fat_create_file_entry(char *name, vfs_t *vfs);
int del(char *cmdline, vfs_t *vfs);
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
bool file_loadfile(int clustno, int size, char *buf, int *fat, vfs_t *vfs) {
  if (size < 0 || (size > 0 && buf == NULL) || fat == NULL) {
    return false;
  }
  if (!size) {
    return true;
  }
  uint32_t cluster_count =
      (uint32_t)(size - 1) / get_dm(vfs).ClustnoBytes + 1;
  uint64_t allocation_size =
      (uint64_t)cluster_count * get_dm(vfs).ClustnoBytes;
  if (allocation_size > INT_MAX) {
    return false;
  }
  int s = (int)allocation_size;
  unsigned char *img = kmalloc(s);
  if (img == NULL) {
    return false;
  }
  int flag = 0;
  uint32_t a = 0, num = 0, sec_start = 0;
  for (uint32_t i = 0; i < cluster_count; i++) {
    if (!fat_data_cluster_valid(vfs, clustno)) {
      kfree(img);
      return false;
    }
    uint32_t sec = (get_dm(vfs).FileDataAddress +
                    (clustno - 2) * get_dm(vfs).ClustnoBytes) /
                   get_dm(vfs).SectorBytes;
    if (!flag) {
      a = sec;
      num = 1;
    } else {
      if (a + num == sec) {
        num++;
      } else {
        disk_read(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                  img + sec_start * get_dm(vfs).ClustnoBytes,
                  vfs_mount_disk_number(vfs));
        sec_start += num;
        a = sec;
        num = 1;
        clustno = fat[clustno];
        continue;
      }
    }
    // disk_read(sec, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
    //           img + i * get_dm(vfs).ClustnoBytes, vfs->disk_number);
    flag = 1;
    clustno = fat[clustno];
  }
  if (num) {
    disk_read(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
              img + sec_start * get_dm(vfs).ClustnoBytes,
              vfs_mount_disk_number(vfs));
  }
  memcpy((void *)buf, img, size);
  kfree(img);
  return true;
}
bool file_savefile(int clustno, int size, char *buf, int *fat,
                   unsigned char *ff, vfs_t *vfs) {
  if (size < 0 || (size > 0 && buf == NULL) || fat == NULL || ff == NULL ||
      !fat_data_cluster_valid(vfs, clustno)) {
    return false;
  }
  uint32_t clustall = 0;
  int tmp = clustno;
  uint32_t end = fat_eoc_marker(get_dm(vfs).type);
  while (true) {
    if (!fat_data_cluster_valid(vfs, clustno) ||
        clustall >= (uint32_t)get_dm(vfs).FatMaxTerms) {
      return false;
    }
    if (fat_is_eoc(get_dm(vfs).type, fat[clustno])) {
      break;
    }
    clustno = fat[clustno];
    clustall++;
  }
  int old_clustno = clustno;
  clustno = tmp;
  uint32_t existing_clusters = clustall + 1;
  uint32_t required_clusters =
      size == 0 ? 1 : (uint32_t)(size - 1) / get_dm(vfs).ClustnoBytes + 1;
  uint32_t stored_clusters =
      required_clusters > existing_clusters ? required_clusters
                                             : existing_clusters;
  uint64_t allocation_size =
      (uint64_t)stored_clusters * get_dm(vfs).ClustnoBytes;
  if (allocation_size > INT_MAX) {
    return false;
  }
  unsigned char *img = kmalloc((int)allocation_size);
  if (img == NULL) {
    return false;
  }

  if (required_clusters > existing_clusters) {
    uint32_t additional = required_clusters - existing_clusters;
    uint32_t available = 0;
    for (int i = 2; i < get_dm(vfs).FatMaxTerms && available < additional;
         i++) {
      if (!ff[i] && fat_data_cluster_valid(vfs, i)) {
        available++;
      }
    }
    if (available != additional) {
      kfree(img);
      return false;
    }
    for (int i = 2; i < get_dm(vfs).FatMaxTerms && additional != 0; i++) {
      if (!ff[i] && fat_data_cluster_valid(vfs, i)) {
        fat[old_clustno] = i;
        old_clustno = i;
        ff[i] = true;
        additional--;
      }
    }
    fat[old_clustno] = end; // 结尾Fat
    ff[old_clustno] = true;
  }
  int alloc_size = (int)allocation_size;
  clean((char *)img, alloc_size);
  if (size > 0) {
    memcpy(img, buf, size); // 把要写入的数据复制到新请求的内存地址
  }
  // for (int i = 0; i != (alloc_size / get_dm(vfs).ClustnoBytes); i++) {
  //   uint32_t sec = (get_dm(vfs).FileDataAddress +
  //                   (clustno - 2) * get_dm(vfs).ClustnoBytes) /
  //                  get_dm(vfs).SectorBytes;
  //   disk_write(sec, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
  //              img + i * get_dm(vfs).ClustnoBytes, vfs->disk_number);
  //   clustno = fat[clustno];
  // }
  int flag = 0;
  uint32_t a = 0, num = 0, sec_start = 0;
  for (uint32_t i = 0; i < stored_clusters; i++) {
    if (!fat_data_cluster_valid(vfs, clustno)) {
      kfree(img);
      return false;
    }
    uint32_t sec = (get_dm(vfs).FileDataAddress +
                    (clustno - 2) * get_dm(vfs).ClustnoBytes) /
                   get_dm(vfs).SectorBytes;
    if (!flag) {
      a = sec;
      num = 1;
    } else {
      if (a + num == sec) {
        num++;
      } else {
        disk_write(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                   img + sec_start * get_dm(vfs).ClustnoBytes,
                   vfs_mount_disk_number(vfs));
        sec_start += num;
        a = sec;
        num = 1;
        clustno = fat[clustno];
        continue;
      }
    }
    // disk_read(sec, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
    //           img + i * get_dm(vfs).ClustnoBytes, vfs->disk_number);
    flag = 1;
    clustno = fat[clustno];
  }
  if (num) {
    disk_write(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
               img + sec_start * get_dm(vfs).ClustnoBytes,
               vfs_mount_disk_number(vfs));
  }
  kfree(img);
  if ((uint64_t)(uint32_t)size <
      (uint64_t)clustall * get_dm(vfs).ClustnoBytes) {
    // 分配Fat（中间情况没必要分配）
    int i = old_clustno;
    for (uint64_t size1 = (uint64_t)clustall * get_dm(vfs).ClustnoBytes;
         size1 > (uint32_t)size; size1 -= get_dm(vfs).ClustnoBytes) {
      fat[i] = 0;
      ff[i] = false;
      for (int j = 0; j != get_dm(vfs).FatMaxTerms; j++) {
        if (fat[j] == i) {
          i = j;
        }
      }
    }
    old_clustno = i;
    fat[old_clustno] = end;
    ff[old_clustno] = true;
  }
  file_savefat(fat, 0, get_dm(vfs).FatMaxTerms, vfs);
  return true;
}
void file_saveinfo(struct FAT_FILEINFO *directory, vfs_t *vfs) {
  if (directory == get_dm(vfs).root_directory) {
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
static struct FAT_FILEINFO *file_search(const char *name,
                                        struct FAT_FILEINFO *finfo, int max) {
  int i, j;
  char s[12];
  for (j = 0; j < 11; j++) {
    s[j] = ' ';
  }
  j = 0;
  for (i = 0; name[i] != 0; i++) {
    if (j >= 11) {
      return 0; /*没有找到*/
    }
    if (name[i] == '.' && j <= 8) {
      j = 8;
    } else {
      s[j] = name[i];
      if ('a' <= s[j] && s[j] <= 'z') {
        /*将小写字母转换为大写字母*/
        s[j] -= 0x20;
      }
      j++;
    }
  }
  for (i = 0; i < max;) {
    if (finfo[i].name[0] == 0x00) {
      break;
    }
    if ((finfo[i].type & 0x18) == 0) {
      if (fat_name_matches(&finfo[i], s)) {
        return finfo + i; /*找到文件*/
      }
    }
    i++;
  }
  return 0; /*没有找到*/
}
static struct FAT_FILEINFO *dict_search(const char *name,
                                        struct FAT_FILEINFO *finfo, int max) {
  int i, j;
  char s[12];
  for (j = 0; j < 11; j++) {
    s[j] = ' ';
  }
  j = 0;
  for (i = 0; name[i] != 0; i++) {
    if (j >= 11) {
      return 0; /*没有找到*/
    } else {
      s[j] = name[i];
      if ('a' <= s[j] && s[j] <= 'z') {
        /*将小写字母转换为大写字母*/
        s[j] -= 0x20;
      }
      j++;
    }
  }
  for (i = 0; i < max;) {
    if (finfo[i].name[0] == 0x00) {
      break;
    }
    if (finfo[i].type == 0x10) {
      if (fat_name_matches(&finfo[i], s)) {
        return finfo + i; /* 找到文件 */
      }
    }
    i++;
  }
  return 0; /*没有找到*/
}
static struct FAT_FILEINFO *Get_File_Address(const char *name, vfs_t *vfs) {
  struct FAT_FILEINFO *directory = get_now_dir(vfs);
  if (name == NULL || directory == NULL || strchr(name, '/') != NULL ||
      strchr(name, '\\') != NULL) {
    return NULL;
  }
  return file_search(name, directory, get_directory_max(directory, vfs));
}

static struct FAT_FILEINFO *Get_dictaddr(const char *name, vfs_t *vfs) {
  (void)name;
  return get_now_dir(vfs);
}

static void fat_replace_directory_cursor(vfs_t *vfs,
                                         uintptr_t old_directory,
                                         struct FAT_FILEINFO *new_directory) {
  if ((uintptr_t)get_now_dir(vfs) == old_directory) {
    get_now_dir(vfs) = new_directory;
  }
}

static bool fat_make_short_name(const char *name, char result[11]) {
  if (name == NULL || *name == '\0' || strchr(name, '/') != NULL ||
      strchr(name, '\\') != NULL) {
    return false;
  }
  memset(result, ' ', 11);
  size_t name_length = 0;
  size_t extension_length = 0;
  bool extension = false;
  for (; *name != '\0'; name++) {
    if (*name == '.') {
      if (extension || name_length == 0) {
        return false;
      }
      extension = true;
      continue;
    }
    if ((!extension && name_length == 8) ||
        (extension && extension_length == 3)) {
      return false;
    }
    char ch = *name >= 'a' && *name <= 'z' ? *name - ('a' - 'A') : *name;
    if (extension) {
      result[8 + extension_length++] = ch;
    } else {
      result[name_length++] = ch;
    }
  }
  return name_length != 0 && (!extension || extension_length != 0);
}

int mkdir(char *dictname, vfs_t *vfs) {
  if (dictname == NULL || vfs == NULL || Get_File_Address(dictname, vfs)) {
    return 0;
  }
  struct FAT_FILEINFO *parent = Get_dictaddr(dictname, vfs);
  char short_name[11];
  if (parent == NULL || !fat_make_short_name(dictname, short_name)) {
    return 0;
  }
  int parent_max = get_directory_max(parent, vfs);
  int parent_index = 0;
  while (parent_index < parent_max && parent[parent_index].name[0] != 0x00 &&
         parent[parent_index].name[0] != 0xe5) {
    parent_index++;
  }

  int extension_cluster = -1;
  int extension_last = -1;
  int parent_list_index = -1;
  if (parent_index == parent_max) {
    if (parent == get_dm(vfs).root_directory ||
        parent_max > INT_MAX - (int)get_dm(vfs).ClustnoBytes / 32) {
      return 0;
    }
    for (int i = 1; list_get(i, get_dm(vfs).directory_list) != NULL; i++) {
      if ((struct FAT_FILEINFO *)(uintptr_t)list_get(
              i, get_dm(vfs).directory_list)->val == parent) {
        parent_list_index = i;
        break;
      }
    }
    if (parent_list_index < 0 ||
        !fat_chain_last(
            vfs, get_clustno(parent[0].clustno_high, parent[0].clustno_low),
            &extension_last)) {
      return 0;
    }
    extension_cluster = fat_find_free_cluster(vfs);
    if (extension_cluster < 0) {
      return 0;
    }
  }

  int directory_cluster = -1;
  for (int cluster = 2; cluster < get_dm(vfs).FatMaxTerms; cluster++) {
    if (cluster != extension_cluster && get_dm(vfs).fat[cluster] == 0 &&
        fat_data_cluster_valid(vfs, cluster)) {
      directory_cluster = cluster;
      break;
    }
  }
  if (directory_cluster < 0) {
    return 0;
  }

  void *directory_buffer = malloc(get_dm(vfs).ClustnoBytes);
  if (directory_buffer == NULL) {
    return 0;
  }
  memset(directory_buffer, 0, get_dm(vfs).ClustnoBytes);
  uint16_t date = get_fat_date(get_year(), get_mon_hex(), get_day_of_month());
  uint16_t time = get_fat_time(get_hour_hex(), get_min_hex());
  int parent_cluster = parent == get_dm(vfs).root_directory
                           ? 0
                           : get_clustno(parent[0].clustno_high,
                                         parent[0].clustno_low);
  struct FAT_FILEINFO *model = directory_buffer;
  memset(model[0].name, ' ', sizeof(model[0].name));
  memset(model[0].ext, ' ', sizeof(model[0].ext));
  model[0].name[0] = '.';
  model[0].type = 0x10;
  model[0].clustno_low = directory_cluster & 0xffff;
  model[0].clustno_high = (unsigned)directory_cluster >> 16;
  model[0].update_date = date;
  model[0].update_time = time;
  memset(model[1].name, ' ', sizeof(model[1].name));
  memset(model[1].ext, ' ', sizeof(model[1].ext));
  model[1].name[0] = '.';
  model[1].name[1] = '.';
  model[1].type = 0x10;
  model[1].clustno_low = parent_cluster & 0xffff;
  model[1].clustno_high = (unsigned)parent_cluster >> 16;
  model[1].update_date = date;
  model[1].update_time = time;

  if (!AddVal(directory_cluster, get_dm(vfs).directory_clustno_list)) {
    free(directory_buffer);
    return 0;
  }
  if (!AddVal((uintptr_t)directory_buffer, get_dm(vfs).directory_list)) {
    DeleteVal(get_dm(vfs).directory_clustno_list->ctl->all,
              get_dm(vfs).directory_clustno_list);
    free(directory_buffer);
    return 0;
  }
  if (!AddVal(get_dm(vfs).ClustnoBytes / 32,
              get_dm(vfs).directory_max_list)) {
    DeleteVal(get_dm(vfs).directory_list->ctl->all,
              get_dm(vfs).directory_list);
    DeleteVal(get_dm(vfs).directory_clustno_list->ctl->all,
              get_dm(vfs).directory_clustno_list);
    free(directory_buffer);
    return 0;
  }

  if (extension_cluster >= 0) {
    int new_max = parent_max + get_dm(vfs).ClustnoBytes / 32;
    uintptr_t old_parent = (uintptr_t)parent;
    parent = realloc(parent, new_max * sizeof(struct FAT_FILEINFO));
    if (parent == NULL) {
      DeleteVal(get_dm(vfs).directory_max_list->ctl->all,
                get_dm(vfs).directory_max_list);
      DeleteVal(get_dm(vfs).directory_list->ctl->all,
                get_dm(vfs).directory_list);
      DeleteVal(get_dm(vfs).directory_clustno_list->ctl->all,
                get_dm(vfs).directory_clustno_list);
      free(directory_buffer);
      return 0;
    }
    memset(parent + parent_max, 0, get_dm(vfs).ClustnoBytes);
    list_get(parent_list_index, get_dm(vfs).directory_list)->val =
        (uintptr_t)parent;
    list_get(parent_list_index, get_dm(vfs).directory_max_list)->val =
        new_max;
    fat_replace_directory_cursor(vfs, old_parent, parent);
    get_dm(vfs).fat[extension_last] = extension_cluster;
    get_dm(vfs).fat[extension_cluster] = fat_eoc_marker(get_dm(vfs).type);
    get_dm(vfs).FatClustnoFlags[extension_cluster] = true;
  }

  struct FAT_FILEINFO *entry = &parent[parent_index];
  memset(entry, 0, sizeof(*entry));
  memcpy(entry->name, short_name, 8);
  memcpy(entry->ext, short_name + 8, 3);
  entry->type = 0x10;
  entry->clustno_low = directory_cluster & 0xffff;
  entry->clustno_high = (unsigned)directory_cluster >> 16;
  entry->update_date = date;
  entry->update_time = time;
  get_dm(vfs).fat[directory_cluster] = fat_eoc_marker(get_dm(vfs).type);
  get_dm(vfs).FatClustnoFlags[directory_cluster] = true;

  disk_write((get_dm(vfs).FileDataAddress +
              (directory_cluster - 2) * get_dm(vfs).ClustnoBytes) /
                 get_dm(vfs).SectorBytes,
             get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
             directory_buffer, vfs_mount_disk_number(vfs));
  file_saveinfo(parent, vfs);
  file_savefat(get_dm(vfs).fat, directory_cluster, 1, vfs);
  if (extension_cluster >= 0) {
    file_savefat(get_dm(vfs).fat, extension_last, 1, vfs);
    file_savefat(get_dm(vfs).fat, extension_cluster, 1, vfs);
  }
  return 1;
}

struct FAT_FILEINFO *clust_sech(int clustno, struct FAT_FILEINFO *finfo,
                                int max) {
  // 通过簇号找到文件信息
  for (int i = 0; i < max; i++) {
    if (finfo[i].clustno_low == clustno) {
      return finfo + i;
    }
  }
  return 0; /*没找到*/
}
int del(char *cmdline, vfs_t *vfs) {
  // 删除某个文件
  char *name = cmdline;
  struct FAT_FILEINFO *finfo;
  finfo = Get_File_Address(name, vfs);
  if (finfo == 0 || finfo->type == 0x01 || finfo->type == 0x04) {
    return 0;
  }
  if (!file_savefile(get_clustno(finfo->clustno_high, finfo->clustno_low), 0,
                     NULL, get_dm(vfs).fat, get_dm(vfs).FatClustnoFlags,
                     vfs)) {
    return 0;
  }
  finfo->name[0] = 0xe5;
  get_dm(vfs).fat[get_clustno(finfo->clustno_high, finfo->clustno_low)] = 0;
  get_dm(vfs)
      .FatClustnoFlags[get_clustno(finfo->clustno_high, finfo->clustno_low)] =
      false;
  file_saveinfo(Get_dictaddr(name, vfs), vfs);
  file_savefat(get_dm(vfs).fat,
               get_clustno(finfo->clustno_high, finfo->clustno_low), 1, vfs);
  return 1;
}
static int fat_create_file_entry(char *name, vfs_t *vfs) {
  char s[12];
  int i, j;
  struct FAT_FILEINFO *finfo = Get_dictaddr(name, vfs);
  if (finfo == NULL) {
    return 0;
  }
  int max = get_directory_max(finfo, vfs);

  for (j = 0; j != 12; j++) {
    s[j] = ' ';
  }
  j = 0;
  for (i = 0; name[i] != 0; i++) {
    if (j >= 11) {
      return 0;
    }
    if (name[i] == '.' && j <= 8) {
      j = 8;
    } else {
      s[j] = name[i];
      if ('a' <= s[j] && s[j] <= 'z') {
        s[j] -= 0x20;
      }
      j++;
    }
  }
  for (i = 0;; i++) {
    if (i >= max) {
      if (finfo == get_dm(vfs).root_directory) {
        return 0;
      }
      bool extended = false;
      for (int j = 1; list_get(j, get_dm(vfs).directory_list) != NULL;
           j++) {
        struct List *l = list_get(j, get_dm(vfs).directory_list);
        if ((struct FAT_FILEINFO *)l->val == finfo) {
          int new_cluster = fat_find_free_cluster(vfs);
          if (new_cluster < 0 ||
              max > INT_MAX - (int)get_dm(vfs).ClustnoBytes / 32) {
            return 0;
          }
          int new_max = max + get_dm(vfs).ClustnoBytes / 32;
          struct FAT_FILEINFO *finfo_ =
              (struct FAT_FILEINFO *)realloc((void *)finfo, new_max * 32);
          if (finfo_ == NULL) {
            return 0;
          }
          if (get_now_dir(vfs) == finfo) {
            get_now_dir(vfs) = finfo_;
          }
          finfo = finfo_;
          l->val = (uintptr_t)finfo;
          memset(finfo + max, 0, get_dm(vfs).ClustnoBytes);
          int last;
          if (!fat_chain_last(
                  vfs, get_clustno(finfo->clustno_high, finfo->clustno_low),
                  &last)) {
            return 0;
          }
          get_dm(vfs).fat[last] = new_cluster;
          get_dm(vfs).fat[new_cluster] = fat_eoc_marker(get_dm(vfs).type);
          get_dm(vfs).FatClustnoFlags[new_cluster] = true;
          max = new_max;
          list_get(j, get_dm(vfs).directory_max_list)->val = max;
          file_savefat(get_dm(vfs).fat, last, 1, vfs);
          file_savefat(get_dm(vfs).fat, new_cluster, 1, vfs);
          file_saveinfo(finfo, vfs);
          extended = true;
          break;
        }
      }
      if (!extended) {
        return 0;
      }
    }
    if (finfo[i].name[0] == 0x00 || finfo[i].name[0] == 0xe5) {
      finfo = finfo + i;
      break;
    }
  }
  for (i = 0; i != 8; i++) {
    finfo->name[i] = s[i];
  }
  for (i = 8; i != 11; i++) {
    finfo->ext[i - 8] = s[i];
  }
  finfo->type = 0x20;
  int file_cluster = fat_find_free_cluster(vfs);
  if (file_cluster < 0) {
    memset(finfo, 0, sizeof(*finfo));
    return 0;
  }
  finfo->clustno_low = file_cluster & 0xffff;
  finfo->clustno_high = (unsigned)file_cluster >> 16;
  get_dm(vfs).fat[file_cluster] = fat_eoc_marker(get_dm(vfs).type);
  get_dm(vfs).FatClustnoFlags[file_cluster] = true;
  finfo->reserve = 0;
  finfo->update_time = get_fat_time(get_hour_hex(), get_min_hex());
  finfo->update_date =
      get_fat_date(get_year(), get_mon_hex(), get_day_of_month());
  finfo->size = 0;
  file_saveinfo(Get_dictaddr(name, vfs), vfs);
  file_savefat(get_dm(vfs).fat,
               get_clustno(finfo->clustno_high, finfo->clustno_low), 1, vfs);
  return 1;
}
static int fat_rename_entry(char *src_name, char *dst_name, vfs_t *vfs) {
  strtoupper(src_name);
  strtoupper(dst_name);
  if (strcmp(src_name, dst_name) == 0) {
    return 1;
  }

  if (*dst_name == '\0' || strchr(src_name, '/') != NULL ||
      strchr(src_name, '\\') != NULL || strchr(dst_name, '/') != NULL ||
      strchr(dst_name, '\\') != NULL || strcmp(dst_name, ".") == 0 ||
      strcmp(dst_name, "..") == 0) {
    return 0;
  }

  char short_name[11];
  memset(short_name, ' ', sizeof(short_name));
  size_t name_length = 0;
  size_t extension_length = 0;
  bool extension = false;
  for (const char *cursor = dst_name; *cursor != '\0'; cursor++) {
    if (*cursor == '.') {
      if (extension || name_length == 0) {
        return 0;
      }
      extension = true;
      continue;
    }
    if ((!extension && name_length >= 8) ||
        (extension && extension_length >= 3)) {
      return 0;
    }
    if (extension) {
      short_name[8 + extension_length++] = *cursor;
    } else {
      short_name[name_length++] = *cursor;
    }
  }
  if (name_length == 0 || (extension && extension_length == 0) ||
      Get_File_Address(dst_name, vfs) != NULL) {
    return 0;
  }
  struct FAT_FILEINFO *finfo = Get_File_Address(src_name, vfs);
  if (finfo == 0 || finfo->type == 0x01 || finfo->type == 0x04) {
    return 0;
  }
  memcpy((void *)finfo->name, short_name, sizeof(short_name));
  file_saveinfo(Get_dictaddr(src_name, vfs), vfs);
  return 1;
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
int attrib(char *filename, ftype type, vfs_t *vfs) {
  struct FAT_FILEINFO *finfo = Get_File_Address(filename, vfs);
  if (finfo == 0) {
    return 0;
  }
  if (type == FLE)
    finfo->type = 0x20;
  else if (type == RDO)
    finfo->type = 0x01;
  else if (type == HID)
    finfo->type = 0x02;
  else if (type == SYS)
    finfo->type = 0x04;
  else
    return 0;
  file_saveinfo(Get_dictaddr(filename, vfs), vfs);
  return 1;
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
  get_dm(vfs).RootMaxFiles = (unsigned short)root_entries;
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
  get_dm(vfs).root_directory =
      (struct FAT_FILEINFO *)malloc(get_dm(vfs).RootMaxFiles * 32);
  if (get_dm(vfs).root_directory == NULL) {
    goto fail;
  }
  memcpy((void *)get_dm(vfs).root_directory,
         (void *)(uintptr_t)get_dm(vfs).ADR_DISKIMG +
             get_dm(vfs).RootDictAddress,
         get_dm(vfs).RootMaxFiles * 32);
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
    if (finfo[i].type == 0x10 && finfo[i].name[0] != 0xe5) {
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
      if (finfo[j].type == 0x10 && finfo[j].name[0] != 0xe5 &&
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
  get_now_dir(vfs) = get_dm(vfs).root_directory;
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
  if (entry->type == 0x10) {
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
  if (name == NULL || normalized == NULL || length == 0 || length >= 13 ||
      capacity < length + 1) {
    return VFS_ERROR_INVALID;
  }
  char input[13];
  memcpy(input, name, length);
  input[length] = '\0';
  char short_name[11];
  if (!fat_make_short_name(input, short_name)) {
    return VFS_ERROR_INVALID;
  }
  for (size_t index = 0; index < length; index++) {
    char ch = input[index];
    normalized[index] = ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
  }
  normalized[length] = '\0';
  return VFS_OK;
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
      file_search(name, directory, get_directory_max(directory, vfs));
  if (entry == NULL) {
    entry = dict_search(name, directory, get_directory_max(directory, vfs));
  }
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
  fat_chain_last(vfs, start, &last);
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
  get_now_dir(vfs) = directory;
  if (file_search(name, directory, get_directory_max(directory, vfs)) != NULL ||
      dict_search(name, directory, get_directory_max(directory, vfs)) != NULL) {
    return VFS_ERROR_EXISTS;
  }
  if ((type == VFS_NODE_FILE ? fat_create_file_entry((char *)name, vfs)
                             : mkdir((char *)name, vfs)) == 0) {
    return Get_File_Address(name, vfs) != NULL ? VFS_ERROR_EXISTS
                                               : VFS_ERROR_NO_SPACE;
  }
  directory = get_now_dir(vfs);
  struct FAT_FILEINFO *entry =
      type == VFS_NODE_FILE
          ? file_search(name, directory, get_directory_max(directory, vfs))
          : dict_search(name, directory, get_directory_max(directory, vfs));
  if (entry == NULL) {
    return VFS_ERROR_IO;
  }
  fat_make_node(vfs, directory, entry - directory, node);
  return VFS_OK;
}

static int fat_remove_directory(vfs_t *vfs, struct FAT_FILEINFO *parent,
                                const char *name) {
  struct FAT_FILEINFO *entry =
      dict_search(name, parent, get_directory_max(parent, vfs));
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
    if (directory[index].name[0] != 0xe5) {
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
  entry->name[0] = 0xe5;
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
  get_now_dir(vfs) = directory;
  return del((char *)name, vfs) ? VFS_OK : VFS_ERROR_NO_ENTRY;
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
  get_now_dir(vfs) = directory;
  return fat_rename_entry((char *)source_name, (char *)destination_name, vfs)
             ? VFS_OK
             : VFS_ERROR_EXISTS;
}

static void fat_entry_name(const struct FAT_FILEINFO *entry, char name[255]) {
  uint32_t length = 0;
  for (uint32_t index = 0; index < 8 && entry->name[index] != ' '; index++) {
    name[length++] = entry->name[index];
  }
  if (entry->ext[0] != ' ') {
    name[length++] = '.';
    for (uint32_t index = 0; index < 3 && entry->ext[index] != ' '; index++) {
      name[length++] = entry->ext[index];
    }
  }
  name[length] = '\0';
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
    if (entry->name[0] == 0xe5 || entry->name[0] == '+' ||
        ((entry->type & 0x18) != 0 && entry->type != 0x10)) {
      continue;
    }
    char name[255];
    fat_entry_name(entry, name);
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
