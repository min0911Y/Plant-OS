#include <dos.h>
#define vfs(task)  ((task)->nfs)
#define toupper(c) ((c) >= 'a' && (c) <= 'z' ? c - 32 : c)
#define PDEBUG
vfs_t         vfsstl[26];
vfs_t         vfsMount_Stl[26];
static vfs_t *drive2fs(u8 drive) {
  for (int i = 0; i < 26; i++) {
    if (vfsMount_Stl[i].drive == toupper(drive) && vfsMount_Stl[i].flag == 1) {
      return &vfsMount_Stl[i];
    }
  }
  return NULL;
}
static vfs_t *ParsePath(char *result) {
  PDEBUG("Parse Path: %s", result);
  vfs_t *vfs_result = vfs_now;
  if (result[1] == ':') {
    if (!(vfs_result = drive2fs(result[0]))) {
      WARNING_K("Mount Drive is not found!");
      Panic_K("Parse Error.");
      return NULL;
    }
    if (result) {
      delete_char(result, 0);
      delete_char(result, 0);
    }
  }
  if (result) {
    for (int i = 0; i < strlen(result); i++) {
      if (result[i] == '\\') { result[i] = '/'; }
    }
  }
  PDEBUG("Parse Path OK: %s", result);
  return vfs_result;
}
static vfs_t *findSeat(vfs_t *vstl) {
  for (int i = 0; i < 26; i++) {
    if (vstl[i].flag == 0) { return &vstl[i]; }
  }
  return NULL;
}
static vfs_t *check_disk_fs(u8 disk_number) {
  for (int i = 0; i < 26; i++) {
    if (vfsstl[i].flag == 1) {
      if (vfsstl[i].Check(disk_number)) { return &vfsstl[i]; }
    }
  }
  return NULL;
}
static void insert_str(char *str, char *insert_str, int pos) {
  for (int i = 0; i < strlen(insert_str); i++) {
    insert_char(str, pos + i, insert_str[i]);
  }
}
bool vfs_mount_disk(u8 disk_number, u8 drive) {
  PDEBUG("Mount DISK ---- %02x", disk_number);
  for (int i = 0; i < 26; i++) {
    if (vfsMount_Stl[i].flag == 1 &&
        (vfsMount_Stl[i].drive == drive || vfsMount_Stl[i].disk_number == disk_number)) {
      WARNING_K("It mounted");
      return false;
    }
  }
  vfs_t *seat = findSeat(vfsMount_Stl);
  if (!seat) {
    WARNING_K("can not find a seat of vfsMount_Stl(it's full)");
    Panic_K("Mount error!");
    return false;
  }
  vfs_t *fs = check_disk_fs(disk_number);
  if (!fs) {
    WARNING_K("unknow file system.");
    Panic_K("Mount error!");
    return false;
  }
  *seat = *fs;
  seat->InitFs(seat, disk_number);
  seat->drive       = drive;
  seat->disk_number = disk_number;
  seat->flag        = 1;
  PDEBUG("success");
  return true;
}
bool vfs_unmount_disk(u8 drive) {
  PDEBUG("Unmount disk ---- %c", drive);
  for (int i = 0; i < 26; i++) {
    if (vfsMount_Stl[i].drive == drive && vfsMount_Stl[i].flag == 1) {
      vfsMount_Stl[i].DeleteFs(&vfsMount_Stl[i]);
      vfsMount_Stl[i].flag = 0;
      PDEBUG("Unmount ok!");
      return true;
    }
  }
  Panic_K("Not found the drive.");
  return false;
}
bool vfs_readfile(char *path, char *buffer) {
  logk("Readfile %s to %08x\n", path, buffer);
  char *new_path = malloc(strlen(path) + 1);
  strcpy(new_path, path);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->ReadFile(vfs, new_path, buffer);
  free(new_path);
  logk("OK\n");
  return result;
}
bool vfs_writefile(char *path, char *buffer, int size) {
  char *new_path = malloc(strlen(path) + 1);
  strcpy(new_path, path);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->WriteFile(vfs, new_path, buffer, size);
  free(new_path);
  return result;
}
u32 vfs_filesize(char *filename) {
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return -1;
  }
  int result = vfs->FileSize(vfs, new_path); // 没找到文件统一返回-1
  free(new_path);
  return result;
}
List *vfs_listfile(char *dictpath) { // dictpath == "" 则表示当前路径
  if (strcmp(dictpath, "") == 0) {
    return vfs_now->ListFile(vfs_now, dictpath);
  } else {
    char *new_path = malloc(strlen(dictpath) + 1);
    strcpy(new_path, dictpath);
    vfs_t *vfs = ParsePath(new_path);
    if (vfs == NULL) {
      WARNING_K("Attempt read a nonexistent disk");
      free(new_path);
      return NULL;
    }
    List *result = vfs->ListFile(vfs, new_path);
    free(new_path);
    return result;
  }
}
bool vfs_delfile(char *filename) {
  PDEBUG("Delete file %s.\n", filename);
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->DelFile(vfs, new_path);
  free(new_path);
  return result;
}
bool vfs_deldir(char *dictname) {
  char *new_path = malloc(strlen(dictname) + 1);
  strcpy(new_path, dictname);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->DelDict(vfs, new_path);
  free(new_path);
  return result;
}
bool vfs_createfile(char *filename) {
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->CreateFile(vfs, new_path);
  free(new_path);
  return result;
}
bool vfs_createdict(char *filename) {
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->CreateDict(vfs, new_path);
  free(new_path);
  return result;
}
bool vfs_renamefile(char *filename, char *filename_of_new) {
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->RenameFile(vfs, new_path, filename_of_new);
  free(new_path);
  return result;
}
bool vfs_attrib(char *filename, ftype type) {
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return false;
  }
  int result = vfs->Attrib(vfs, new_path, type);
  free(new_path);
  return result;
}
bool vfs_format(u8 disk_number, char *FSName) {
  for (int i = 0; i < 255; i++) {
    if (strcmp(vfsstl[i].FSName, FSName) == 0 && vfsstl[i].flag == 1) {
      return vfsstl[i].Format(disk_number);
    }
  }
  return false;
}
vfs_file *vfs_fileinfo(char *filename) {
  char *new_path = malloc(strlen(filename) + 1);
  strcpy(new_path, filename);
  vfs_t *vfs = ParsePath(new_path);
  if (vfs == NULL) {
    WARNING_K("Attempt read a nonexistent disk");
    free(new_path);
    return NULL;
  }
  vfs_file *result = vfs->FileInfo(vfs, new_path);
  free(new_path);
  return result;
}
bool vfs_change_disk(u8 drive) {
  PDEBUG("will change to %c", drive);
  if (vfs_now != NULL) {
    while (FindForCount(1, vfs_now->path) != NULL) {
      // printk("%d\n",vfs_now->path->ctl->all);
      free((void *)FindForCount(vfs_now->path->ctl->all, vfs_now->path)->val);
      DeleteVal(vfs_now->path->ctl->all, vfs_now->path);
    }
    free(vfs_now->cache);
    DeleteList(vfs_now->path);
    free(vfs_now);
  }
  PDEBUG("Find mount.......");
  vfs_t *f;
  if (!(f = drive2fs(drive))) {
    WARNING_K("no mount.");
    return false; // 没有mount
  }
  PDEBUG("Changing......");
  vfs_now = malloc(sizeof(vfs_t));
  memcpy(vfs_now, f, sizeof(vfs_t));
  f->CopyCache(vfs_now, f);
  vfs_now->path = NewList();
  vfs_now->cd(vfs_now, "/");
  PDEBUG("OK.");
  return true;
}
bool vfs_change_disk_for_task(u8 drive, mtask *task) {
  PDEBUG("will change to %c", drive);
  if (vfs(task) != NULL) {
    while (FindForCount(1, vfs(task)->path) != NULL) {
      //("%d\n",vfs_now->path->ctl->all);
      free((void *)FindForCount(vfs(task)->path->ctl->all, vfs(task)->path)->val);
      DeleteVal(vfs(task)->path->ctl->all, vfs(task)->path);
    }
    free(vfs(task)->cache);
    DeleteList(vfs(task)->path);
    free(vfs(task));
  }
  PDEBUG("Find mount.......");
  vfs_t *f;
  if (!(f = drive2fs(drive))) {
    WARNING_K("no mount.");
    return false; // 没有mount
  }
  PDEBUG("Changing......");
  vfs(task) = malloc(sizeof(vfs_t));
  memcpy(vfs(task), f, sizeof(vfs_t));
  f->CopyCache(vfs(task), f);
  vfs(task)->path = NewList();
  vfs(task)->cd(vfs(task), "/");
  PDEBUG("OK.");
  return true;
}
bool vfs_change_path(char *dictName) {
  char *buf = malloc(strlen(dictName) + 1);
  char *r   = buf;
  memcpy(buf, dictName, strlen(dictName) + 1);
  int i = 0;
  if (buf[i] == '/' || buf[i] == '\\') {
    if (!vfs_now->cd(vfs_now, "/")) {
      free(r);
      return false;
    }
    i++;
    buf++;
  }

  for (;; i++) {
    if (buf[i] == '/' || buf[i] == '\\') {
      buf[i] = 0;
      if (!vfs_now->cd(vfs_now, buf)) {
        free(r);
        return false;
      }
      buf += strlen(buf) + 1;
    }
    if (buf[i] == 0) {
      if (!vfs_now->cd(vfs_now, buf)) {
        free(r);
        return false;
      }
      break;
    }
  }
  free(r);
  return true;
}
void vfs_getPath(char *buffer) {
  char *path;
  List *l;
  buffer[0] = 0;
  insert_char(buffer, 0, vfs_now->drive);
  insert_char(buffer, 1, ':');
  insert_char(buffer, 2, '\\');
  PDEBUG("%s", vfs_now->FSName);
  int pos = strlen(buffer);
  for (int i = 1; FindForCount(i, vfs_now->path) != NULL; i++) {
    l    = FindForCount(i, vfs_now->path);
    path = (char *)l->val;
    insert_str(buffer, path, pos);
    pos += strlen(path);
    insert_char(buffer, pos, '\\');
    pos++;
  }
  delete_char(buffer, pos - 1);
}
void vfs_getPath_no_drive(char *buffer) {
  char *path;
  List *l;
  buffer[0] = 0;
  PDEBUG("%s", vfs_now->FSName);
  int pos = strlen(buffer);
  int i;
  for (i = 1; FindForCount(i, vfs_now->path) != NULL; i++) {
    l    = FindForCount(i, vfs_now->path);
    path = (char *)l->val;
    insert_char(buffer, pos, '/');
    pos++;
    insert_str(buffer, path, pos);
    pos += strlen(path);
  }
  if (i == 1) { insert_char(buffer, 0, '/'); }
}
bool vfs_check_mount(u8 drive) {
  return drive2fs(drive) ? true : false;
}
void init_vfs() {
  PDEBUG("init vfs..........");
  for (int i = 0; i < 26; i++) {
    vfsstl[i].flag              = 0;
    vfsstl[i].disk_number       = 0;
    vfsstl[i].drive             = 0;
    vfsMount_Stl[i].flag        = 0;
    vfsMount_Stl[i].disk_number = 0;
    vfsMount_Stl[i].drive       = 0;
    // PDEBUG("Set vfsstl[%d] & vfsMount_Stl[%d] OK.", i, i);
  }
  PDEBUG("vfs ok.");
  vfs_now = NULL;
}
bool vfs_register_fs(vfs_t vfs) {
  PDEBUG("Register file system: %s", vfs.FSName);
  PDEBUG("looking for a seat of vfsstl.........");
  vfs_t *seat;
  seat = findSeat(vfsstl);
  if (!seat) {
    WARNING_K("can not find a seat of vfsstl(it's full)");
    Panic_K("Register error!");
    return false;
  }
  *seat = vfs;
  PDEBUG("success");
  return true;
}
