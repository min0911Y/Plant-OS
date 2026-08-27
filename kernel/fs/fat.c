// fat.c : fat文件系统的实现
#include <dos.h>
#include <fs.h>
#include <limits.h>
void Fat_DeleteFs(struct vfs_t *vfs);
void *kmalloc(int size);
void kfree(void *p);
void file_savefat(int *fat, int clustno, int length, vfs_t *vfs);
int mkfile(char *name, vfs_t *vfs);
int del(char *cmdline, vfs_t *vfs);
bool Fat_WriteFile(struct vfs_t *vfs, char *path, char *buffer, int size);
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
static lock_t *fat_alloc_lock(void) {
  lock_t *l = malloc(sizeof(lock_t));
  if (l != NULL) {
    lock_init(l);
  }
  return l;
}
static mtask **fat_alloc_lock_owner(void) {
  mtask **owner = malloc(sizeof(mtask *));
  if (owner != NULL) {
    *owner = NULL;
  }
  return owner;
}
static int *fat_alloc_lock_depth(void) {
  int *depth = malloc(sizeof(int));
  if (depth != NULL) {
    *depth = 0;
  }
  return depth;
}
static void fat_op_lock(vfs_t *vfs) {
  if (get_fat_lock(vfs) == NULL || get_fat_lock_owner(vfs) == NULL ||
      get_fat_lock_depth(vfs) == NULL) {
    return;
  }
  if (*(get_fat_lock_owner(vfs)) == current_task()) {
    ++(*get_fat_lock_depth(vfs));
    return;
  }
  lock(get_fat_lock(vfs));
  *(get_fat_lock_owner(vfs)) = current_task();
  *get_fat_lock_depth(vfs) = 1;
}
static void fat_op_unlock(vfs_t *vfs) {
  if (get_fat_lock(vfs) == NULL || get_fat_lock_owner(vfs) == NULL ||
      get_fat_lock_depth(vfs) == NULL ||
      *(get_fat_lock_owner(vfs)) != current_task()) {
    return;
  }
  if (--(*get_fat_lock_depth(vfs)) == 0) {
    *(get_fat_lock_owner(vfs)) = NULL;
    unlock(get_fat_lock(vfs));
  }
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
  for (int i = 1; FindForCount(i, get_dm(vfs).directory_list) != NULL; i++) {
    struct List *l = FindForCount(i, get_dm(vfs).directory_list);
    if ((struct FAT_FILEINFO *)l->val == directory) {
      return (int)FindForCount(i, get_dm(vfs).directory_max_list)->val;
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
        Disk_Read(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                  img + sec_start * get_dm(vfs).ClustnoBytes, vfs->disk_number);
        sec_start += num;
        a = sec;
        num = 1;
        clustno = fat[clustno];
        continue;
      }
    }
    // Disk_Read(sec, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
    //           img + i * get_dm(vfs).ClustnoBytes, vfs->disk_number);
    flag = 1;
    clustno = fat[clustno];
  }
  if (num) {
    Disk_Read(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
              img + sec_start * get_dm(vfs).ClustnoBytes, vfs->disk_number);
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
  //   Disk_Write(sec, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
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
        Disk_Write(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                   img + sec_start * get_dm(vfs).ClustnoBytes,
                   vfs->disk_number);
        sec_start += num;
        a = sec;
        num = 1;
        clustno = fat[clustno];
        continue;
      }
    }
    // Disk_Read(sec, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
    //           img + i * get_dm(vfs).ClustnoBytes, vfs->disk_number);
    flag = 1;
    clustno = fat[clustno];
  }
  if (num) {
    Disk_Write(a, num * get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
               img + sec_start * get_dm(vfs).ClustnoBytes, vfs->disk_number);
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
    Disk_Write(get_dm(vfs).RootDictAddress / get_dm(vfs).SectorBytes,
               get_dm(vfs).RootMaxFiles * 32 / get_dm(vfs).SectorBytes,
               (void *)directory, vfs->disk_number);
  } else {
    for (int i = 1; FindForCount(i, get_dm(vfs).directory_list) != NULL; i++) {
      struct List *list = FindForCount(i, get_dm(vfs).directory_list);
      if (list->val == (uintptr_t)directory) {
        list = FindForCount(i, get_dm(vfs).directory_clustno_list);
        int k = (int)FindForCount(i, get_dm(vfs).directory_max_list)->val * 32 /
                get_dm(vfs).ClustnoBytes;
        for (int j = list->val, l = 0; l != k; l++) {
          if (!fat_data_cluster_valid(vfs, j)) {
            return;
          }
          Disk_Write((get_dm(vfs).FileDataAddress +
                      (j - 2) * get_dm(vfs).ClustnoBytes) /
                         get_dm(vfs).SectorBytes,
                     get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
                     (char *)directory + l * get_dm(vfs).ClustnoBytes,
                     vfs->disk_number);
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
  Disk_Write(get_dm(vfs).Fat1Address / get_dm(vfs).SectorBytes + first_sector,
             sector_count, source, vfs->disk_number);
  Disk_Write(get_dm(vfs).Fat2Address / get_dm(vfs).SectorBytes + first_sector,
             sector_count, source, vfs->disk_number);
}
struct FAT_FILEINFO *file_search(char *name, struct FAT_FILEINFO *finfo,
                                 int max) {
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
struct FAT_FILEINFO *dict_search(char *name, struct FAT_FILEINFO *finfo,
                                 int max) {
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
struct FAT_FILEINFO *Get_File_Address(char *path1, vfs_t *vfs) {
  if (path1 == NULL || vfs == NULL || path1[0] == '\0') {
    return NULL;
  }
  struct FAT_FILEINFO *bmpDict = get_now_dir(vfs);
  size_t path_length = strlen(path1);
  if (path_length >= INT_MAX) {
    return NULL;
  }
  char *path = (char *)malloc(path_length + 1);
  if (path == NULL) {
    return NULL;
  }
  char *bmp = path;
  memcpy(path, path1, path_length + 1);
  strtoupper(path);
  if (strncmp("/", path, 1) == 0) {
    path += 1;
    bmpDict = get_dm(vfs).root_directory;
  }
  if (path[0] == '\\' || path[0] == '/') {
    // 跳过反斜杠和正斜杠
    for (size_t i = 0; i < strlen(path); i++) {
      if (path[i] != '\\' && path[i] != '/') {
        path += i;
        break;
      }
    }
  }
  if (path[0] == '\0') {
    free(bmp);
    return NULL;
  }
  char *temp_name = (char *)malloc(128);
  if (temp_name == NULL) {
    free(bmp);
    return NULL;
  }
  struct FAT_FILEINFO *finfo = get_dm(vfs).root_directory;
  int i = 0;
  while (1) {
    int j;
    size_t length = strlen(path);
    for (j = 0; i < (int)length; i++, j++) {
      if (j >= 127) {
        free(temp_name);
        free(bmp);
        return NULL;
      }
      if (path[i] == '\\' || path[i] == '/') {
        i++;
        break;
      }
      temp_name[j] = path[i];
    }
    temp_name[j] = '\0';
    finfo = dict_search(temp_name, bmpDict, get_directory_max(bmpDict, vfs));
    if (finfo == 0) {
      if (path[i] != '\0') {
        free((void *)temp_name);
        free((void *)bmp);
        return 0;
      }
      finfo = file_search(temp_name, bmpDict, get_directory_max(bmpDict, vfs));
      if (finfo == 0) {
        free((void *)temp_name);
        free((void *)bmp);
        return 0;
      } else {
        goto END;
      }
    } else {
      if (get_clustno(finfo->clustno_high, finfo->clustno_low) != 0) {
        for (int count = 1;
             FindForCount(count, get_dm(vfs).directory_clustno_list) != NULL;
             count++) {
          struct List *list =
              FindForCount(count, get_dm(vfs).directory_clustno_list);
          if (get_clustno(finfo->clustno_high, finfo->clustno_low) ==
              list->val) {
            list = FindForCount(count, get_dm(vfs).directory_list);
            bmpDict = (struct FAT_FILEINFO *)list->val;
            break;
          }
        }
      } else {
        bmpDict = get_dm(vfs).root_directory;
      }
      clean(temp_name, 128);
    }
  }
END:
  free((void *)temp_name);
  free((void *)bmp);
  return finfo;
}
struct FAT_FILEINFO *Get_dictaddr(char *path1, vfs_t *vfs) {
  if (path1 == NULL || vfs == NULL) {
    return NULL;
  }
  struct FAT_FILEINFO *bmpDict = get_now_dir(vfs);
  if (path1[0] == '\0') {
    return bmpDict;
  }
  size_t path_length = strlen(path1);
  if (path_length >= INT_MAX) {
    return NULL;
  }
  char *path = (char *)malloc(path_length + 1);
  if (path == NULL) {
    return NULL;
  }
  char *bmp = path;
  memcpy(path, path1, path_length + 1);
  strtoupper(path);
  if (strncmp("/", path, 1) == 0) {
    path += 1;
    bmpDict = get_dm(vfs).root_directory;
  }
  if (path[0] == '\\' || path[0] == '/') {
    // 跳过反斜杠和正斜杠
    for (size_t i = 0; i < strlen(path); i++) {
      if (path[i] != '\\' && path[i] != '/') {
        path += i;
        break;
      }
    }
  }
  if (path[0] == '\0') {
    free(bmp);
    return bmpDict;
  }
  char *temp_name = (char *)malloc(128);
  if (temp_name == NULL) {
    free(bmp);
    return NULL;
  }
  struct FAT_FILEINFO *finfo;
  int i = 0;
  while (1) {
    int j;
    size_t length = strlen(path);
    for (j = 0; i < (int)length; i++, j++) {
      if (j >= 127) {
        free(temp_name);
        free(bmp);
        return NULL;
      }
      if (path[i] == '\\' || path[i] == '/') {
        i++;
        break;
      }
      temp_name[j] = path[i];
    }
    temp_name[j] = '\0';
    finfo = dict_search(temp_name, bmpDict, get_directory_max(bmpDict, vfs));
    if (finfo == 0) {
      if (path[i] != 0) {
        bmpDict = NULL;
      }
      goto END;
    } else {
      if (get_clustno(finfo->clustno_high, finfo->clustno_low) != 0) {
        for (int count = 1;
             FindForCount(count, get_dm(vfs).directory_clustno_list) != NULL;
             count++) {
          struct List *list =
              FindForCount(count, get_dm(vfs).directory_clustno_list);
          if (get_clustno(finfo->clustno_high, finfo->clustno_low) ==
              list->val) {
            list = FindForCount(count, get_dm(vfs).directory_list);
            bmpDict = (struct FAT_FILEINFO *)list->val;
            break;
          }
        }
      } else {
        bmpDict = get_dm(vfs).root_directory;
      }
      clean(temp_name, 128);
      if (path[i] == '\0') {
        goto END;
      }
    }
  }
END:
  free((void *)temp_name);
  free((void *)bmp);
  return bmpDict;
}
struct fat_cursor_update {
  List *directory_list;
  uintptr_t old_directory;
  struct FAT_FILEINFO *new_directory;
};

static void fat_update_directory_cursor(vfs_t *instance, void *context) {
  struct fat_cursor_update *update = context;
  if (instance->cache != NULL &&
      get_dm(instance).directory_list == update->directory_list &&
      (uintptr_t)get_now_dir(instance) == update->old_directory) {
    get_now_dir(instance) = update->new_directory;
  }
}

static void fat_replace_directory_cursor(vfs_t *vfs,
                                         uintptr_t old_directory,
                                         struct FAT_FILEINFO *new_directory) {
  struct fat_cursor_update update = {
      .directory_list = get_dm(vfs).directory_list,
      .old_directory = old_directory,
      .new_directory = new_directory,
  };
  vfs_visit_mount_instances(vfs, fat_update_directory_cursor, &update);
}

static bool fat_make_short_name(const char *path, char result[11]) {
  const char *name = path;
  for (const char *cursor = path; *cursor != '\0'; cursor++) {
    if (*cursor == '/' || *cursor == '\\') {
      name = cursor + 1;
    }
  }
  if (*name == '\0') {
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
    for (int i = 1; FindForCount(i, get_dm(vfs).directory_list) != NULL; i++) {
      if ((struct FAT_FILEINFO *)(uintptr_t)FindForCount(
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
    FindForCount(parent_list_index, get_dm(vfs).directory_list)->val =
        (uintptr_t)parent;
    FindForCount(parent_list_index, get_dm(vfs).directory_max_list)->val =
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

  Disk_Write((get_dm(vfs).FileDataAddress +
              (directory_cluster - 2) * get_dm(vfs).ClustnoBytes) /
                 get_dm(vfs).SectorBytes,
             get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
             directory_buffer, vfs->disk_number);
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
int deldir(char *path, vfs_t *vfs) {
  struct FAT_FILEINFO *finfo = Get_dictaddr(path, vfs);
  if (finfo == Get_dictaddr(".", vfs)) {
    return 0;
  }
  struct FAT_FILEINFO *f = get_now_dir(vfs);
  get_now_dir(vfs) = finfo;
  for (int i = 2; finfo[i].name[0] != '\0'; i++) {
    if (finfo[i].type == 0x10 && finfo[i].name[0] != 0xe5) {
      char s[30];
      int j = 0;
      for (; finfo[i].name[j] != ' '; j++) {
        s[j] = finfo[i].name[j];
      }
      s[j] = '\0';
      // printk("(CALL)DEL DIR:%s\n", s);
      if (deldir(s, vfs) == -1) {
        return 0;
      }
      // return -1;
    }
  }
  for (int i = 2; finfo[i].name[0] != '\0'; i++) {
    if (finfo[i].name[0] != 0xe5 && finfo[i].type != 0x10) {
      char s[30];
      int p = 0;
      for (int j = 0; finfo[i].name[j] != ' '; j++, p++) {
        s[p] = finfo[i].name[j];
      }
      if (finfo[i].ext[0] != ' ') {
        s[p++] = '.';
        for (int j = 0; finfo[i].ext[j] != ' ' || j != 3; j++, p++) {
          s[p] = finfo[i].ext[j];
        }
      }
      s[p] = '\0';
      // printk("(IN)DEL FILE:%s\n", s);
      if (del(s, vfs) == -1) {
        return 0;
      }
    }
  }
  get_now_dir(vfs) = f;
  // printk("(IN)DEL SELF\n");
  struct FAT_FILEINFO *root_finfo;
  if (finfo[1].clustno_low == 0) {
    root_finfo = get_dm(vfs).root_directory;
  } else {
    for (int i = 1; FindForCount(i, get_dm(vfs).directory_clustno_list) != NULL;
         i++) {
      if (FindForCount(i, get_dm(vfs).directory_clustno_list)->val ==
          finfo[1].clustno_low) {
        root_finfo =
            (struct FAT_FILEINFO *)FindForCount(i, get_dm(vfs).directory_list)
                ->val;
        // printk("FIND ROOT %08x\n", root_finfo);
      }
    }
  }
  for (int i = 0; root_finfo[i].name[0] != '\0'; i++) {
    // printk("ROOT FILE:%s\n", root_finfo[i].name);
    if (root_finfo[i].clustno_low == finfo[0].clustno_low) {
      root_finfo[i].name[0] = 0xe5;
      break;
    }
  }
  get_dm(vfs).fat[get_clustno(finfo->clustno_high, finfo->clustno_low)] = 0;
  get_dm(vfs)
      .FatClustnoFlags[get_clustno(finfo->clustno_high, finfo->clustno_low)] =
      false;
  file_saveinfo(Get_dictaddr(path, vfs), vfs);
  file_savefat(get_dm(vfs).fat,
               get_clustno(finfo->clustno_high, finfo->clustno_low), 1, vfs);
  return 1;
}
int mkfile(char *name, vfs_t *vfs) {
  logk("mkfile : %s\n", name);
  char s[12];
  int i, j;
  struct FAT_FILEINFO *finfo = Get_dictaddr(name, vfs);
  logk("finfo = %08x\n", finfo);
  if (finfo == NULL) {
    return 0;
  }
  int max = get_directory_max(finfo, vfs);

  char *path = name;
  for (i = strlen(name); i >= 0; i--) {
    if (name[i] == '/' || name[i] == '\\') {
      name += i + 1;
      break;
    }
  }

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
      for (int j = 1; FindForCount(j, get_dm(vfs).directory_list) != NULL;
           j++) {
        struct List *l = FindForCount(j, get_dm(vfs).directory_list);
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
          FindForCount(j, get_dm(vfs).directory_max_list)->val = max;
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
  file_saveinfo(Get_dictaddr(path, vfs), vfs);
  file_savefat(get_dm(vfs).fat,
               get_clustno(finfo->clustno_high, finfo->clustno_low), 1, vfs);
  return 1;
}
int changedict(char *dictname, vfs_t *vfs) {
  if (dictname == NULL || vfs == NULL || vfs->path == NULL) {
    return 0;
  }
  strtoupper(dictname);

  if (strcmp(dictname, "/") == 0) {
    while (vfs->path->ctl->all != 0) {
      free((void *)(uintptr_t)FindForCount(vfs->path->ctl->all, vfs->path)
               ->val);
      DeleteVal(vfs->path->ctl->all, vfs->path);
    }
    get_now_dir(vfs) = get_dm(vfs).root_directory;
    return 1;
  }
  if (strcmp(dictname, ".") == 0) {
    return 1;
  }
  if (strcmp(dictname, "..") == 0 && vfs->path->ctl->all == 0) {
    return 0;
  }

  struct FAT_FILEINFO *finfo = dict_search(
      dictname, get_now_dir(vfs), get_directory_max(get_now_dir(vfs), vfs));
  if (finfo == NULL) {
    return 0;
  }
  unsigned int cluster =
      get_clustno(finfo->clustno_high, finfo->clustno_low);
  if (cluster == 0) {
    while (vfs->path->ctl->all != 0) {
      free((void *)(uintptr_t)FindForCount(vfs->path->ctl->all, vfs->path)
               ->val);
      DeleteVal(vfs->path->ctl->all, vfs->path);
    }
    get_now_dir(vfs) = get_dm(vfs).root_directory;
    return 1;
  }

  struct FAT_FILEINFO *next_directory = NULL;
  for (int count = 1;
       FindForCount(count, get_dm(vfs).directory_clustno_list) != NULL;
       count++) {
    struct List *cluster_entry =
        FindForCount(count, get_dm(vfs).directory_clustno_list);
    if (cluster == cluster_entry->val) {
      struct List *directory_entry =
          FindForCount(count, get_dm(vfs).directory_list);
      if (directory_entry != NULL) {
        next_directory = (struct FAT_FILEINFO *)(uintptr_t)directory_entry->val;
      }
      break;
    }
  }
  if (next_directory == NULL) {
    return 0;
  }

  if (strcmp(dictname, "..") == 0) {
    struct List *path_entry =
        FindForCount(vfs->path->ctl->all, vfs->path);
    if (path_entry == NULL) {
      return 0;
    }
    free((void *)(uintptr_t)path_entry->val);
    DeleteVal(vfs->path->ctl->all, vfs->path);
  } else {
    size_t length = strlen(dictname);
    if (length >= (size_t)INT_MAX) {
      return 0;
    }
    char *path_entry = malloc((int)length + 1);
    if (path_entry == NULL) {
      return 0;
    }
    memcpy(path_entry, dictname, length + 1);
    if (!AddVal((uintptr_t)path_entry, vfs->path)) {
      free(path_entry);
      return 0;
    }
  }
  get_now_dir(vfs) = next_directory;
  return 1;
}
int rename(char *src_name, char *dst_name, vfs_t *vfs) {
  strtoupper(src_name);
  strtoupper(dst_name);
  if (strcmp(src_name, dst_name) == 0) {
    return 1;
  }

  char *src_base = src_name;
  char *dst_base = dst_name;
  for (char *cursor = src_name; *cursor != '\0'; cursor++) {
    if (*cursor == '/' || *cursor == '\\') {
      src_base = cursor + 1;
    }
  }
  for (char *cursor = dst_name; *cursor != '\0'; cursor++) {
    if (*cursor == '/' || *cursor == '\\') {
      dst_base = cursor + 1;
    }
  }
  size_t src_parent_length = (size_t)(src_base - src_name);
  size_t dst_parent_length = (size_t)(dst_base - dst_name);
  if (src_parent_length != dst_parent_length ||
      memcmp(src_name, dst_name, src_parent_length) != 0 || *dst_base == '\0' ||
      strcmp(dst_base, ".") == 0 || strcmp(dst_base, "..") == 0) {
    return 0;
  }

  char short_name[11];
  memset(short_name, ' ', sizeof(short_name));
  size_t name_length = 0;
  size_t extension_length = 0;
  bool extension = false;
  for (const char *cursor = dst_base; *cursor != '\0'; cursor++) {
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
  // A,B盘——软盘
  // C盘——IDE/SATA硬盘主分区
  // D,E,F...盘——IDE/USB/SATA存储介质/分区/虚拟磁盘
  FILE *fp = fopen("/boot.bin", "r"); // or \boot.bin
  FILE *fp_32 = fopen("/boot32.bin", "r");
  if (fp == 0 || fp_32 == 0) {
    return 0;
  }
  void *read_in = malloc(512);
  if (!(drive - 'A')) {
    fread(read_in, fp->fileSize, 1, fp);
    write_floppy_for_ths(0, 0, 1, read_in, 1);
    unsigned int *fat = (unsigned int *)malloc(9 * 512);
    fat[0] = 0x00fffff0;
    write_floppy_for_ths(0, 0, 2, (unsigned char *)fat, 9);
    write_floppy_for_ths(0, 0, 11, (unsigned char *)fat, 9);
    free((void *)fat);
    void *null_sec = malloc(512);
    for (int i = 0; i < 224 * 32 / 512; i++) {
      int t, h, s;
      block2hts(19 + i, &t, &h, &s);
      write_floppy_for_ths(t, h, s, null_sec, 1);
    }
    free(null_sec);
  } else if (drive != 'B') {
    if (!DiskReady(drive)) {
      return 0;
    }
    if (disk_Size(drive) <= 2097152) { // 2MB及以下 fat12
      fread(read_in, fp->fileSize, 1, fp);
      *(unsigned char *)(&((unsigned char *)read_in)[BPB_SecPerClus]) = 1;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_RootEntCnt]) = 224;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_TotSec16]) =
          disk_Size(drive) / 512;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_TotSec32]) = 0;
      unsigned short fatsz = (disk_Size(drive) - 1) / 512 * 3 / 2 / 512 + 1;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_FATSz16]) = fatsz;
      *(unsigned char *)(&((unsigned char *)read_in)[BS_DrvNum]) =
          drive - 'C' + 0x80;
      memcpy((void *)&((unsigned char *)read_in)[BS_FileSysType],
             (void *)"FAT12   ", 8);
      srand(time());
      *(unsigned int *)(&((unsigned char *)read_in)[BS_VolD]) = rand();
      memcpy((void *)&((unsigned char *)read_in)[BS_VolLab],
             (void *)"POWERINTDOS", 11);
      Disk_Write(0, 1, read_in, drive);
      unsigned int *fat = (unsigned int *)malloc(fatsz * 512);
      fat[0] = 0x00fffff0;
      Disk_Write(1, fatsz, (void *)fat, drive);
      Disk_Write(1 + fatsz, fatsz, (void *)fat, drive);
      free((void *)fat);
      void *null_sec = malloc(512);
      clean((char *)null_sec, 512);
      for (int i = 0; i != 224 * 32 / 512; i++) {
        Disk_Write(1 + fatsz * 2 + i, 1, null_sec, drive);
      }
      free(null_sec);
      // page_free((void*)info, 256 * sizeof(short));
    } else if (disk_Size(drive) > 2097152 &&
               disk_Size(drive) <= 2147483648) { // 2MB~2GB fat16
      fread(read_in, fp->fileSize, 1, fp);
      unsigned int clustno_size =
          ((disk_Size(drive) - 1) / 65536 + 512) / 512 * 512;
      *(unsigned char *)(&((unsigned char *)read_in)[BPB_SecPerClus]) =
          clustno_size / 512;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_RootEntCnt]) =
          14 * clustno_size / 32;
      if (disk_Size(drive) / 512 > 65535) {
        *(unsigned short *)(&((unsigned char *)read_in)[BPB_TotSec16]) = 0;
      } else {
        *(unsigned short *)(&((unsigned char *)read_in)[BPB_TotSec16]) =
            disk_Size(drive) / 512;
      }
      *(unsigned int *)(&((unsigned char *)read_in)[BPB_TotSec32]) =
          disk_Size(drive) / 512;
      unsigned short fatsz =
          (disk_Size(drive) - 1) / clustno_size * 2 / 512 + 1;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_FATSz16]) = fatsz;
      *(unsigned char *)(&((unsigned char *)read_in)[BS_DrvNum]) =
          drive - 'C' + 0x80;
      memcpy((void *)&((unsigned char *)read_in)[BS_FileSysType],
             (void *)"FAT16   ", 8);
      srand(time());
      *(unsigned int *)(&((unsigned char *)read_in)[BS_VolD]) = rand();
      memcpy((void *)&((unsigned char *)read_in)[BS_VolLab],
             (void *)"POWERINTDOS", 11);
      Disk_Write(0, 1, read_in, drive);
      unsigned short *fat = (unsigned short *)malloc(fatsz * 512);
      fat[0] = 0xfff0;
      fat[1] = 0xffff;
      Disk_Write(1, fatsz, (void *)fat, drive);
      Disk_Write(1 + fatsz, fatsz, (void *)fat, drive);
      free((void *)fat);
      void *null_sec = malloc(512);
      clean((char *)null_sec, 512);
      for (unsigned i = 0; i < 14 * clustno_size / 512; i++) {
        Disk_Write(1 + fatsz * 2 + i, 1, null_sec, drive);
      }
      free(null_sec);
    } else if (disk_Size(drive) > 2147483648) { // 2GB以上 fat32
      fread(read_in, fp_32->fileSize, 1, fp_32);
      unsigned int clustno_size =
          (disk_Size(drive) - 1) / 268435456 * 512 + 512;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_RsvdSecCnt]) = 1;
      *(unsigned char *)(&((unsigned char *)read_in)[BPB_SecPerClus]) =
          clustno_size / 512;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_RootEntCnt]) = 0;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_TotSec16]) = 0;
      *(unsigned int *)(&((unsigned char *)read_in)[BPB_TotSec32]) =
          disk_Size(drive) / 512;
      unsigned int fatsz = (disk_Size(drive) - 1) / clustno_size * 4 / 512 + 1;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_FATSz16]) = 0;
      *(unsigned int *)(&((unsigned char *)read_in)[BPB_FATSz32]) = fatsz;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_ExtFlags]) = 0;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_FSVer]) = 0;
      *(unsigned int *)(&((unsigned char *)read_in)[BPB_RootClus]) = 2;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_FSInfo]) = 0;
      *(unsigned short *)(&((unsigned char *)read_in)[BPB_BkBootSec]) = 0;
      *(unsigned long long *)(&((unsigned char *)read_in)[BPB_Reserved]) = 0;
      *(unsigned char *)(&(
          (unsigned char *)read_in)[BS_DrvNum + BPB_Fat32ExtByts]) =
          drive - 'C' + 0x80;
      *(unsigned char *)(&(
          (unsigned char *)read_in)[BS_Reserved1 + BPB_Fat32ExtByts]) = 0;
      *(unsigned char *)(&(
          (unsigned char *)read_in)[BS_BootSig + BPB_Fat32ExtByts]) = 0x29;
      memcpy((void *)&(
                 (unsigned char *)read_in)[BS_FileSysType + BPB_Fat32ExtByts],
             (void *)"FAT32   ", 8);
      srand(time());
      *(unsigned int *)(&(
          (unsigned char *)read_in)[BS_VolD + BPB_Fat32ExtByts]) = rand();
      memcpy((void *)&((unsigned char *)read_in)[BS_VolLab + BPB_Fat32ExtByts],
             (void *)"POWERINTDOS", 11);
      Disk_Write(0, 1, read_in, drive);
      unsigned int *fat = (unsigned int *)malloc(fatsz * 512);
      fat[0] = 0xffffff0;
      fat[1] = 0xfffffff;
      fat[2] = 0xfffffff;
      Disk_Write(1, fatsz, (void *)fat, drive);
      Disk_Write(1 + fatsz, fatsz, (void *)fat, drive);
      free((void *)fat);
      void *null_sec = malloc(512);
      clean((char *)null_sec, 512);
      Disk_Write(1 + fatsz * 2, 1, null_sec, drive);
      free(null_sec);
    }
  }
  free(read_in);
  fclose(fp);
  fclose(fp_32);
  return 1;
}
int attrib(char *filename, ftype type, struct vfs_t *vfs) {
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
bool fat_InitFS(struct vfs_t *vfs, uint8_t disk_number) {
  void *boot_sector = NULL;
  vfs->cache = malloc(sizeof(fat_cache));
  if (vfs->cache == NULL) {
    return false;
  }
  memset(vfs->cache, 0, sizeof(fat_cache));
  get_fat_lock(vfs) = fat_alloc_lock();
  get_fat_lock_owner(vfs) = fat_alloc_lock_owner();
  get_fat_lock_depth(vfs) = fat_alloc_lock_depth();
  if (get_fat_lock(vfs) == NULL || get_fat_lock_owner(vfs) == NULL ||
      get_fat_lock_depth(vfs) == NULL) {
    goto fail;
  }
  boot_sector = malloc(512);
  if (boot_sector == NULL) {
    goto fail;
  }
  unsigned char *boot = boot_sector;
  Disk_Read(0, 1, boot_sector, disk_number);

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
      (unsigned int)malloc((int)cache_allocation_size);
  if (get_dm(vfs).ADR_DISKIMG == 0) {
    goto fail;
  }

  Disk_Read(0, sec, (void *)get_dm(vfs).ADR_DISKIMG, disk_number);

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
         (void *)get_dm(vfs).ADR_DISKIMG + get_dm(vfs).RootDictAddress,
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
        Disk_Read(sec1, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
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
       FindForCount(i, (struct List *)get_dm(vfs).directory_list) != NULL;
       i++) {
    struct List *list =
        FindForCount(i, (struct List *)get_dm(vfs).directory_list);
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
          Disk_Read(sec1, get_dm(vfs).ClustnoBytes / get_dm(vfs).SectorBytes,
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
  Fat_DeleteFs(vfs);
  return false;
}
bool Fat_CopyCache(struct vfs_t *dest, struct vfs_t *src) {
  dest->cache = malloc(sizeof(fat_cache));
  if (dest->cache == NULL) {
    return false;
  }
  memcpy(dest->cache, src->cache, sizeof(fat_cache));
  get_fat_lock(dest) = get_fat_lock(src);
  get_fat_lock_owner(dest) = get_fat_lock_owner(src);
  get_fat_lock_depth(dest) = get_fat_lock_depth(src);
  return true;
}
static void Fat_ReleaseCache(struct vfs_t *vfs) {
  free(vfs->cache);
  vfs->cache = NULL;
}
bool Fat_cd(struct vfs_t *vfs, char *dictName) {
  bool result;
  fat_op_lock(vfs);
  result = changedict(dictName, vfs);
  fat_op_unlock(vfs);
  return result;
}
bool Fat_ReadFile(struct vfs_t *vfs, char *path, char *buffer) {
  bool result = false;
  fat_op_lock(vfs);
  struct FAT_FILEINFO *finfo;
  finfo = Get_File_Address(path, vfs);
  if (finfo == 0) {
    result = false;
  } else {
    result = finfo->size <= INT_MAX &&
             file_loadfile(
                 get_clustno(finfo->clustno_high, finfo->clustno_low),
                 finfo->size, buffer, get_dm(vfs).fat, vfs);
  }
  fat_op_unlock(vfs);
  return result;
}
bool Fat_WriteFile(struct vfs_t *vfs, char *path, char *buffer, int size) {
  fat_op_lock(vfs);
  struct FAT_FILEINFO *finfo = Get_File_Address(path, vfs);
  bool result = finfo != NULL && size >= 0 && (size == 0 || buffer != NULL) &&
                file_savefile(
                    get_clustno(finfo->clustno_high, finfo->clustno_low), size,
                    buffer, get_dm(vfs).fat, get_dm(vfs).FatClustnoFlags, vfs);
  if (result) {
    finfo->size = size;
    file_saveinfo(Get_dictaddr(path, vfs), vfs);
  }
  fat_op_unlock(vfs);
  return result;
}
static void fat_free_file_list(List *files) {
  if (files == NULL) {
    return;
  }
  for (int i = 1; FindForCount(i, files) != NULL; i++) {
    free((void *)(uintptr_t)FindForCount(i, files)->val);
  }
  DeleteList(files);
}
List *Fat_ListFile(struct vfs_t *vfs, char *dictpath) {
  fat_op_lock(vfs);
  if (dictpath[0] != '\0' && strcmp(dictpath, "/") != 0) {
    struct FAT_FILEINFO *entry = Get_File_Address(dictpath, vfs);
    if (entry == NULL || entry->type != 0x10) {
      fat_op_unlock(vfs);
      return NULL;
    }
  }
  struct FAT_FILEINFO *finfo = Get_dictaddr(dictpath, vfs);
  if (finfo == NULL) {
    fat_op_unlock(vfs);
    return NULL;
  }
  List *result = NewList();
  if (result == NULL) {
    fat_op_unlock(vfs);
    return NULL;
  }
  char s[30];
  for (int i = 0; i != 30; i++) {
    s[i] = 0;
  }
  for (int i = 0; i < get_directory_max(finfo, vfs); i++) {
    if (finfo[i].name[0] == 0x00) {
      break;
    }
    if (finfo[i].name[0] != 0xe5) {
      if ((finfo[i].type & 0x18) == 0 || finfo[i].type == 0x10) {
        for (int j = 0; j < 8; j++) {
          s[j] = finfo[i].name[j];
        }
        s[8] = '.';
        if (finfo[i].ext[0] == ' ') {
          s[8] = 0;
        } else {
          s[9] = finfo[i].ext[0];
          s[10] = finfo[i].ext[1];
          s[11] = finfo[i].ext[2];
        }
        if (s[0] != '+') {
          vfs_file *d = malloc(sizeof(vfs_file));
          if (d == NULL) {
            fat_free_file_list(result);
            fat_op_unlock(vfs);
            return NULL;
          }
          if (finfo[i].type == 0x10) {
            d->type = DIR;
          } else if (finfo[i].type == 0x20) {
            d->type = FLE;
          } else if (finfo[i].type == 0x01) {
            d->type = RDO;
          } else if (finfo[i].type == 0x02) {
            d->type = HID;
          } else if (finfo[i].type == 0x04) {
            d->type = SYS;
          }
          d->year = (finfo[i].update_date & 65024) >> 9;
          d->year += 1980;
          d->month = (finfo[i].update_date & 480) >> 5;
          d->day = finfo[i].update_date & 31;
          d->hour = (finfo[i].update_time & 63488) >> 11;
          d->minute = (finfo[i].update_time & 2016) >> 5;
          d->size = finfo[i].size;
          int q = 0;
          for (int k = 0; k < 12 && s[k] != 0; ++k) {
            if (s[k] != ' ') {
              d->name[q++] = s[k];
            }
          }
          d->name[q] = 0;
          // printk("d->name = %s\n", d->name);
          if (!AddVal((uintptr_t)d, result)) {
            free(d);
            fat_free_file_list(result);
            fat_op_unlock(vfs);
            return NULL;
          }
        }
      }
    }
  }
  fat_op_unlock(vfs);
  return result;
}
bool Fat_RenameFile(struct vfs_t *vfs, char *filename, char *filename_of_new) {
  bool result;
  fat_op_lock(vfs);
  result = rename(filename, filename_of_new, vfs);
  fat_op_unlock(vfs);
  return result;
}
bool Fat_CreateFile(struct vfs_t *vfs, char *filename) {
  bool result;
  fat_op_lock(vfs);
  result = mkfile(filename, vfs);
  fat_op_unlock(vfs);
  return result;
}
void Fat_DeleteFs(struct vfs_t *vfs) {
  if (vfs->cache == NULL) {
    return;
  }
  free((void *)get_dm(vfs).ADR_DISKIMG);
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
    for (int i = 1; FindForCount(i, get_dm(vfs).directory_list) != NULL; i++) {
      free((void *)(uintptr_t)FindForCount(i, get_dm(vfs).directory_list)->val);
    }
    DeleteList(get_dm(vfs).directory_list);
  }
  if (get_fat_lock(vfs) != NULL) {
    free(get_fat_lock(vfs));
    get_fat_lock(vfs) = NULL;
  }
  if (get_fat_lock_owner(vfs) != NULL) {
    free(get_fat_lock_owner(vfs));
    get_fat_lock_owner(vfs) = NULL;
  }
  if (get_fat_lock_depth(vfs) != NULL) {
    free(get_fat_lock_depth(vfs));
    get_fat_lock_depth(vfs) = NULL;
  }
  free(vfs->cache);
  vfs->cache = NULL;
}
bool Fat_Check(uint8_t disk_number) {
  if (!DiskReady(disk_number)) {
    return false;
  }
  uint8_t *boot_sec = malloc(512);
  if (boot_sec == NULL) {
    return false;
  }
  Disk_Read(0, 1, boot_sec, disk_number);
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
bool Fat_DelFile(struct vfs_t *vfs, char *path) {
  bool result;
  fat_op_lock(vfs);
  result = del(path, vfs);
  fat_op_unlock(vfs);
  return result;
}
bool Fat_DelDict(struct vfs_t *vfs, char *path) {
  bool result;
  fat_op_lock(vfs);
  result = deldir(path, vfs);
  fat_op_unlock(vfs);
  return result;
}
int Fat_FileSize(struct vfs_t *vfs, char *filename) {
  int result = -1;
  fat_op_lock(vfs);
  struct FAT_FILEINFO *finfo = Get_File_Address(filename, vfs);
  if (finfo != NULL) {
    result = finfo->size;
  }
  fat_op_unlock(vfs);
  return result;
}
bool Fat_Format(uint8_t disk_number) { return format(disk_number); }
bool Fat_CreateDict(struct vfs_t *vfs, char *filename) {
  fat_op_lock(vfs);
  bool result = mkdir(filename, vfs);
  fat_op_unlock(vfs);
  return result;
}
bool Fat_Attrib(struct vfs_t *vfs, char *filename, ftype type) {
  bool result;
  fat_op_lock(vfs);
  result = attrib(filename, type, vfs);
  fat_op_unlock(vfs);
  return result;
}
vfs_file *Fat_FileInfo(struct vfs_t *vfs, char *filename) {
  fat_op_lock(vfs);
  struct FAT_FILEINFO *finfo = Get_File_Address(filename, vfs);
  if (finfo == NULL) {
    fat_op_unlock(vfs);
    return (vfs_file *)NULL;
  }
  vfs_file *result = (vfs_file *)malloc(sizeof(vfs_file));
  char s[30];
  for (int i = 0; i != 30; i++) {
    s[i] = 0;
  }
  for (int i = 0; i < 8; i++) {
    s[i] = finfo->name[i];
  }
  s[8] = '.';
  if (finfo->ext[0] == ' ') {
    s[8] = 0;
  } else {
    s[9] = finfo->ext[0];
    s[10] = finfo->ext[1];
    s[11] = finfo->ext[2];
  }
  int i = 0;
  for (int j = 0; i < 12 && s[i] != 0; i++) {
    if (s[i] != ' ') {
      result->name[j++] = s[i];
    }
  }
  s[i] = '\0';
  if (finfo->type == 0x10) {
    result->type = DIR;
  } else if (finfo->type == 0x20) {
    result->type = FLE;
  } else if (finfo->type == 0x01) {
    result->type = RDO;
  } else if (finfo->type == 0x02) {
    result->type = HID;
  } else if (finfo->type == 0x04) {
    result->type = SYS;
  }
  result->year = (finfo->update_date & 65024) >> 9;
  result->year += 1980;
  result->month = (finfo->update_date & 480) >> 5;
  result->day = finfo->update_date & 31;
  result->hour = (finfo->update_time & 63488) >> 11;
  result->minute = (finfo->update_time & 2016) >> 5;
  result->size = finfo->size;
  fat_op_unlock(vfs);
  return result;
}
void Register_fat_fileSys() {
  vfs_t fs = {0};
  fs.flag = 1;
  fs.cache = NULL;
  strcpy(fs.FSName, "FAT");
  fs.CopyCache = Fat_CopyCache;
  fs.ReleaseCache = Fat_ReleaseCache;
  fs.Format = Fat_Format;
  fs.CreateFile = Fat_CreateFile;
  fs.CreateDict = Fat_CreateDict;
  fs.DelDict = Fat_DelDict;
  fs.DelFile = Fat_DelFile;
  fs.ReadFile = Fat_ReadFile;
  fs.WriteFile = Fat_WriteFile;
  fs.DeleteFs = Fat_DeleteFs;
  fs.cd = Fat_cd;
  fs.FileSize = Fat_FileSize;
  fs.Check = Fat_Check;
  fs.ListFile = Fat_ListFile;
  fs.InitFs = fat_InitFS;
  fs.RenameFile = Fat_RenameFile;
  fs.Attrib = Fat_Attrib;
  fs.FileInfo = Fat_FileInfo;
  vfs_register_fs(fs);
}
