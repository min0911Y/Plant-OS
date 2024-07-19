#pragma once
#include <copi143-define.h>
#include <dosldr/vfs.h>
#include <type.h>
void pfs_get_file_index_by_path(vfs_t *vfs, char *path, u32 start_block, u32 *err, u32 *idx,
                                u32 *dict_block);
void pfs_delete_name_link(vfs_t *vfs, u32 next, u32 dict_block);
void reg_pfs();
void pfs_flush_bitmap(vfs_t *vfs);