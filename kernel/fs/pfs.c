// libpfs
#include <dos.h>
#include <calendar.h>
#include <mstr.h>
#include <pfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static void pfs_delete_fs(vfs_t *vfs);

void pfs_flush_bitmap(vfs_t *vfs);
void pfs_delete_name_link(vfs_t *vfs, uint32_t next, uint32_t dict_block);

void pfs_read_block(pfs_t *pfs, uint32_t lba, uint32_t numbers, void *buff) {
  if (!buff) {
    return;
  }
  disk_read(lba, numbers, buff, pfs->disk_number);
}
void pfs_write_block(pfs_t *pfs, uint32_t lba, uint32_t numbers, void *buff) {
  if (!buff) {
    return;
  }
  disk_write(lba, numbers, buff, pfs->disk_number);
}
#define now_pfs_t ((pfs_t *)vfs_mount_data(vfs))
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
bool pfs_format_impl(pfs_t p, const char volid[16]) {
  if (volid == NULL || p.read_block == NULL || p.write_block == NULL) {
    return false;
  }
  uint8_t mbr[512] = {0};
  FILE *fp = fopen("/boot_pfs.bin", "rb");
  if (fp == NULL || fread(mbr, 1, sizeof(mbr), fp) != sizeof(mbr)) {
    if (fp != NULL) {
      fclose(fp);
    }
    return false;
  }
  fclose(fp);
  fp = fopen("/dosldr.bin", "rb");
  if (fp == NULL || fseek(fp, 0, SEEK_END) != 0) {
    if (fp != NULL) {
      fclose(fp);
    }
    return false;
  }
  long loader_length = ftell(fp);
  if (loader_length <= 0 || loader_length > INT_MAX - 511 ||
      fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return false;
  }
  uint32_t loader_size = loader_length;
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
    fclose(fp);
    return false;
  }
  memset(dosldr, 0, loader_buffer_size);
  if (fread(dosldr, 1, loader_size, fp) != loader_size) {
    fclose(fp);
    free(dosldr);
    return false;
  }
  fclose(fp);
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
  for (int i = 0, k = 1; (l = list_get(k, now_pfs_t->bitmap)); i++, k++) {
    uint32_t current_block = l->val;
    List *buffer_entry = list_get(k, now_pfs_t->bitmap_buffer);
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
  List *l = list_get(index_of_list, now_pfs_t->bitmap);
  if (!l) {
    return;
  }
  uint8_t *bm;
  bm = (uint8_t *)(uintptr_t)list_get(index_of_list,
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
  for (int i = 0, k = 1; (l = list_get(k, now_pfs_t->bitmap)); i++, k++) {
    uint32_t current_block = l->val;
    List *buffer_entry = list_get(k, now_pfs_t->bitmap_buffer);
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
  List *l = list_get(index_of_list, now_pfs_t->bitmap);
  if (!l) {
    return;
  }
  uint8_t *bm;
  bm = (uint8_t *)(uintptr_t)list_get(index_of_list,
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
void pfs_create_file_impl(vfs_t *vfs, char *filename, uint32_t dict_block) {
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
void pfs_create_dict_impl(vfs_t *vfs, char *name, uint32_t dict_block) {
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
static int pfs_vfs_normalize(const char *name, size_t length,
                             char *normalized, size_t capacity) {
  if (name == NULL || normalized == NULL || length == 0 || length >= 255 ||
      capacity <= length) {
    return VFS_ERROR_INVALID;
  }
  memcpy(normalized, name, length);
  normalized[length] = '\0';
  return VFS_OK;
}

static uint32_t pfs_vfs_directory(const vfs_node_t *node) {
  return node != NULL && node->type == VFS_NODE_DIRECTORY &&
                 node->id.value[0] == 1
             ? node->id.value[1]
             : UINT_MAX;
}

static void pfs_vfs_node(uint32_t directory, uint32_t index,
                         const pfs_inode *inode, vfs_node_t *node) {
  memset(node, 0, sizeof(*node));
  node->size = inode->size;
  node->modified_time = inode->time;
  if (inode->type == 2) {
    node->id.value[0] = 1;
    node->id.value[1] = inode->dat;
    node->type = VFS_NODE_DIRECTORY;
    node->attributes = DIR;
  } else {
    node->id.value[0] = 2;
    node->id.value[1] = directory;
    node->id.value[2] = index;
    node->type = VFS_NODE_FILE;
    node->attributes = FLE;
  }
}

static bool pfs_vfs_inode(vfs_t *vfs, const vfs_node_t *node,
                          pfs_inode *inode) {
  if (node == NULL || node->id.value[0] != 2 ||
      node->type != VFS_NODE_FILE) {
    return false;
  }
  *inode = pfs_get_inode_by_index(vfs, node->id.value[2], node->id.value[1]);
  return inode->type == 1;
}

static int pfs_vfs_root(vfs_t *vfs, vfs_node_t *node) {
  memset(node, 0, sizeof(*node));
  node->id.value[0] = 1;
  node->id.value[1] = now_pfs_t->root_dict_block;
  node->type = VFS_NODE_DIRECTORY;
  node->attributes = DIR;
  return VFS_OK;
}

static int pfs_vfs_lookup(vfs_t *vfs, const vfs_node_t *directory_node,
                          const char *name, vfs_node_t *node) {
  uint32_t directory = pfs_vfs_directory(directory_node);
  if (directory == UINT_MAX) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  uint32_t error = 0;
  uint32_t index =
      pfs_get_idx_of_inode_by_name(vfs, (char *)name, directory, &error);
  if (error == 0x114514) {
    return VFS_ERROR_NO_ENTRY;
  }
  pfs_inode inode = pfs_get_inode_by_index(vfs, index, directory);
  pfs_vfs_node(directory, index, &inode, node);
  return VFS_OK;
}

static bool pfs_vfs_block_at(vfs_t *vfs, uint32_t start, uint32_t index,
                             uint32_t *block) {
  uint32_t current = start;
  while (index-- != 0) {
    if (current == 0) {
      return false;
    }
    pfs_data_block data;
    now_pfs_t->read_block(now_pfs_t, block2sector(current, now_pfs_t), 1,
                          &data);
    current = data.next;
  }
  if (current == 0) {
    return false;
  }
  *block = current;
  return true;
}

static bool pfs_vfs_ensure(vfs_t *vfs, pfs_inode *inode, uint32_t count) {
  if (count == 0) {
    return true;
  }
  if (inode->dat == 0) {
    inode->dat = pfs_alloc_block(vfs, NULL);
    if (inode->dat == 0) {
      return false;
    }
    pfs_init_data_block(vfs, inode->dat);
  }
  uint32_t existing = 1;
  uint32_t current = inode->dat;
  pfs_data_block data;
  for (;;) {
    now_pfs_t->read_block(now_pfs_t, block2sector(current, now_pfs_t), 1,
                          &data);
    if (data.next == 0) {
      break;
    }
    current = data.next;
    existing++;
  }
  while (existing < count) {
    uint32_t next = pfs_alloc_block(vfs, NULL);
    if (next == 0) {
      return false;
    }
    pfs_init_data_block(vfs, next);
    data.next = next;
    now_pfs_t->write_block(now_pfs_t, block2sector(current, now_pfs_t), 1,
                           &data);
    current = next;
    now_pfs_t->read_block(now_pfs_t, block2sector(current, now_pfs_t), 1,
                          &data);
    existing++;
  }
  return true;
}

static bool pfs_vfs_transfer(vfs_t *vfs, uint32_t start, uint32_t offset,
                             void *buffer, uint32_t length, bool write,
                             bool zero) {
  if (length == 0) {
    return true;
  }
  uint32_t block;
  if (!pfs_vfs_block_at(vfs, start, offset / 508, &block)) {
    return false;
  }
  uint32_t in_block = offset % 508;
  uint32_t completed = 0;
  while (completed < length) {
    pfs_data_block data;
    now_pfs_t->read_block(now_pfs_t, block2sector(block, now_pfs_t), 1,
                          &data);
    uint32_t chunk = 508 - in_block;
    if (chunk > length - completed) {
      chunk = length - completed;
    }
    if (write) {
      if (zero) {
        memset(data.data + in_block, 0, chunk);
      } else {
        memcpy(data.data + in_block, (uint8_t *)buffer + completed, chunk);
      }
      now_pfs_t->write_block(now_pfs_t, block2sector(block, now_pfs_t), 1,
                             &data);
    } else {
      memcpy((uint8_t *)buffer + completed, data.data + in_block, chunk);
    }
    completed += chunk;
    in_block = 0;
    block = data.next;
    if (completed < length && block == 0) {
      return false;
    }
  }
  return true;
}

static int pfs_vfs_read(vfs_t *vfs, const vfs_node_t *node, uint32_t offset,
                        void *buffer, uint32_t length) {
  pfs_inode inode;
  if (!pfs_vfs_inode(vfs, node, &inode) || offset > inode.size) {
    return VFS_ERROR_NO_ENTRY;
  }
  if (length > inode.size - offset) {
    length = inode.size - offset;
  }
  return length == 0 ||
                 pfs_vfs_transfer(vfs, inode.dat, offset, buffer, length,
                                  false, false)
             ? (int)length
             : VFS_ERROR_IO;
}

static int pfs_vfs_resize(vfs_t *vfs, vfs_node_t *node, uint32_t size) {
  pfs_inode inode;
  if (!pfs_vfs_inode(vfs, node, &inode)) {
    return VFS_ERROR_NO_ENTRY;
  }
  uint32_t required = size == 0 ? 0 : (size - 1) / 508 + 1;
  if (!pfs_vfs_ensure(vfs, &inode, required)) {
    return VFS_ERROR_NO_SPACE;
  }
  if (required == 0 && inode.dat != 0) {
    pfs_delete_data_block(vfs, inode.dat);
    pfs_free_block(vfs, inode.dat);
    inode.dat = 0;
  } else if (required != 0) {
    uint32_t last;
    if (!pfs_vfs_block_at(vfs, inode.dat, required - 1, &last)) {
      return VFS_ERROR_IO;
    }
    pfs_data_block data;
    now_pfs_t->read_block(now_pfs_t, block2sector(last, now_pfs_t), 1, &data);
    if (data.next != 0) {
      pfs_delete_data_block(vfs, data.next);
      pfs_free_block(vfs, data.next);
      data.next = 0;
      now_pfs_t->write_block(now_pfs_t, block2sector(last, now_pfs_t), 1,
                             &data);
    }
  }
  if (size > inode.size &&
      !pfs_vfs_transfer(vfs, inode.dat, inode.size, NULL, size - inode.size,
                        true, true)) {
    return VFS_ERROR_IO;
  }
  if (size < inode.size && size != 0 && size % 508 != 0 &&
      !pfs_vfs_transfer(vfs, inode.dat, size, NULL, 508 - size % 508, true,
                        true)) {
    return VFS_ERROR_IO;
  }
  inode.size = size;
  inode.time = (uint32_t)time();
  pfs_set_inode_by_index(vfs, node->id.value[2], node->id.value[1], &inode);
  node->size = size;
  node->modified_time = inode.time;
  return VFS_OK;
}

static int pfs_vfs_write(vfs_t *vfs, vfs_node_t *node, uint32_t offset,
                         const void *buffer, uint32_t length) {
  if (offset > UINT_MAX - length) {
    return VFS_ERROR_OVERFLOW;
  }
  pfs_inode inode;
  if (!pfs_vfs_inode(vfs, node, &inode)) {
    return VFS_ERROR_NO_ENTRY;
  }
  uint32_t previous = inode.size;
  uint32_t end = offset + length;
  if (end > previous) {
    int status = pfs_vfs_resize(vfs, node, end);
    if (status < 0) {
      return status;
    }
    pfs_vfs_inode(vfs, node, &inode);
  }
  if (!pfs_vfs_transfer(vfs, inode.dat, offset, (void *)buffer, length, true,
                        false)) {
    if (end > previous) {
      pfs_vfs_resize(vfs, node, previous);
    }
    return VFS_ERROR_IO;
  }
  return length;
}

static int pfs_vfs_create(vfs_t *vfs, const vfs_node_t *directory_node,
                          const char *name, vfs_node_type_t type,
                          vfs_node_t *node) {
  uint32_t directory = pfs_vfs_directory(directory_node);
  if (directory == UINT_MAX) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  uint32_t error = 0;
  pfs_get_idx_of_inode_by_name(vfs, (char *)name, directory, &error);
  if (error != 0x114514) {
    return VFS_ERROR_EXISTS;
  }
  if (type == VFS_NODE_DIRECTORY) {
    pfs_create_dict_impl(vfs, (char *)name, directory);
  } else {
    pfs_create_file_impl(vfs, (char *)name, directory);
  }
  return pfs_vfs_lookup(vfs, directory_node, name, node);
}

static int pfs_vfs_remove(vfs_t *vfs, const vfs_node_t *directory_node,
                          const char *name, vfs_node_type_t type) {
  uint32_t directory = pfs_vfs_directory(directory_node);
  uint32_t error = 0;
  if (directory == UINT_MAX) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  uint32_t index =
      pfs_get_idx_of_inode_by_name(vfs, (char *)name, directory, &error);
  if (error == 0x114514) {
    return VFS_ERROR_NO_ENTRY;
  }
  pfs_inode inode = pfs_get_inode_by_index(vfs, index, directory);
  if ((inode.type == 2) != (type == VFS_NODE_DIRECTORY)) {
    return type == VFS_NODE_DIRECTORY ? VFS_ERROR_NOT_DIRECTORY
                                      : VFS_ERROR_IS_DIRECTORY;
  }
  if (inode.type == 2 && pfs_get_dict_number(vfs, inode.dat) != 0) {
    return VFS_ERROR_NOT_EMPTY;
  }
  if (inode.type == 2) {
    pfs_delete_dict(vfs, (char *)name, directory);
  } else {
    pfs_delete_file(vfs, (char *)name, directory);
  }
  return VFS_OK;
}

static int pfs_vfs_rename(vfs_t *vfs, const vfs_node_t *source_directory,
                          const char *source_name,
                          const vfs_node_t *destination_directory,
                          const char *destination_name) {
  uint32_t source = pfs_vfs_directory(source_directory);
  uint32_t destination = pfs_vfs_directory(destination_directory);
  if (source == UINT_MAX || destination == UINT_MAX) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  if (source != destination) {
    return VFS_ERROR_NOT_SUPPORTED;
  }
  uint32_t error = 0;
  pfs_get_idx_of_inode_by_name(vfs, (char *)destination_name, source, &error);
  if (error != 0x114514) {
    return VFS_ERROR_EXISTS;
  }
  uint32_t index =
      pfs_get_idx_of_inode_by_name(vfs, (char *)source_name, source, &error);
  if (error == 0x114514) {
    return VFS_ERROR_NO_ENTRY;
  }
  pfs_inode inode = pfs_get_inode_by_index(vfs, index, source);
  pfs_delete_name_link(vfs, inode.next, source);
  pfs_make_inode(vfs, index, (char *)destination_name, inode.type, source);
  pfs_inode renamed = pfs_get_inode_by_index(vfs, index, source);
  renamed.dat = inode.dat;
  renamed.size = inode.size;
  renamed.time = inode.time;
  renamed.attr = inode.attr;
  pfs_set_inode_by_index(vfs, index, source, &renamed);
  return VFS_OK;
}

static bool pfs_vfs_name(vfs_t *vfs, uint32_t directory,
                         const pfs_inode *inode, char name[255]) {
  uint32_t length = inode->name[13] == 0 ? strlen(inode->name) : 13;
  if (length >= 255) {
    return false;
  }
  memcpy(name, inode->name, length);
  uint32_t next = inode->name[13] == 0 ? 0 : inode->next;
  while (next != 0) {
    pfs_inode raw = pfs_get_inode_by_index(vfs, next, directory);
    pfs_inode_of_long_file_name *part = (pfs_inode_of_long_file_name *)&raw;
    uint32_t chunk = part->name[26] == 0 ? strlen(part->name) : 26;
    if (length + chunk >= 255) {
      return false;
    }
    memcpy(name + length, part->name, chunk);
    length += chunk;
    next = part->name[26] == 0 ? 0 : part->next;
  }
  name[length] = '\0';
  return true;
}

static int pfs_vfs_iterate(vfs_t *vfs, const vfs_node_t *directory_node,
                           uint32_t wanted, vfs_dir_entry_t *entry) {
  uint32_t directory = pfs_vfs_directory(directory_node);
  if (directory == UINT_MAX) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  uint32_t block = directory;
  uint32_t block_number = 0;
  uint32_t visible = 0;
  bool first = true;
  while (block != 0 || first) {
    first = false;
    pfs_dict_block data;
    now_pfs_t->read_block(now_pfs_t, block2sector(block, now_pfs_t), 1, &data);
    for (uint32_t slot = 0; slot < 15; slot++) {
      pfs_inode *inode = &data.inodes[slot];
      if (inode->type != 1 && inode->type != 2) {
        continue;
      }
      if (visible++ == wanted) {
        uint32_t index = block_number * 15 + slot;
        if (!pfs_vfs_name(vfs, directory, inode, entry->name)) {
          return VFS_ERROR_OVERFLOW;
        }
        pfs_vfs_node(directory, index, inode, &entry->node);
        return 1;
      }
    }
    block = data.next;
    block_number++;
  }
  return 0;
}

static int pfs_vfs_sync(vfs_t *vfs) {
  pfs_flush_bitmap(vfs);
  return VFS_OK;
}

void pfs_delete_name_link(vfs_t *vfs, uint32_t next, uint32_t dict_block) {
  while (next != 0) {
    pfs_inode inode = pfs_get_inode_by_index(vfs, next, dict_block);
    uint32_t following = inode.next;
    memset(&inode, 0, sizeof(inode));
    pfs_set_inode_by_index(vfs, next, dict_block, &inode);
    next = following;
  }
}

static void pfs_delete_fs(vfs_t *vfs) {
  if (now_pfs_t == NULL) {
    return;
  }
  pfs_flush_bitmap(vfs);
  if (now_pfs_t->bitmap_buffer != NULL) {
    for (int index = 1; list_get(index, now_pfs_t->bitmap_buffer) != NULL;
         index++) {
      free((void *)(uintptr_t)list_get(index, now_pfs_t->bitmap_buffer)->val);
    }
    DeleteList(now_pfs_t->bitmap_buffer);
  }
  if (now_pfs_t->bitmap != NULL) {
    DeleteList(now_pfs_t->bitmap);
  }
  free(now_pfs_t);
  vfs_mount_set_data(vfs, NULL);
}

static int pfs_vfs_mount(vfs_t *vfs) {
  pfs_t *state = malloc(sizeof(*state));
  if (state == NULL) {
    return VFS_ERROR_NO_MEMORY;
  }
  memset(state, 0, sizeof(*state));
  state->disk_number = vfs_mount_disk_number(vfs);
  state->read_block = pfs_read_block;
  state->write_block = pfs_write_block;
  state->current_bitmap_block = -1ll;
  vfs_mount_set_data(vfs, state);
  uint8_t mbr[512];
  state->read_block(state, 0, 1, mbr);
  pfs_mbr *header = (pfs_mbr *)mbr;
  if (memcmp(header->sign, "PFS\xff", 4) != 0) {
    pfs_delete_fs(vfs);
    return VFS_ERROR_IO;
  }
  state->first_sec_of_bitmap = header->first_sector_of_bitmap;
  state->resd_sec_end = header->resd_sector_end;
  state->resd_sec_start = header->resd_sector_start;
  state->root_dict_block = header->root_dict_block;
  state->sec_bitmap_start = header->sec_bitmap_start;
  state->bitmap = NewList();
  state->bitmap_buffer = NewList();
  if (state->bitmap == NULL || state->bitmap_buffer == NULL ||
      !init_bitmap(vfs)) {
    pfs_delete_fs(vfs);
    return VFS_ERROR_NO_MEMORY;
  }
  return VFS_OK;
}

static bool pfs_check(uint8_t disk_number) {
  if (!DiskReady(disk_number)) {
    return false;
  }
  uint8_t mbr[512];
  disk_read(0, 1, mbr, disk_number);
  return memcmp(((pfs_mbr *)mbr)->sign, "PFS\xff", 4) == 0;
}

static bool pfs_format(uint8_t disk_number) {
  pfs_t state;
  memset(&state, 0, sizeof(state));
  state.read_block = pfs_read_block;
  state.write_block = pfs_write_block;
  state.disk_number = disk_number;
  char volume[16] = "POWERINTDOS386";
  return pfs_format_impl(state, volume);
}

void reg_pfs(void) {
  static const vfs_filesystem_t filesystem = {
      .name = "PFS",
      .check = pfs_check,
      .format = pfs_format,
      .mount = pfs_vfs_mount,
      .unmount = pfs_delete_fs,
      .root = pfs_vfs_root,
      .normalize_name = pfs_vfs_normalize,
      .lookup = pfs_vfs_lookup,
      .read = pfs_vfs_read,
      .write = pfs_vfs_write,
      .truncate = pfs_vfs_resize,
      .create = pfs_vfs_create,
      .remove = pfs_vfs_remove,
      .rename = pfs_vfs_rename,
      .iterate = pfs_vfs_iterate,
      .sync = pfs_vfs_sync,
  };
  vfs_register_fs(&filesystem);
}
