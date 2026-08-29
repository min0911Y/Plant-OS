#include <dos.h>
#include <irq.h>
#include <limits.h>
#define vfs(task) ((task)->nfs)
#define PDEBUG(...) ((void)0)

enum vfs_mount_state {
  VFS_MOUNT_INITIALIZING,
  VFS_MOUNT_ACTIVE,
  VFS_MOUNT_RETIRED,
};

struct vfs_mount {
  vfs_t filesystem;
  vfs_t *instances;
  unsigned int references;
  enum vfs_mount_state state;
  bool retired_registered;
};

struct vfs_disk_state {
  unsigned int retired_count;
  bool formatting;
};

struct vfs_path {
  vfs_t *filesystem;
  struct vfs_mount *mount;
};

vfs_t vfsstl[26];
static struct vfs_mount *vfs_mounts[26];
static struct vfs_disk_state vfs_disk_states[26];

static bool normalize_drive(uint8_t drive, uint8_t *normalized) {
  if (drive >= 'a' && drive <= 'z') {
    drive -= 'a' - 'A';
  }
  if (drive < 'A' || drive > 'Z') {
    return false;
  }
  *normalized = drive;
  return true;
}

static struct vfs_mount *find_active_mount(uint8_t drive) {
  for (int i = 0; i < 26; i++) {
    if (vfs_mounts[i] != NULL &&
        vfs_mounts[i]->state == VFS_MOUNT_ACTIVE &&
        vfs_mounts[i]->filesystem.drive == drive) {
      return vfs_mounts[i];
    }
  }
  return NULL;
}

static vfs_t *findSeat(vfs_t *vstl) {
  for (int i = 0; i < 26; i++) {
    if (vstl[i].flag == 0) {
      return &vstl[i];
    }
  }
  return NULL;
}
static vfs_t *check_disk_fs(uint8_t disk_number) {
  for (int i = 0; i < 26; i++) {
    if (vfsstl[i].flag == 1) {
      if (vfsstl[i].Check(disk_number)) {
        return &vfsstl[i];
      }
    }
  }
  return NULL;
}
static int find_mount_seat(void) {
  for (int i = 0; i < 26; i++) {
    if (vfs_mounts[i] == NULL) {
      return i;
    }
  }
  return -1;
}

static void insert_str(char *str, char *insert_str, int pos) {
  for (size_t i = 0; i < strlen(insert_str); i++) {
    insert_char(str, pos + i, insert_str[i]);
  }
}
static void vfs_destroy_mount(struct vfs_mount *mount) {
  bool unregister_retired = mount->retired_registered;
  unsigned int disk_index = mount->filesystem.disk_number - 'A';
  irq_state_t instance_state = irq_save();
  if (mount->instances != NULL &&
      (mount->instances != &mount->filesystem ||
       mount->filesystem.mount_prev != NULL ||
       mount->filesystem.mount_next != NULL)) {
    irq_restore(instance_state);
    Panic_K("destroying mount with live instances");
    return;
  }
  mount->instances = NULL;
  irq_restore(instance_state);
  if (mount->filesystem.DeleteFs != NULL) {
    mount->filesystem.DeleteFs(&mount->filesystem);
  }
  if (unregister_retired) {
    irq_state_t interrupt_state = irq_save();
    if (disk_index < 26 && vfs_disk_states[disk_index].retired_count != 0) {
      vfs_disk_states[disk_index].retired_count--;
    }
    mount->retired_registered = false;
    irq_restore(interrupt_state);
  }
  free(mount);
}

static void vfs_register_instance(struct vfs_mount *mount, vfs_t *instance) {
  irq_state_t state = irq_save();
  instance->mount_prev = NULL;
  instance->mount_next = mount->instances;
  if (mount->instances != NULL) {
    mount->instances->mount_prev = instance;
  }
  mount->instances = instance;
  irq_restore(state);
}

static void vfs_unregister_instance(vfs_t *instance) {
  struct vfs_mount *mount = instance->mount_owner;
  irq_state_t state = irq_save();
  if (instance->mount_prev != NULL) {
    instance->mount_prev->mount_next = instance->mount_next;
  } else if (mount != NULL && mount->instances == instance) {
    mount->instances = instance->mount_next;
  }
  if (instance->mount_next != NULL) {
    instance->mount_next->mount_prev = instance->mount_prev;
  }
  instance->mount_prev = NULL;
  instance->mount_next = NULL;
  irq_restore(state);
}

void vfs_visit_mount_instances(vfs_t *instance,
                               vfs_instance_visitor_t visitor,
                               void *context) {
  if (instance == NULL || instance->mount_owner == NULL || visitor == NULL) {
    return;
  }
  irq_state_t state = irq_save();
  for (vfs_t *cursor = instance->mount_owner->instances; cursor != NULL;
       cursor = cursor->mount_next) {
    visitor(cursor, context);
  }
  irq_restore(state);
}
static void vfs_release_mount_reference(struct vfs_mount *mount) {
  if (mount == NULL) {
    return;
  }
  irq_state_t interrupt_state = irq_save();
  bool destroy = false;
  if (mount->references != 0) {
    mount->references--;
    destroy = mount->state == VFS_MOUNT_RETIRED && mount->references == 0;
  }
  irq_restore(interrupt_state);
  if (destroy) {
    vfs_destroy_mount(mount);
  }
}

static struct vfs_mount *vfs_acquire_active_mount(uint8_t drive) {
  uint8_t normalized;
  if (!normalize_drive(drive, &normalized)) {
    return NULL;
  }
  irq_state_t interrupt_state = irq_save();
  struct vfs_mount *mount = find_active_mount(normalized);
  if (mount != NULL) {
    mount->references++;
  }
  irq_restore(interrupt_state);
  return mount;
}

static struct vfs_mount *vfs_acquire_instance_mount(const vfs_t *instance) {
  if (instance == NULL || instance->mount_owner == NULL) {
    return NULL;
  }
  irq_state_t interrupt_state = irq_save();
  struct vfs_mount *mount = instance->mount_owner;
  if (mount->state == VFS_MOUNT_INITIALIZING || mount->references == 0) {
    mount = NULL;
  } else {
    mount->references++;
  }
  irq_restore(interrupt_state);
  return mount;
}

static void vfs_release_instance(vfs_t *vfs) {
  if (vfs == NULL) {
    return;
  }
  struct vfs_mount *mount = vfs->mount_owner;
  vfs_unregister_instance(vfs);
  while (FindForCount(1, vfs->path) != NULL) {
    free((void *)(uintptr_t)FindForCount(vfs->path->ctl->all, vfs->path)->val);
    DeleteVal(vfs->path->ctl->all, vfs->path);
  }
  if (vfs->ReleaseCache != NULL) {
    vfs->ReleaseCache(vfs);
  }
  DeleteList(vfs->path);
  free(vfs);
  vfs_release_mount_reference(mount);
}

static vfs_t *vfs_create_instance(struct vfs_mount *mount) {
  if (mount == NULL || mount->filesystem.CopyCache == NULL ||
      mount->filesystem.ReleaseCache == NULL || mount->filesystem.cd == NULL) {
    vfs_release_mount_reference(mount);
    return NULL;
  }
  vfs_t *instance = malloc(sizeof(vfs_t));
  if (instance == NULL) {
    vfs_release_mount_reference(mount);
    return NULL;
  }
  memcpy(instance, &mount->filesystem, sizeof(vfs_t));
  instance->mount_prev = NULL;
  instance->mount_next = NULL;
  if (!mount->filesystem.CopyCache(instance, &mount->filesystem)) {
    free(instance);
    vfs_release_mount_reference(mount);
    return NULL;
  }
  instance->path = NewList();
  if (instance->path == NULL) {
    if (instance->ReleaseCache != NULL) {
      instance->ReleaseCache(instance);
    }
    free(instance);
    vfs_release_mount_reference(mount);
    return NULL;
  }
  vfs_register_instance(mount, instance);
  return instance;
}

static bool vfs_resolve_path(char *path, struct vfs_path *resolved) {
  resolved->filesystem = NULL;
  resolved->mount = NULL;
  if (path == NULL) {
    return false;
  }

  size_t length = strlen(path);
  if (length >= 2 && path[1] == ':') {
    struct vfs_mount *mount = vfs_acquire_active_mount((uint8_t)path[0]);
    if (mount == NULL) {
      return false;
    }
    memmove(path, path + 2, length - 1);
    resolved->filesystem = &mount->filesystem;
    resolved->mount = mount;
  } else {
    if (vfs_now == NULL) {
      return false;
    }
    /* The current task's VFS instance already holds its mount reference. */
    resolved->filesystem = vfs_now;
  }

  for (size_t i = 0; path[i] != '\0'; i++) {
    if (path[i] == '\\') {
      path[i] = '/';
    }
  }
  return true;
}

static void vfs_release_path(struct vfs_path *resolved) {
  vfs_release_mount_reference(resolved->mount);
  resolved->filesystem = NULL;
  resolved->mount = NULL;
}

static bool vfs_open_path(const char *path, char **path_copy,
                          struct vfs_path *resolved) {
  if (path == NULL) {
    return false;
  }
  size_t length = strlen(path);
  if (length >= (size_t)INT_MAX) {
    return false;
  }
  char *copy = malloc((int)length + 1);
  if (copy == NULL) {
    return false;
  }
  memcpy(copy, path, length + 1);
  if (!vfs_resolve_path(copy, resolved)) {
    free(copy);
    return false;
  }
  *path_copy = copy;
  return true;
}

static void vfs_close_path(char *path, struct vfs_path *resolved) {
  free(path);
  vfs_release_path(resolved);
}

void vfs_free_task_instance(vfs_t *vfs) { vfs_release_instance(vfs); }
bool vfs_mount_disk(uint8_t disk_number, uint8_t drive) {
  uint8_t normalized_disk;
  uint8_t normalized_drive;
  if (!normalize_drive(disk_number, &normalized_disk) ||
      !normalize_drive(drive, &normalized_drive)) {
    return false;
  }
  PDEBUG("Mount DISK ---- %02x", disk_number);
  struct vfs_mount *mount = malloc(sizeof(struct vfs_mount));
  if (mount == NULL) {
    return false;
  }
  memset(mount, 0, sizeof(struct vfs_mount));
  mount->filesystem.drive = normalized_drive;
  mount->filesystem.disk_number = normalized_disk;
  mount->state = VFS_MOUNT_INITIALIZING;

  irq_state_t interrupt_state = irq_save();
  int reservation_error = 0;
  if (vfs_disk_states[normalized_disk - 'A'].formatting) {
    reservation_error = 1;
  }
  for (int i = 0; reservation_error == 0 && i < 26; i++) {
    if (vfs_mounts[i] != NULL &&
        (vfs_mounts[i]->filesystem.drive == normalized_drive ||
         vfs_mounts[i]->filesystem.disk_number == normalized_disk)) {
      reservation_error = 2;
    }
  }
  int seat = reservation_error == 0 ? find_mount_seat() : -1;
  if (reservation_error == 0 && seat < 0) {
    reservation_error = 3;
  }
  if (reservation_error == 0) {
    vfs_mounts[seat] = mount;
  }
  irq_restore(interrupt_state);
  if (reservation_error != 0) {
    if (reservation_error == 2) {
      WARNING_K("disk or drive is already mounted");
    } else if (reservation_error == 3) {
      WARNING_K("mount table is full");
    }
    free(mount);
    return false;
  }

  vfs_t *fs = check_disk_fs(normalized_disk);
  if (fs == NULL) {
    interrupt_state = irq_save();
    if (vfs_mounts[seat] == mount) {
      vfs_mounts[seat] = NULL;
    }
    irq_restore(interrupt_state);
    WARNING_K("unknown file system");
    free(mount);
    return false;
  }
  mount->filesystem = *fs;
  mount->filesystem.mount_owner = mount;
  mount->filesystem.drive = normalized_drive;
  mount->filesystem.disk_number = normalized_disk;
  mount->filesystem.flag = 1;

  if (!mount->filesystem.InitFs(&mount->filesystem, normalized_disk)) {
    interrupt_state = irq_save();
    if (vfs_mounts[seat] == mount) {
      vfs_mounts[seat] = NULL;
    }
    mount->state = VFS_MOUNT_RETIRED;
    irq_restore(interrupt_state);
    vfs_destroy_mount(mount);
    return false;
  }
  mount->filesystem.mount_prev = NULL;
  mount->filesystem.mount_next = NULL;
  mount->instances = &mount->filesystem;
  interrupt_state = irq_save();
  mount->state = VFS_MOUNT_ACTIVE;
  irq_restore(interrupt_state);
  PDEBUG("success");
  return true;
}
bool vfs_mount_all_disks(void) {
  bool mounted = false;
  for (char drive = first_vdisk(); drive != 0; drive = next_vdisk(drive)) {
    if (vfs_check_mount(drive)) {
      mounted = true;
      continue;
    }
    if (vfs_mount_disk(drive, drive)) {
      mounted = true;
    }
  }
  return mounted;
}
bool vfs_unmount_disk(uint8_t drive) {
  uint8_t normalized;
  if (!normalize_drive(drive, &normalized)) {
    return false;
  }
  PDEBUG("Unmount disk ---- %c", drive);
  irq_state_t interrupt_state = irq_save();
  for (int i = 0; i < 26; i++) {
    if (vfs_mounts[i] != NULL &&
        vfs_mounts[i]->state == VFS_MOUNT_ACTIVE &&
        vfs_mounts[i]->filesystem.drive == normalized) {
      struct vfs_mount *mount = vfs_mounts[i];
      vfs_mounts[i] = NULL;
      mount->state = VFS_MOUNT_RETIRED;
      mount->retired_registered = true;
      vfs_disk_states[mount->filesystem.disk_number - 'A'].retired_count++;
      bool destroy = mount->references == 0;
      irq_restore(interrupt_state);
      if (destroy) {
        vfs_destroy_mount(mount);
      }
      PDEBUG("Unmount ok!");
      return true;
    }
  }
  irq_restore(interrupt_state);
  return false;
}
bool vfs_readfile(char *path, char *buffer) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(path, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  logk("Readfile %s to %08x\n", path, buffer);
  if (resolved.filesystem->ReadFile == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result =
      resolved.filesystem->ReadFile(resolved.filesystem, new_path, buffer);
  vfs_close_path(new_path, &resolved);
  logk("OK\n");
  return result;
}
bool vfs_writefile(char *path, char *buffer, int size) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(path, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (resolved.filesystem->WriteFile == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result = resolved.filesystem->WriteFile(
      resolved.filesystem, new_path, buffer, size);
  vfs_close_path(new_path, &resolved);
  return result;
}
uint32_t vfs_filesize(char *filename) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(filename, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return -1;
  }
  if (resolved.filesystem->FileSize == NULL) {
    vfs_close_path(new_path, &resolved);
    return -1;
  }
  int result = resolved.filesystem->FileSize(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
List *vfs_listfile(char *dictpath) { // dictpath == "" 则表示当前路径
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(dictpath, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return NULL;
  }
  if (resolved.filesystem->ListFile == NULL) {
    vfs_close_path(new_path, &resolved);
    return NULL;
  }
  List *result =
      resolved.filesystem->ListFile(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_delfile(char *filename) {
  PDEBUG("Delete file %s.\n", filename);
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(filename, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (resolved.filesystem->DelFile == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result =
      resolved.filesystem->DelFile(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_deldir(char *dictname) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(dictname, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (resolved.filesystem->DelDict == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result =
      resolved.filesystem->DelDict(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_createfile(char *filename) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(filename, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (resolved.filesystem->CreateFile == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result =
      resolved.filesystem->CreateFile(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_createdict(char *filename) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(filename, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (resolved.filesystem->CreateDict == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result =
      resolved.filesystem->CreateDict(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_renamefile(char *filename, char *filename_of_new) {
  char *source_path;
  char *destination_path;
  struct vfs_path source;
  struct vfs_path destination;
  if (!vfs_open_path(filename, &source_path, &source)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (!vfs_open_path(filename_of_new, &destination_path, &destination)) {
    vfs_close_path(source_path, &source);
    return false;
  }
  if (source.filesystem->mount_owner != destination.filesystem->mount_owner ||
      source.filesystem->RenameFile == NULL) {
    vfs_close_path(destination_path, &destination);
    vfs_close_path(source_path, &source);
    return false;
  }
  bool result = source.filesystem->RenameFile(
      source.filesystem, source_path, destination_path);
  vfs_close_path(destination_path, &destination);
  vfs_close_path(source_path, &source);
  return result;
}
bool vfs_attrib(char *filename, ftype type) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(filename, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return false;
  }
  if (resolved.filesystem->Attrib == NULL) {
    vfs_close_path(new_path, &resolved);
    return false;
  }
  bool result =
      resolved.filesystem->Attrib(resolved.filesystem, new_path, type);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_format(uint8_t disk_number, char *FSName) {
  uint8_t normalized;
  if (!normalize_drive(disk_number, &normalized) || FSName == NULL) {
    return false;
  }
  bool (*format)(uint8_t) = NULL;
  for (int i = 0; i < 26; i++) {
    if (strcmp(vfsstl[i].FSName, FSName) == 0 && vfsstl[i].flag == 1 &&
        vfsstl[i].Format != NULL) {
      format = vfsstl[i].Format;
      break;
    }
  }
  if (format == NULL) {
    return false;
  }

  unsigned int disk_index = normalized - 'A';
  irq_state_t interrupt_state = irq_save();
  if (vfs_disk_states[disk_index].formatting ||
      vfs_disk_states[disk_index].retired_count != 0) {
    irq_restore(interrupt_state);
    return false;
  }
  struct vfs_mount *unused_mount = NULL;
  int unused_seat = -1;
  for (int i = 0; i < 26; i++) {
    if (vfs_mounts[i] != NULL &&
        vfs_mounts[i]->filesystem.disk_number == normalized) {
      struct vfs_mount *mount = vfs_mounts[i];
      if (mount->state != VFS_MOUNT_ACTIVE || mount->references != 0 ||
          unused_mount != NULL) {
        irq_restore(interrupt_state);
        return false;
      }
      unused_mount = mount;
      unused_seat = i;
    }
  }
  vfs_disk_states[disk_index].formatting = true;
  if (unused_mount != NULL) {
    vfs_mounts[unused_seat] = NULL;
    unused_mount->state = VFS_MOUNT_RETIRED;
  }
  irq_restore(interrupt_state);

  if (unused_mount != NULL) {
    vfs_destroy_mount(unused_mount);
  }
  bool result = format(normalized);
  interrupt_state = irq_save();
  vfs_disk_states[disk_index].formatting = false;
  irq_restore(interrupt_state);
  return result;
}
vfs_file *vfs_fileinfo(char *filename) {
  char *new_path;
  struct vfs_path resolved;
  if (!vfs_open_path(filename, &new_path, &resolved)) {
    WARNING_K("Attempt read a nonexistent disk");
    return NULL;
  }
  if (resolved.filesystem->FileInfo == NULL) {
    vfs_close_path(new_path, &resolved);
    return NULL;
  }
  vfs_file *result =
      resolved.filesystem->FileInfo(resolved.filesystem, new_path);
  vfs_close_path(new_path, &resolved);
  return result;
}
bool vfs_change_disk(uint8_t drive) {
  PDEBUG("will change to %c", drive);
  PDEBUG("Find mount.......");
  PDEBUG("Changing......");
  vfs_t *instance = vfs_create_instance(vfs_acquire_active_mount(drive));
  if (instance == NULL) {
    WARNING_K("no mount.");
    return false;
  }
  vfs_t *previous = vfs_now;
  vfs_now = instance;
  vfs_release_instance(previous);
  PDEBUG("OK.");
  return true;
}
bool vfs_change_disk_for_task(uint8_t drive, mtask *task) {
  if (task == NULL) {
    return false;
  }
  PDEBUG("will change to %c", drive);
  PDEBUG("Find mount.......");
  PDEBUG("Changing......");
  vfs_t *instance = vfs_create_instance(vfs_acquire_active_mount(drive));
  if (instance == NULL) {
    WARNING_K("no mount.");
    return false;
  }
  vfs_t *previous = vfs(task);
  vfs(task) = instance;
  vfs_release_instance(previous);
  PDEBUG("OK.");
  return true;
}
bool vfs_clone_for_task(mtask *src, mtask *dest) {
  if (src == NULL || dest == NULL || src->nfs == NULL) {
    return false;
  }
  vfs_t *instance =
      vfs_create_instance(vfs_acquire_instance_mount(src->nfs));
  if (instance == NULL) {
    return false;
  }
  for (int i = 1; FindForCount(i, src->nfs->path) != NULL; i++) {
    List *entry = FindForCount(i, src->nfs->path);
    if (!instance->cd(instance, (char *)(uintptr_t)entry->val)) {
      vfs_release_instance(instance);
      return false;
    }
  }
  vfs_t *previous = dest->nfs;
  dest->nfs = instance;
  vfs_release_instance(previous);
  return true;
}
bool vfs_change_path(char *dictName) {
  char *buf = malloc(strlen(dictName) + 1);
  char *r = buf;
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
    l = FindForCount(i, vfs_now->path);
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
    l = FindForCount(i, vfs_now->path);
    path = (char *)l->val;
    insert_char(buffer, pos, '/');
    pos++;
    insert_str(buffer, path, pos);
    pos += strlen(path);
  }
  if (i == 1) {
    insert_char(buffer, 0, '/');
  }
}
bool vfs_check_mount(uint8_t drive) {
  uint8_t normalized;
  if (!normalize_drive(drive, &normalized)) {
    return false;
  }
  irq_state_t interrupt_state = irq_save();
  bool mounted = find_active_mount(normalized) != NULL;
  irq_restore(interrupt_state);
  return mounted;
}
void init_vfs() {
  PDEBUG("init vfs..........");
  for (int i = 0; i < 26; i++) {
    vfsstl[i].flag = 0;
    vfsstl[i].disk_number = 0;
    vfsstl[i].drive = 0;
    vfs_mounts[i] = NULL;
    vfs_disk_states[i].retired_count = 0;
    vfs_disk_states[i].formatting = false;
  }
  PDEBUG("vfs ok.");
  vfs_now = NULL;
}
bool vfs_register_fs(vfs_t vfs) {
  PDEBUG("Register file system: %s", vfs.FSName);
  PDEBUG("looking for a seat of vfsstl.........");
  if (vfs.Check == NULL || vfs.InitFs == NULL || vfs.CopyCache == NULL ||
      vfs.ReleaseCache == NULL || vfs.cd == NULL) {
    return false;
  }
  vfs_t *seat;
  seat = findSeat(vfsstl);
  if (!seat) {
    WARNING_K("can not find a seat of vfsstl(it's full)");
    Panic_K("Register error!");
    return false;
  }
  *seat = vfs;
  seat->mount_owner = NULL;
  PDEBUG("success");
  return true;
}
