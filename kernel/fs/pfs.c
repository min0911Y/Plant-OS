// libpfs
#include <dos.h>
#include <calendar.h>
#include <mstr.h>
#include <pfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
void pfs_DeleteFs(struct vfs_t *vfs);
vfs_file *pfs_FileInfo(struct vfs_t *vfs, char *filename);

void pfs_flush_bitmap(vfs_t *vfs);
void pfs_get_file_index_by_path(vfs_t *vfs, char *path, uint32_t start_block,
                                uint32_t *err, uint32_t *idx,
                                uint32_t *dict_block);
void pfs_delete_name_link(vfs_t *vfs, uint32_t next, uint32_t dict_block);

void pfs_read_block(pfs_t *pfs, uint32_t lba, uint32_t numbers, void *buff) {
  if (!buff) {
    return;
  }
  Disk_Read(lba, numbers, buff, pfs->disk_number);
}
void pfs_write_block(pfs_t *pfs, uint32_t lba, uint32_t numbers, void *buff) {
  if (!buff) {
    return;
  }
  Disk_Write(lba, numbers, buff, pfs->disk_number);
}
#define now_pfs_t ((pfs_t *)(vfs->cache))
static bool pfs_write_and_verify(pfs_t *pfs, uint32_t lba, uint32_t sectors,
                                 const void *buffer) {
  if (pfs == NULL || pfs->read_block == NULL || pfs->write_block == NULL ||
      buffer == NULL || sectors == 0 || sectors > INT_MAX / 512) {
    return false;
  }
  size_t size = sectors * 512;
  void *verification = malloc(size);
  if (verification == NULL) {
    return false;
  }
  pfs->write_block(pfs, lba, sectors, (void *)buffer);
  pfs->read_block(pfs, lba, sectors, verification);
  bool matches = memcmp(buffer, verification, size) == 0;
  free(verification);
  return matches;
}
/*
  @brief 格式化磁盘为pfs
 */
bool pfs_format(pfs_t p, const char volid[16]) {
  if (volid == NULL || p.read_block == NULL || p.write_block == NULL) {
    return false;
  }
  uint8_t mbr[512] = {0};
  FILE *fp = fopen("/boot_pfs.bin", "rb");
  if (fp == NULL || fp->fileSize < sizeof(mbr) ||
      fread(mbr, 1, sizeof(mbr), fp) != sizeof(mbr)) {
    if (fp != NULL) {
      fclose(fp);
    }
    return false;
  }
  fclose(fp);
  uint32_t loader_size = vfs_filesize("/dosldr.bin");
  if (loader_size == UINT_MAX || loader_size == 0 ||
      loader_size > INT_MAX - 511) {
    return false;
  }
  uint32_t loader_sectors = (loader_size + 511) / 512;
  if (loader_sectors > UINT_MAX - 91) {
    return false;
  }
  uint32_t reserved_loader_sectors = (loader_sectors + 91) / 92 * 92;
  if (reserved_loader_sectors > USHRT_MAX) {
    return false;
  }
  p.resd_sec_start = 1;
  p.resd_sec_end = p.resd_sec_start + reserved_loader_sectors;
  p.sec_bitmap_start = p.resd_sec_end;
  p.first_sec_of_bitmap = p.sec_bitmap_start + 1;
  if (p.first_sec_of_bitmap >= disk_Size(p.disk_number) / 512) {
    return false;
  }

  pfs_mbr *pm = (pfs_mbr *)mbr;
  pm->resd_sector_start = p.resd_sec_start;
  pm->resd_sector_end = p.resd_sec_end;
  pm->sec_bitmap_start = p.sec_bitmap_start;
  pm->sign[0] = 'P';
  pm->sign[1] = 'F';
  pm->sign[2] = 'S';
  pm->sign[3] = '\xff';
  pm->first_sector_of_bitmap = p.first_sec_of_bitmap;
  pm->root_dict_block = 0;
  memcpy(pm->volid, volid, 16);
  uint8_t bitmap[512] = {0};
  bitmap[0] = 1; // 默认有一个目录区
  uint8_t root_dict[512] = {0};
  uint32_t loader_buffer_size = reserved_loader_sectors * 512;
  char *dosldr = malloc(loader_buffer_size);
  if (dosldr == NULL) {
    return false;
  }
  memset(dosldr, 0, loader_buffer_size);
  if (!vfs_readfile("/dosldr.bin", dosldr)) {
    free(dosldr);
    return false;
  }
  bool written = pfs_write_and_verify(&p, 0, 1, mbr) &&
                 pfs_write_and_verify(&p, pm->sec_bitmap_start, 1, bitmap) &&
                 pfs_write_and_verify(&p, pm->first_sector_of_bitmap, 1,
                                      root_dict) &&
                 pfs_write_and_verify(&p, p.resd_sec_start,
                                      reserved_loader_sectors, dosldr);
  free(dosldr);
  return written;
}
/*
  @brief 分配一个pfs block
  @return 返回block编号
 */
uint32_t pfs_alloc_block(vfs_t *vfs, uint32_t *err) {
  List *l;
  for (int i = 0, k = 1; (l = FindForCount(k, now_pfs_t->bitmap)); i++, k++) {
    uint32_t current_block = l->val;
    List *buffer_entry = FindForCount(k, now_pfs_t->bitmap_buffer);
    if (buffer_entry == NULL) {
      break;
    }
    uint8_t *bitmap = (uint8_t *)(uintptr_t)buffer_entry->val;
    for (int j = 0; j < total_bits_of_one_sec; j++) {
      if (!bit_get(bitmap, j)) {
        used(bitmap, j);
        if (j == total_bits_of_one_sec - 1) {
          uint32_t new_block = j + i * total_bits_of_one_sec;
          uint8_t *bitmap_new = malloc(512); // 默认啥也没使用
          if (bitmap_new == NULL) {
            unused(bitmap, j);
            break;
          }
          memset(bitmap_new, 0, 512);
          if (!AddVal(new_block, now_pfs_t->bitmap)) {
            free(bitmap_new);
            unused(bitmap, j);
            break;
          }
          if (!AddVal((uintptr_t)bitmap_new, now_pfs_t->bitmap_buffer)) {
            DeleteVal(now_pfs_t->bitmap->ctl->all, now_pfs_t->bitmap);
            free(bitmap_new);
            unused(bitmap, j);
            break;
          }
          set_next(bitmap, new_block);
          now_pfs_t->write_block(now_pfs_t,
                                 i ? block2sector(current_block, now_pfs_t)
                                   : current_block,
                                 1, bitmap);
          now_pfs_t->write_block(
              now_pfs_t, block2sector(new_block, now_pfs_t), 1, bitmap_new);
          break; // 这个你不能用[doge]
        } else {
          now_pfs_t->write_block(now_pfs_t,
                                 i ? block2sector(current_block, now_pfs_t)
                                   : current_block,
                                 1, bitmap);
          return j + i * (total_bits_of_one_sec);
        }
      }
    }
  }
  if (err) {
    *err = 0x114514;
  }
  return 0;
}
void pfs_free_block(vfs_t *vfs, uint32_t block) {
  uint32_t index_of_list = block / total_bits_of_one_sec + 1;
  uint32_t index_of_block = block % total_bits_of_one_sec;
  List *l = FindForCount(index_of_list, now_pfs_t->bitmap);
  if (!l) {
    return;
  }
  uint8_t *bm;
  bm = (uint8_t *)(uintptr_t)FindForCount(index_of_list,
                                          now_pfs_t->bitmap_buffer)
           ->val;
  unused(bm, index_of_block);
  now_pfs_t->write_block(
      now_pfs_t, index_of_list - 1 ? block2sector(l->val, now_pfs_t) : l->val,
      1, bm);
}
uint32_t pfs_alloc_block_mark(
    vfs_t *vfs,
    uint32_t *err) { // just mark, and save the bitmap to now_pfs_t->bitmap,
                     // but it wouldn't write the bitmap to the disk
  List *l;
  for (int i = 0, k = 1; (l = FindForCount(k, now_pfs_t->bitmap)); i++, k++) {
    uint32_t current_block = l->val;
    List *buffer_entry = FindForCount(k, now_pfs_t->bitmap_buffer);
    if (buffer_entry == NULL) {
      break;
    }
    uint8_t *bitmap = (uint8_t *)(uintptr_t)buffer_entry->val;
    for (int j = 0; j < total_bits_of_one_sec; j++) {
      if (!bit_get(bitmap, j)) {
        used(bitmap, j);
        if (j == total_bits_of_one_sec - 1) {
          uint32_t new_block = j + i * total_bits_of_one_sec;
          uint8_t *bitmap_new = malloc(512); // 默认啥也没使用
          if (bitmap_new == NULL) {
            unused(bitmap, j);
            break;
          }
          memset(bitmap_new, 0, 512);
          if (!AddVal(new_block, now_pfs_t->bitmap)) {
            free(bitmap_new);
            unused(bitmap, j);
            break;
          }
          if (!AddVal((uintptr_t)bitmap_new, now_pfs_t->bitmap_buffer)) {
            DeleteVal(now_pfs_t->bitmap->ctl->all, now_pfs_t->bitmap);
            free(bitmap_new);
            unused(bitmap, j);
            break;
          }
          set_next(bitmap, new_block);
          now_pfs_t->write_block(now_pfs_t,
                                 i ? block2sector(current_block, now_pfs_t)
                                   : current_block,
                                 1, bitmap);
          now_pfs_t->write_block(
              now_pfs_t, block2sector(new_block, now_pfs_t), 1, bitmap_new);
          break; // 这个你不能用[doge]
        } else {
          uint32_t cb = i ? current_block
                          : current_block - now_pfs_t->first_sec_of_bitmap;
          if (now_pfs_t->current_bitmap_block != -1ll &&
              now_pfs_t->current_bitmap_block != cb) {
            pfs_flush_bitmap(vfs);
            now_pfs_t->write_block(now_pfs_t,
                                   i ? block2sector(current_block, now_pfs_t)
                                     : current_block,
                                   1, bitmap);
            now_pfs_t->current_bitmap_block =
                i ? current_block
                  : current_block - now_pfs_t->first_sec_of_bitmap;
            now_pfs_t->bitmap_buff = bitmap;
          } else if (now_pfs_t->current_bitmap_block == -1ll) {
            now_pfs_t->current_bitmap_block =
                (int64_t)(i ? current_block
                            : current_block - now_pfs_t->first_sec_of_bitmap);
            now_pfs_t->bitmap_buff = bitmap;
          }
          return j + i * (total_bits_of_one_sec);
        }
      }
    }
  }

  if (err) {
    *err = 0x114514;
  }
  return 0;
}
void pfs_flush_bitmap(vfs_t *vfs) {
  if (now_pfs_t->current_bitmap_block == -1ll) {
    return;
  }
  if (now_pfs_t->bitmap_buff == NULL) {
    return;
  }
  now_pfs_t->write_block(
      now_pfs_t, block2sector(now_pfs_t->current_bitmap_block, now_pfs_t), 1,
      now_pfs_t->bitmap_buff);
  now_pfs_t->current_bitmap_block = -1ll;
  now_pfs_t->bitmap_buff = NULL;
}
void pfs_free_block_mark(vfs_t *vfs, uint32_t block) {
  uint32_t index_of_list = block / total_bits_of_one_sec + 1;
  uint32_t index_of_block = block % total_bits_of_one_sec;
  List *l = FindForCount(index_of_list, now_pfs_t->bitmap);
  if (!l) {
    return;
  }
  uint8_t *bm;
  bm = (uint8_t *)(uintptr_t)FindForCount(index_of_list,
                                          now_pfs_t->bitmap_buffer)
           ->val;
  unused(bm, index_of_block);
  uint32_t cb =
      index_of_list - 1 ? l->val : l->val - now_pfs_t->first_sec_of_bitmap;
  if (now_pfs_t->current_bitmap_block != -1ll &&
      now_pfs_t->current_bitmap_block != cb) {
    pfs_flush_bitmap(vfs);
    now_pfs_t->write_block(
        now_pfs_t, index_of_list - 1 ? block2sector(l->val, now_pfs_t) : l->val,
        1, bm);
    now_pfs_t->current_bitmap_block = cb;
    now_pfs_t->bitmap_buff = bm;
  } else if (now_pfs_t->current_bitmap_block == -1ll) {
    now_pfs_t->current_bitmap_block = cb;
    now_pfs_t->bitmap_buff = bm;
  }
}

bool init_bitmap(vfs_t *vfs) {
  uint8_t *sec;
  sec = malloc(512);
  if (sec == NULL) {
    return false;
  }
  now_pfs_t->read_block(now_pfs_t, now_pfs_t->sec_bitmap_start, 1, sec);
  if (!AddVal(now_pfs_t->sec_bitmap_start, now_pfs_t->bitmap) ||
      !AddVal((uintptr_t)sec, now_pfs_t->bitmap_buffer)) {
    free(sec);
    return false;
  }
  while (get_next(sec)) {
    if (!AddVal(get_next(sec), now_pfs_t->bitmap)) {
      return false;
    }
    uint8_t *sec1 = sec;
    sec = malloc(512);
    if (sec == NULL) {
      return false;
    }
    now_pfs_t->read_block(now_pfs_t, block2sector(get_next(sec1), now_pfs_t), 1,
                          sec);
    if (!AddVal((uintptr_t)sec, now_pfs_t->bitmap_buffer)) {
      free(sec);
      return false;
    }
  }
  return true;
}
pfs_inode pfs_get_inode_by_index(vfs_t *vfs, uint32_t index,
                                 uint32_t dict_block) {
  int flags = 1;
  uint32_t times = 0;
  pfs_dict_block pdb;
  while (dict_block || flags) {
    memset(&pdb, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    if (times == index / 15) {
      break;
    }
    dict_block = pdb.next;
    flags = 0;
    ++times;
  }
  if (times != index / 15) {
    //  pdb.resd[0] = 0x114514;
    pfs_inode i;
    i.type = 0x04;
    return i;
  }
  return pdb.inodes[index % 15];
}
void pfs_set_inode_by_index(vfs_t *vfs, uint32_t index, uint32_t dict_block,
                            pfs_inode *inode) {
  int flags = 1;
  uint32_t times = 0;
  uint32_t old = dict_block;
  pfs_dict_block pdb;
  while (dict_block || flags) {
    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    old = dict_block;
    dict_block = pdb.next;
    if (times == index / 15) {
      break;
    }

    flags = 0;
    ++times;
    memset(&pdb, 0, 512);
  }
  if (times != index / 15) {
    return;
  }
  pdb.inodes[index % 15] = *inode;
  now_pfs_t->write_block(now_pfs_t, block2sector(old, now_pfs_t), 1, &pdb);
}
void pfs_make_inode(vfs_t *vfs, uint32_t index, char *name, uint32_t type,
                    uint32_t dict_block) {
  pfs_inode i;
  i.type = type;
  i.dat = 0;
  i.next = 0;
  i.time = (unsigned)time();
  i.size = 0;
  pfs_set_inode_by_index(vfs, index, dict_block, &i);

  if (strlen(name) <= 13) {
    memset(i.name, 0, 14);
    memcpy(i.name, name, strlen(name));
    pfs_set_inode_by_index(vfs, index, dict_block, &i);
  } else {
    uint32_t rest_of_len_of_name, next;
    memcpy(i.name, name, 13);
    name += 13;
    i.name[13] = 0xff;
    next = pfs_create_inode(vfs, dict_block);
    i.next = next;
    pfs_set_inode_by_index(vfs, index, dict_block, &i);
    rest_of_len_of_name = strlen(name);
    while (rest_of_len_of_name > 0) {
      pfs_inode_of_long_file_name l;
      l.type = 3;
      if (rest_of_len_of_name > 26) {
        memcpy(l.name, name, 26);
        l.name[26] = 0xff;
        name += 26;
        rest_of_len_of_name -= 26;
        pfs_set_inode_by_index(vfs, next, dict_block, (pfs_inode *)&l);
        uint32_t n = next;
        next = pfs_create_inode(vfs, dict_block);
        l.next = next;
        pfs_set_inode_by_index(vfs, n, dict_block, (pfs_inode *)&l);
      } else {
        memcpy(l.name, name, rest_of_len_of_name);
        l.name[rest_of_len_of_name] = 0x00;
        l.name[26] = 0x00;
        name += rest_of_len_of_name;
        rest_of_len_of_name -= rest_of_len_of_name;

        pfs_set_inode_by_index(vfs, next, dict_block, (pfs_inode *)&l);
      }
    }
  }
}
void pfs_inode_block_make(vfs_t *vfs, uint32_t block, uint32_t next) {
  pfs_dict_block d;
  for (int i = 0; i < 15; i++) {
    d.inodes[i].type = 0;
    d.inodes[i].next = 0;
    d.inodes[i].dat = 0;
  }
  d.next = next;
  now_pfs_t->write_block(now_pfs_t, block2sector(block, now_pfs_t), 1, &d);
}
uint32_t pfs_create_inode(vfs_t *vfs, uint32_t dict_block) {
  int flags = 1;
  int times = 0;
  uint32_t old;
  pfs_dict_block pdb;
  while (dict_block || flags) {
    memset(&pdb, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    for (int i = 0; i < 15; i++) {
      if (pdb.inodes[i].type == 0) { // 找到没有使用的inode
        return i + times * 15;
      }
    }
    old = dict_block;
    dict_block = pdb.next;
    flags = 0;
    ++times;
  }
  pdb.next = pfs_alloc_block(vfs, NULL);
  now_pfs_t->write_block(now_pfs_t, block2sector(old, now_pfs_t), 1, &pdb);
  pfs_inode_block_make(vfs, pdb.next, 0);
  now_pfs_t->read_block(now_pfs_t, block2sector(pdb.next, now_pfs_t), 1, &pdb);
  return times * 15;
}
uint32_t pfs_get_filesize(vfs_t *vfs, char *filename, uint32_t dict_block,
                          uint32_t *err) {
  if (err != NULL) {
    *err = 0;
  }
  uint32_t idx;
  uint32_t err1 = 0;
  pfs_get_file_index_by_path(vfs, filename, dict_block, &err1, &idx,
                             &dict_block);
  if (err1 == 0x114514) {
    if (err) {
      *err = 0x114514;
    }
    return 0;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  if (i.type != 1) {
    if (err) {
      *err = 0x114514;
    }
    return 0;
  }
  return i.size;
}
void pfs_ls(vfs_t *vfs, uint32_t dict_block) {
  int flags = 1;
  pfs_dict_block pdb;
  while (dict_block || flags) {
    memset(&pdb, 0, 512);

    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    for (int i = 0; i < 15; i++) {
      if (pdb.inodes[i].type == 3) {
        //        return;
        continue;
      }
      if (pdb.inodes[i].type != 0) {
        if (pdb.inodes[i].name[13] == 0) {
          printk("%s ", pdb.inodes[i].name);
        } else {
          for (int j = 0; j < 13; j++) {
            printk("%c", pdb.inodes[i].name[j]);
          }
          uint32_t idx;
          idx = pdb.inodes[i].next;
          while (idx) {
            pfs_inode pi;
            pi = pfs_get_inode_by_index(vfs, idx, dict_block);
            pfs_inode_of_long_file_name *f = (pfs_inode_of_long_file_name *)&pi;
            if (f->name[26] == 0x0) {
              printk("%s ", f->name);
              break;
            } else {
              for (int k = 0; k < 26; k++) {
                printk("%c", f->name[k]);
              }
            }
            idx = f->next;
          }
        }
      }
    }
    dict_block = pdb.next;
    flags = 0;
  }
  printk("\n");
}
uint32_t pfs_get_idx_of_inode_by_name(vfs_t *vfs, char *name,
                                      uint32_t dict_block, uint32_t *err) {
  int flags = 1;
  int times = 0;
  pfs_dict_block pdb;
  while (dict_block || flags) {
    memset(&pdb, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    for (int i = 0; i < 15; i++) {
      if (pdb.inodes[i].type == 3) {
        //        return;
        continue;
      }
      if (pdb.inodes[i].type != 0) {
        mstr *s = mstr_init();
        if (pdb.inodes[i].name[13] == 0) {
          mstr_add_str(s, pdb.inodes[i].name);
        } else {
          for (int j = 0; j < 13; j++) {
            mstr_add_char(s, pdb.inodes[i].name[j]);
          }
          uint32_t idx;
          idx = pdb.inodes[i].next;
          while (idx) {
            pfs_inode pi;
            pi = pfs_get_inode_by_index(vfs, idx, dict_block);
            pfs_inode_of_long_file_name *f = (pfs_inode_of_long_file_name *)&pi;
            if (f->name[26] == 0x0) {
              mstr_add_str(s, f->name);
              break;
            } else {
              for (int k = 0; k < 26; k++) {
                mstr_add_char(s, f->name[k]);
              }
            }
            idx = f->next;
          }
        }
        // printk("%s ", mstr_get(s));
        if (strcmp(mstr_get(s), name) == 0) {
          mstr_free(s);
          return i + times * 15;
        }
        mstr_free(s);
      }
    }
    dict_block = pdb.next;
    flags = 0;
    ++times;
  }
  if (err) {
    *err = 0x114514;
  }
  return 0;
}
void pfs_create_file(vfs_t *vfs, char *filename, uint32_t dict_block) {
  pfs_make_inode(vfs, pfs_create_inode(vfs, dict_block), filename, 1,
                 dict_block);
}
void pfs_delete_data_block(vfs_t *vfs, uint32_t start_block) {
  pfs_data_block p;
  now_pfs_t->read_block(now_pfs_t, block2sector(start_block, now_pfs_t), 1, &p);
  uint32_t next = p.next;
  int i = 0;
  while (next) {
    i = 1;
    memset(&p, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(next, now_pfs_t), 1, &p);
    pfs_free_block_mark(vfs, next);
    next = p.next;
  }
  if (i) {
    pfs_flush_bitmap(vfs);
  }
}
void pfs_delete_dict_block(vfs_t *vfs, uint32_t start_block) {
  pfs_dict_block p;
  now_pfs_t->read_block(now_pfs_t, block2sector(start_block, now_pfs_t), 1, &p);
  uint32_t next = p.next;
  while (next) {
    // s printk("next %d\n",next);
    memset(&p, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(next, now_pfs_t), 1, &p);
    pfs_free_block(vfs, next);
    next = p.next;
  }
}
void pfs_init_data_block(vfs_t *vfs, uint32_t dict_block) {
  pfs_data_block d;
  d.next = 0;
  now_pfs_t->write_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1, &d);
}
void pfs_write_file(vfs_t *vfs, char *filename, uint32_t size, void *buff,
                    uint32_t dict_block) {
  uint32_t err;
  uint32_t idx;
  pfs_get_file_index_by_path(vfs, filename, dict_block, &err, &idx,
                             &dict_block);
  if (err == 0x114514) {
    return;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  if (i.type != 1) {
    return;
  }
  i.time = (unsigned int)time();
  i.size = size;
  uint32_t dat = i.dat;
  int j = 0;
  if (!dat) {
    j = 1;
    i.dat = pfs_alloc_block_mark(vfs, NULL);
    pfs_init_data_block(vfs, i.dat);
  }
  dat = i.dat;
  while (size > 0) {
    pfs_data_block dat_block;
    now_pfs_t->read_block(now_pfs_t, block2sector(dat, now_pfs_t), 1,
                          &dat_block);
    if (size <= 508) {
      if (dat_block.next) {
        pfs_delete_data_block(vfs, dat_block.next);
      }
      dat_block.next = 0;
      memcpy(dat_block.data, buff, size);
      buff += size;
      size -= size;
    } else {
      if (!dat_block.next) {
        j = 1;
        dat_block.next = pfs_alloc_block_mark(vfs, NULL);
        pfs_init_data_block(vfs, dat_block.next);
      }
      memcpy(dat_block.data, buff, 508);
      buff += 508;
      size -= 508;
    }
    now_pfs_t->write_block(now_pfs_t, block2sector(dat, now_pfs_t), 1,
                           &dat_block);
    dat = dat_block.next;
  }
  if (j) {
    pfs_flush_bitmap(vfs);
  }
  pfs_set_inode_by_index(vfs, idx, dict_block, &i);
}
void pfs_read_file(vfs_t *vfs, char *filename, void *buff,
                   uint32_t dict_block) {
  uint32_t err;
  uint32_t idx;
  pfs_get_file_index_by_path(vfs, filename, dict_block, &err, &idx,
                             &dict_block);
  if (err == 0x114514) {
    return;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  if (i.type != 1) {
    return;
  }
  if (!i.dat) {
    return;
  }
  uint32_t next = i.dat;
  // 乐 某个傻逼想打read的过去式的，结果这个傻子加了个ed
  uint32_t readed = 0;
  while (next) {
    pfs_data_block p;
    now_pfs_t->read_block(now_pfs_t, block2sector(next, now_pfs_t), 1, &p);
    if (i.size - readed <= 508) {
      memcpy(buff, p.data, i.size - readed);
      buff += i.size - readed;
      readed += i.size - readed;
    } else {
      memcpy(buff, p.data, 508);
      buff += 508;
      readed += 508;
    }
    if (readed == i.size) {
      return;
    }
    next = p.next;
  }
}
uint32_t pfs_get_dict_block_by_name(vfs_t *vfs, char *name, uint32_t dict_block,
                                    uint32_t *err) {
  uint32_t perr;
  uint32_t idx = pfs_get_idx_of_inode_by_name(vfs, name, dict_block, &perr);
  if (perr == 0x114514) {
    if (err) {
      *err = 0x114514;
    }
    return 0;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  if (i.type != 2) {
    if (err) {
      *err = 0x114514; // 啊啊啊啊啊啊啊啊这个文件夹不正常
    }
    return 0;
  }
  return i.dat;
}
void pfs_create_dict(vfs_t *vfs, char *name, uint32_t dict_block) {
  uint32_t idx = pfs_create_inode(vfs, dict_block);
  pfs_make_inode(vfs, idx, name, 2, dict_block);
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  i.size = 0;
  i.time = (unsigned int)time();
  i.dat = pfs_alloc_block(vfs, NULL);
  pfs_inode_block_make(vfs, i.dat, 0);
  pfs_set_inode_by_index(vfs, idx, dict_block, &i);
}
uint32_t pfs_get_dict_number(vfs_t *vfs, uint32_t dict_block) {
  int flags = 1;
  pfs_dict_block pdb;
  uint32_t result = 0;
  while (dict_block || flags) {
    memset(&pdb, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    for (int i = 0; i < 15; i++) {
      if (pdb.inodes[i].type == 3) {
        continue;
      }
      if (pdb.inodes[i].type != 0) {
        result++;
      }
    }
    dict_block = pdb.next;
    flags = 0;
  }
  return result;
}
void pfs_delete_file(vfs_t *vfs, char *filename, uint32_t dict_block) {
  uint32_t err, idx;
  idx = pfs_get_idx_of_inode_by_name(vfs, filename, dict_block, &err);
  if (err == 0x114514) {
    //  printk("delete err.\n");
    return;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  if (i.type != 1) {
    printk("it isn't a file!\n");
    return;
  }
  pfs_delete_name_link(vfs, i.next, dict_block);
  if (i.dat) {
    pfs_delete_data_block(vfs, i.dat);
    pfs_free_block(vfs, i.dat);
  }
  i.dat = 0;
  i.type = 0;
  pfs_set_inode_by_index(vfs, idx, dict_block, &i);
}
void pfs_delete_dict(vfs_t *vfs, char *name, uint32_t dict_block) {
  uint32_t err, idx;
  idx = pfs_get_idx_of_inode_by_name(vfs, name, dict_block, &err);
  if (err == 0x114514) {
    printk("delete err.\n");
    return;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  if (i.type != 2) {
    printk("it isn't a dict!\n");
    return;
  }
  if (i.dat) {
    if (pfs_get_dict_number(vfs, i.dat) > 0) {
      printk("The dict must be empty!\n");
      return;
    }
    pfs_delete_dict_block(vfs, i.dat);
    pfs_free_block(vfs, i.dat);
  }
  pfs_delete_name_link(vfs, i.next, dict_block);
  i.type = 0;
  i.dat = 0;
  pfs_set_inode_by_index(vfs, idx, dict_block, &i);
}
// /pfs/hello.txt
uint32_t _pfs_get_dict_block_by_path(vfs_t *vfs, char *path, char **end,
                                     uint32_t start_block, uint32_t *err) {
  if (err != NULL) {
    *err = 0;
  }
  if (end != NULL) {
    *end = NULL;
  }
  if (vfs == NULL || path == NULL) {
    if (err != NULL) {
      *err = 0x114514;
    }
    return 0;
  }
  if (*path == '/') { /* root */
    start_block = 0;
    path++;
  }
  if (*path == '\0') {
    if (end != NULL) {
      *end = path;
    }
    return start_block;
  }
  while (1) {
    char *separator = strchr(path, '/');
    if (separator == NULL) {
      if (end != NULL) {
        *end = path;
        return start_block;
      }
      uint32_t lookup_error = 0;
      uint32_t block =
          pfs_get_dict_block_by_name(vfs, path, start_block, &lookup_error);
      if (lookup_error == 0x114514) {
        if (err != NULL) {
          *err = 0x114514;
        }
        return 0;
      }
      return block;
    }
    if (separator == path) {
      if (err != NULL) {
        *err = 0x114514;
      }
      return 0;
    }
    *separator = '\0';
    uint32_t lookup_error = 0;
    uint32_t block =
        pfs_get_dict_block_by_name(vfs, path, start_block, &lookup_error);
    *separator = '/';
    if (lookup_error == 0x114514) {
      if (err != NULL) {
        *err = 0x114514;
      }
      return 0;
    }
    start_block = block;
    path = separator + 1;
    if (*path == '\0') {
      return start_block;
    }
  }
}
uint32_t pfs_get_dict_block_by_path(vfs_t *vfs, char *path, char **end,
                                    uint32_t start_block, uint32_t *err) {
  if (err != NULL) {
    *err = 0;
  }
  if (end != NULL) {
    *end = NULL;
  }
  if (vfs == NULL || path == NULL) {
    if (err != NULL) {
      *err = 0x114514;
    }
    return 0;
  }
  size_t length = strlen(path);
  if (length >= INT_MAX) {
    if (err != NULL) {
      *err = 0x114514;
    }
    return 0;
  }
  char *p1 = malloc(length + 1);
  if (p1 == NULL) {
    if (err != NULL) {
      *err = 0x114514;
    }
    return 0;
  }
  memcpy(p1, path, length + 1);
  char *e1 = NULL;
  uint32_t err1 = 0;
  uint32_t r = _pfs_get_dict_block_by_path(
      vfs, p1, end == NULL ? NULL : &e1, start_block, &err1);
  if (err1 == 0 && end != NULL && e1 != NULL) {
    *end = path + (e1 - p1);
  }
  if (err != NULL) {
    *err = err1;
  }
  free(p1);
  return r;
}
void pfs_get_file_index_by_path(vfs_t *vfs, char *path, uint32_t start_block,
                                uint32_t *err, uint32_t *idx,
                                uint32_t *dict_block) {
  if (err != NULL) {
    *err = 0;
  }
  if (vfs == NULL || path == NULL || idx == NULL || dict_block == NULL) {
    if (err != NULL) {
      *err = 0x114514;
    }
    return;
  }
  char *e = NULL;
  uint32_t err1 = 0;
  uint32_t b = pfs_get_dict_block_by_path(vfs, path, &e, start_block, &err1);
  if (err1 == 0x114514 || e == NULL || *e == '\0') {
    if (err != NULL) {
      *err = 0x114514;
    }
    return;
  }
  uint32_t i = pfs_get_idx_of_inode_by_name(vfs, e, b, &err1);
  if (err1 == 0x114514) {
    if (err != NULL) {
      *err = 0x114514;
    }
    return;
  }
  *idx = i;
  *dict_block = b;
}
void pfs_delete_name_link(vfs_t *vfs, uint32_t next, uint32_t dict_block) {
  while (next) {
    pfs_inode i;
    i = pfs_get_inode_by_index(vfs, next, dict_block);
    i.type = 0;
    uint32_t n = i.next;
    i.next = 0;
    pfs_set_inode_by_index(vfs, next, dict_block, &i);
    next = n;
  }
}
bool pfs_rename(vfs_t *vfs, char *old_name, char *new_name,
                uint32_t dict_block) {
  if (strcmp(old_name, new_name) == 0) {
    return true;
  }
  uint32_t destination_error;
  pfs_get_idx_of_inode_by_name(vfs, new_name, dict_block, &destination_error);
  if (destination_error != 0x114514) {
    return false;
  }
  uint32_t err, idx;
  idx = pfs_get_idx_of_inode_by_name(vfs, old_name, dict_block, &err);
  if (err == 0x114514) {
    return false;
  }
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, dict_block);
  uint32_t d, t, s;
  d = i.dat;
  t = i.type;
  s = i.size;
  pfs_delete_name_link(vfs, i.next, dict_block);
  pfs_make_inode(vfs, idx, new_name, t, dict_block);
  i = pfs_get_inode_by_index(vfs, idx, dict_block);
  i.dat = d;
  i.size = s;
  pfs_set_inode_by_index(vfs, idx, dict_block, &i);
  return true;
}
bool init_pfs(vfs_t *vfs, pfs_t p) {
  vfs->cache = malloc(sizeof(pfs_t));
  if (vfs->cache == NULL) {
    return false;
  }
  memset(vfs->cache, 0, sizeof(pfs_t));
  *now_pfs_t = p;
  uint8_t mbr[512];
  now_pfs_t->read_block(now_pfs_t, 0, 1, mbr);
  pfs_mbr *mb = (pfs_mbr *)mbr;
  if (memcmp(mb->sign, "PFS\xff", 4) != 0) {
    goto fail;
  }
  now_pfs_t->first_sec_of_bitmap = mb->first_sector_of_bitmap;
  now_pfs_t->resd_sec_end = mb->resd_sector_end;
  now_pfs_t->resd_sec_start = mb->resd_sector_start;
  now_pfs_t->root_dict_block = 0;
  now_pfs_t->sec_bitmap_start = mb->sec_bitmap_start;
  now_pfs_t->file_list = NewList();
  now_pfs_t->bitmap = NewList();
  now_pfs_t->prev_dict_block = NewList();
  now_pfs_t->bitmap_buffer = NewList();
  if (now_pfs_t->file_list == NULL || now_pfs_t->bitmap == NULL ||
      now_pfs_t->prev_dict_block == NULL ||
      now_pfs_t->bitmap_buffer == NULL) {
    goto fail;
  }
  now_pfs_t->current_dict_block = 0;
  now_pfs_t->current_bitmap_block = -1ll;
  now_pfs_t->bitmap_buff = NULL;
  if (!init_bitmap(vfs)) {
    goto fail;
  }
  return true;

fail:
  pfs_DeleteFs(vfs);
  return false;
}

bool pfs_InitFS(struct vfs_t *vfs, uint8_t disk_number) {
  pfs_t p;
  p.disk_number = disk_number;
  p.read_block = pfs_read_block;
  p.write_block = pfs_write_block;
  return init_pfs(vfs, p);
}
bool pfs_CopyCache(struct vfs_t *dest, struct vfs_t *src) {
  dest->cache = malloc(sizeof(pfs_t));
  if (dest->cache == NULL) {
    return false;
  }
  memcpy(dest->cache, src->cache, sizeof(pfs_t));
  ((pfs_t *)dest->cache)->prev_dict_block = NewList();
  if (((pfs_t *)dest->cache)->prev_dict_block == NULL) {
    free(dest->cache);
    dest->cache = NULL;
    return false;
  }
  return true;
}
static void pfs_ReleaseCache(struct vfs_t *vfs) {
  if (vfs->cache == NULL) {
    return;
  }
  if (now_pfs_t->prev_dict_block != NULL) {
    DeleteList(now_pfs_t->prev_dict_block);
  }
  free(vfs->cache);
  vfs->cache = NULL;
}
bool pfs_cd(struct vfs_t *vfs, char *dictName) {
  if (vfs == NULL || dictName == NULL || vfs->path == NULL ||
      now_pfs_t->prev_dict_block == NULL) {
    return false;
  }
  if (strcmp("/", dictName) == 0) {
    while (vfs->path->ctl->all != 0) {
      struct List *entry = FindForCount(vfs->path->ctl->all, vfs->path);
      free((void *)(uintptr_t)entry->val);
      DeleteVal(vfs->path->ctl->all, vfs->path);
    }
    while (now_pfs_t->prev_dict_block->ctl->all != 0) {
      DeleteVal(now_pfs_t->prev_dict_block->ctl->all,
                now_pfs_t->prev_dict_block);
    }
    now_pfs_t->current_dict_block = 0;
    return true;
  }
  if (strcmp("..", dictName) == 0) {
    if (now_pfs_t->prev_dict_block->ctl->all == 0 ||
        vfs->path->ctl->all == 0) {
      return false;
    }
    struct List *block_entry = FindForCount(
        now_pfs_t->prev_dict_block->ctl->all, now_pfs_t->prev_dict_block);
    struct List *path_entry = FindForCount(vfs->path->ctl->all, vfs->path);
    if (block_entry == NULL || path_entry == NULL) {
      return false;
    }
    uint32_t previous_block = block_entry->val;
    free((void *)(uintptr_t)path_entry->val);
    DeleteVal(vfs->path->ctl->all, vfs->path);
    DeleteVal(now_pfs_t->prev_dict_block->ctl->all,
              now_pfs_t->prev_dict_block);
    now_pfs_t->current_dict_block = previous_block;
    return true;
  } else if (strcmp(".", dictName) == 0) {
    return true;
  }
  uint32_t err;

  uint32_t new_dict_block = pfs_get_dict_block_by_name(
      vfs, dictName, now_pfs_t->current_dict_block, &err);
  if (err == 0x114514) {
    return false;
  }
  size_t length = strlen(dictName);
  if (length >= (size_t)INT_MAX) {
    return false;
  }
  char *s = malloc((int)length + 1);
  if (s == NULL) {
    return false;
  }
  memcpy(s, dictName, length + 1);
  if (!AddVal(now_pfs_t->current_dict_block, now_pfs_t->prev_dict_block)) {
    free(s);
    return false;
  }
  if (!AddVal((uintptr_t)s, vfs->path)) {
    DeleteVal(now_pfs_t->prev_dict_block->ctl->all,
              now_pfs_t->prev_dict_block);
    free(s);
    return false;
  }
  now_pfs_t->current_dict_block = new_dict_block;
  return true;
}
bool pfs_ReadFile(struct vfs_t *vfs, char *path, char *buffer) {
  pfs_read_file(vfs, path, buffer, now_pfs_t->current_dict_block);
  return true;
}
bool pfs_WriteFile(struct vfs_t *vfs, char *path, char *buffer, int size) {
  pfs_write_file(vfs, path, size, buffer, now_pfs_t->current_dict_block);
  return true;
}
static void pfs_free_file_list(List *files) {
  if (files == NULL) {
    return;
  }
  for (int i = 1; FindForCount(i, files) != NULL; i++) {
    free((void *)(uintptr_t)FindForCount(i, files)->val);
  }
  DeleteList(files);
}
List *pfs_ListFile(struct vfs_t *vfs, char *dictpath) {
  int flags = 1;
  pfs_dict_block pdb;
  List *result = NewList();
  if (result == NULL) {
    return NULL;
  }
  uint32_t dict_block;
  if (strlen(dictpath) == 0) {
    dict_block = now_pfs_t->current_dict_block;
  } else {
    vfs_file *info = pfs_FileInfo(vfs, dictpath);
    if (info == NULL || info->type != DIR) {
      free(info);
      DeleteList(result);
      return NULL;
    }
    free(info);
    uint32_t err = 0;
    dict_block = pfs_get_dict_block_by_path(
        vfs, dictpath, NULL, now_pfs_t->current_dict_block, &err);
    if (err == 0x114514) {
      DeleteList(result);
      return NULL;
    }
  }
  while (dict_block || flags) {
    memset(&pdb, 0, 512);
    now_pfs_t->read_block(now_pfs_t, block2sector(dict_block, now_pfs_t), 1,
                          &pdb);
    for (int i = 0; i < 15; i++) {
      if (pdb.inodes[i].type == 3) {
        //        return;
        continue;
      }
      if (pdb.inodes[i].type != 0) {
        mstr *s = mstr_init();
        if (s == NULL) {
          pfs_free_file_list(result);
          return NULL;
        }
        if (pdb.inodes[i].name[13] == 0) {
          mstr_add_str(s, pdb.inodes[i].name);
        } else {
          for (int j = 0; j < 13; j++) {
            mstr_add_char(s, pdb.inodes[i].name[j]);
          }
          uint32_t idx;
          idx = pdb.inodes[i].next;
          while (idx) {
            pfs_inode pi;
            pi = pfs_get_inode_by_index(vfs, idx, dict_block);
            pfs_inode_of_long_file_name *f = (pfs_inode_of_long_file_name *)&pi;
            if (f->name[26] == 0x0) {
              mstr_add_str(s, f->name);
              break;
            } else {
              for (int k = 0; k < 26; k++) {
                mstr_add_char(s, f->name[k]);
              }
            }
            idx = f->next;
          }
        }
        vfs_file *f = malloc(sizeof(vfs_file));
        if (f == NULL || strlen(mstr_get(s)) >= sizeof(f->name)) {
          free(f);
          mstr_free(s);
          pfs_free_file_list(result);
          return NULL;
        }
        uint32_t year, mon, day, hour, min, sec;
        calendar_from_ntp_timestamp(pdb.inodes[i].time, &year, &mon, &day,
                                    &hour, &min, &sec);
        strcpy(f->name, mstr_get(s));
        f->day = day;
        f->hour = hour;
        f->minute = min;
        f->month = mon;
        f->size = pdb.inodes[i].size;
        f->type = pdb.inodes[i].type == 2 ? DIR : FLE;
        f->year = year;
        if (!AddVal((uintptr_t)f, result)) {
          free(f);
          mstr_free(s);
          pfs_free_file_list(result);
          return NULL;
        }
        mstr_free(s);
      }
    }
    dict_block = pdb.next;
    flags = 0;
  }
  return result;
}
bool pfs_RenameFile(struct vfs_t *vfs, char *filename, char *filename_of_new) {
  char *source_name;
  char *destination_name;
  uint32_t source_error = 0;
  uint32_t destination_error = 0;
  uint32_t source_block = pfs_get_dict_block_by_path(
      vfs, filename, &source_name, now_pfs_t->current_dict_block,
      &source_error);
  uint32_t destination_block = pfs_get_dict_block_by_path(
      vfs, filename_of_new, &destination_name, now_pfs_t->current_dict_block,
      &destination_error);
  if (source_error == 0x114514 || destination_error == 0x114514 ||
      source_block != destination_block || source_name == NULL ||
      destination_name == NULL || *source_name == '\0' ||
      *destination_name == '\0') {
    return false;
  }
  return pfs_rename(vfs, source_name, destination_name, source_block);
}
bool pfs_CreateFile(struct vfs_t *vfs, char *filename) {
  char *e = NULL;
  uint32_t err = 0;
  uint32_t block = pfs_get_dict_block_by_path(
      vfs, filename, &e, now_pfs_t->current_dict_block, &err);
  if (err == 0x114514 || e == NULL || *e == '\0') {
    return false;
  }
  pfs_create_file(vfs, e, block);
  return true;
}
void pfs_DeleteFs(struct vfs_t *vfs) {
  if (vfs->cache == NULL) {
    return;
  }
  List *l;
  if (now_pfs_t->bitmap_buffer != NULL) {
    for (int i = 1; (l = FindForCount(i, now_pfs_t->bitmap_buffer)) != NULL;
         i++) {
      free((void *)(uintptr_t)l->val);
    }
    DeleteList(now_pfs_t->bitmap_buffer);
  }
  if (now_pfs_t->bitmap != NULL) {
    DeleteList(now_pfs_t->bitmap);
  }
  if (now_pfs_t->file_list != NULL) {
    DeleteList(now_pfs_t->file_list);
  }
  if (now_pfs_t->prev_dict_block != NULL) {
    DeleteList(now_pfs_t->prev_dict_block);
  }
  free(vfs->cache);
  vfs->cache = NULL;
}
bool pfs_Check(uint8_t disk_number) {
  if (!DiskReady(disk_number)) {
    return false;
  }
  uint8_t mbr[512];
  Disk_Read(0, 1, mbr, disk_number);
  pfs_mbr *mb = (pfs_mbr *)mbr;
  if (memcmp(mb->sign, "PFS\xff", 4) != 0) {
    return false;
  }
  return true;
}
bool pfs_DelFile(struct vfs_t *vfs, char *path) {
  uint32_t b, err = 0;
  char *e = NULL;
  b = pfs_get_dict_block_by_path(vfs, path, &e, now_pfs_t->current_dict_block,
                                 &err);
  if (err == 0x114514 || e == NULL || *e == '\0') {
    return false;
  }
  pfs_delete_file(vfs, e, b);
  return true;
}
bool pfs_DelDict(struct vfs_t *vfs, char *path) {
  uint32_t error = 0;
  char *name = NULL;
  uint32_t block = pfs_get_dict_block_by_path(
      vfs, path, &name, now_pfs_t->current_dict_block, &error);
  if (error == 0x114514 || name == NULL || *name == '\0') {
    return false;
  }
  pfs_delete_dict(vfs, name, block);
  return true;
}
int pfs_FileSize(struct vfs_t *vfs, char *filename) {
  uint32_t b, err = 0;
  char *e = NULL;
  b = pfs_get_dict_block_by_path(vfs, filename, &e,
                                 now_pfs_t->current_dict_block, &err);
  if (err == 0x114514 || e == NULL || *e == '\0') {
    return -1;
  }
  uint32_t r = pfs_get_filesize(vfs, e, b, &err);
  if (err == 0x114514) {
    return -1;
  }
  return r;
}
bool pfs_Format(uint8_t disk_number) {
  if (!DiskReady(disk_number)) {
    return false;
  }
  pfs_t p = {0};
  p.read_block = pfs_read_block;
  p.write_block = pfs_write_block;
  p.disk_number = disk_number;
  char vol[16] = "POWERINTDOS386";
  return pfs_format(p, vol);
}
bool pfs_CreateDict(struct vfs_t *vfs, char *filename) {
  char *e = NULL;
  uint32_t err = 0;
  uint32_t block = pfs_get_dict_block_by_path(
      vfs, filename, &e, now_pfs_t->current_dict_block, &err);
  if (err == 0x114514 || e == NULL || *e == '\0') {
    return false;
  }
  pfs_create_dict(vfs, e, block);
  return true;
}
bool pfs_Attrib(struct vfs_t *vfs, char *filename, ftype type) {
  (void)vfs;
  (void)filename;
  (void)type;
  printk("Sorry, pfs does not support attrib at this time.\n");
  return false;
}
vfs_file *pfs_FileInfo(struct vfs_t *vfs, char *filename) {
  if (vfs == NULL || filename == NULL ||
      strlen(filename) >= sizeof(((vfs_file *)0)->name)) {
    return NULL;
  }
  uint32_t idx, b, err = 0;
  pfs_get_file_index_by_path(vfs, filename, now_pfs_t->current_dict_block, &err,
                             &idx, &b);
  if (err == 0x114514) {
    return NULL;
  }
  vfs_file *result = (vfs_file *)malloc(sizeof(vfs_file));
  if (result == NULL) {
    return NULL;
  }
  memset(result, 0, sizeof(vfs_file));
  pfs_inode i = pfs_get_inode_by_index(vfs, idx, b);
  uint32_t year, mon, day, hour, min, sec;
  calendar_from_ntp_timestamp(i.time, &year, &mon, &day, &hour, &min, &sec);
  memcpy(result->name, filename, strlen(filename) + 1);
  result->day = day;
  result->hour = hour;
  result->minute = min;
  result->month = mon;
  result->size = i.size;
  result->type = i.type == 2 ? DIR : FLE;
  result->year = year;
  return result;
}
void reg_pfs() {
  vfs_t fs = {0};
  fs.flag = 1;
  fs.cache = NULL;
  strcpy(fs.FSName, "PFS");
  fs.CopyCache = pfs_CopyCache;
  fs.ReleaseCache = pfs_ReleaseCache;
  fs.Format = pfs_Format;
  fs.CreateFile = pfs_CreateFile;
  fs.CreateDict = pfs_CreateDict;
  fs.DelDict = pfs_DelDict;
  fs.DelFile = pfs_DelFile;
  fs.ReadFile = pfs_ReadFile;
  fs.WriteFile = pfs_WriteFile;
  fs.DeleteFs = pfs_DeleteFs;
  fs.cd = pfs_cd;
  fs.FileSize = pfs_FileSize;
  fs.Check = pfs_Check;
  fs.ListFile = pfs_ListFile;
  fs.InitFs = pfs_InitFS;
  fs.RenameFile = pfs_RenameFile;
  fs.Attrib = pfs_Attrib;
  fs.FileInfo = pfs_FileInfo;
  vfs_register_fs(fs);
}
