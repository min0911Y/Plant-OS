#include <dos.h>
#include <irq.h>
#include <limits.h>

#define VFS_MAX_FILESYSTEMS 26
#define VFS_MAX_MOUNTS 26
#define VFS_CACHE_PAGE_SIZE 4096u
#define VFS_CACHE_BULK_READ_SIZE (2u * VFS_CACHE_PAGE_SIZE)
#define VFS_CACHE_HASH_BUCKETS 256u
#define VFS_CACHE_MEMORY_DIVISOR 128u
#define VFS_CACHE_MIN_BYTES (256u * 1024u)
#define VFS_CACHE_MAX_BYTES (8u * 1024u * 1024u)
#define VFS_INITIAL_FD_CAPACITY 16u

enum vfs_mount_state {
  VFS_MOUNT_INITIALIZING,
  VFS_MOUNT_ACTIVE,
  VFS_MOUNT_RETIRED,
};

struct vfs_dentry {
  struct vfs_mount *mount;
  struct vfs_dentry *parent;
  vfs_node_t node;
  char *name;
  uint32_t references;
};

struct vfs_mount {
  const vfs_filesystem_t *filesystem;
  void *data;
  struct vfs_dentry *root;
  lock_t lock;
  uint32_t references;
  uint8_t disk_number;
  uint8_t drive;
  enum vfs_mount_state state;
  bool retired_registered;
};

struct vfs_handle {
  struct vfs_handle *previous;
  struct vfs_handle *next;
  struct vfs_dentry *dentry;
  uint32_t offset;
  uint32_t flags;
  uint32_t references;
};

struct vfs_fd_table {
  vfs_handle_t **entries;
  uint32_t capacity;
};

struct vfs_context {
  struct vfs_context *previous;
  struct vfs_context *next;
  struct vfs_dentry *cwd;
  struct vfs_fd_table descriptors;
  uint32_t references;
};

struct vfs_cache_page {
  struct vfs_cache_page *previous;
  struct vfs_cache_page *next;
  struct vfs_cache_page *hash_next;
  struct vfs_mount *mount;
  vfs_node_id_t node;
  uint32_t index;
  uint8_t data[VFS_CACHE_PAGE_SIZE];
};

struct vfs_disk_state {
  uint32_t retired_count;
  bool formatting;
};

static const vfs_filesystem_t *vfs_filesystems[VFS_MAX_FILESYSTEMS];
static struct vfs_mount *vfs_mounts[VFS_MAX_MOUNTS];
static struct vfs_disk_state vfs_disks[VFS_MAX_MOUNTS];
static struct vfs_handle *vfs_handles;
static struct vfs_context *vfs_contexts;
static struct vfs_cache_page *vfs_cache_head;
static struct vfs_cache_page *vfs_cache_tail;
static struct vfs_cache_page *vfs_cache_hash[VFS_CACHE_HASH_BUCKETS];
static uint32_t vfs_cache_pages;
static uint32_t vfs_cache_limit;

/* A failed block transaction poisons the volume until device registration
 * replaces it. Never publish cached data or successful metadata results after
 * transport failure, even for a filesystem that performed several block I/Os.
 */
static int vfs_mount_status(const struct vfs_mount *mount, int status) {
  return DiskReady(mount->disk_number) ? status : VFS_ERROR_IO;
}

static bool vfs_normalize_drive(uint8_t drive, uint8_t *normalized) {
  if (drive >= 'a' && drive <= 'z') {
    drive -= 'a' - 'A';
  }
  if (drive < 'A' || drive > 'Z') {
    return false;
  }
  *normalized = drive;
  return true;
}

static bool vfs_node_equal(const vfs_node_id_t *left,
                           const vfs_node_id_t *right) {
  return memcmp(left, right, sizeof(*left)) == 0;
}

static uint32_t vfs_cache_bucket(struct vfs_mount *mount,
                                 const vfs_node_id_t *node, uint32_t index) {
  uint32_t hash = ((uint32_t)(uintptr_t)mount >> 4) ^ index;
  for (uint32_t i = 0; i < sizeof(node->value) / sizeof(node->value[0]); i++) {
    hash = (hash ^ node->value[i]) * 16777619u;
  }
  return hash & (VFS_CACHE_HASH_BUCKETS - 1);
}

static struct vfs_mount *vfs_find_active_mount(uint8_t drive) {
  for (uint32_t index = 0; index < VFS_MAX_MOUNTS; index++) {
    struct vfs_mount *mount = vfs_mounts[index];
    if (mount != NULL && mount->state == VFS_MOUNT_ACTIVE &&
        mount->drive == drive) {
      return mount;
    }
  }
  return NULL;
}

static int vfs_find_mount_slot(void) {
  for (uint32_t index = 0; index < VFS_MAX_MOUNTS; index++) {
    if (vfs_mounts[index] == NULL) {
      return (int)index;
    }
  }
  return -1;
}

static void vfs_mount_reference(struct vfs_mount *mount) {
  irq_state_t state = irq_save();
  mount->references++;
  irq_restore(state);
}

static void vfs_cache_unlink(struct vfs_cache_page *page) {
  if (page->previous != NULL) {
    page->previous->next = page->next;
  } else {
    vfs_cache_head = page->next;
  }
  if (page->next != NULL) {
    page->next->previous = page->previous;
  } else {
    vfs_cache_tail = page->previous;
  }
  page->previous = NULL;
  page->next = NULL;
}

static void vfs_cache_promote(struct vfs_cache_page *page) {
  if (page == vfs_cache_head) {
    return;
  }
  vfs_cache_unlink(page);
  page->next = vfs_cache_head;
  if (vfs_cache_head != NULL) {
    vfs_cache_head->previous = page;
  } else {
    vfs_cache_tail = page;
  }
  vfs_cache_head = page;
}

static void vfs_cache_free_page(struct vfs_cache_page *page) {
  struct vfs_cache_page **entry =
      &vfs_cache_hash[vfs_cache_bucket(page->mount, &page->node, page->index)];
  while (*entry != NULL && *entry != page) {
    entry = &(*entry)->hash_next;
  }
  if (*entry == page) {
    *entry = page->hash_next;
  }
  vfs_cache_unlink(page);
  free(page);
  vfs_cache_pages--;
}

static void vfs_cache_evict(void) {
  while (vfs_cache_pages > vfs_cache_limit && vfs_cache_tail != NULL) {
    vfs_cache_free_page(vfs_cache_tail);
  }
}

static void vfs_cache_insert(struct vfs_cache_page *page) {
  uint32_t bucket = vfs_cache_bucket(page->mount, &page->node, page->index);
  page->previous = NULL;
  page->hash_next = vfs_cache_hash[bucket];
  vfs_cache_hash[bucket] = page;
  page->next = vfs_cache_head;
  if (vfs_cache_head != NULL) {
    vfs_cache_head->previous = page;
  } else {
    vfs_cache_tail = page;
  }
  vfs_cache_head = page;
  vfs_cache_pages++;
  vfs_cache_evict();
}

static struct vfs_cache_page *
vfs_cache_find(struct vfs_mount *mount, const vfs_node_id_t *node,
               uint32_t index) {
  uint32_t bucket = vfs_cache_bucket(mount, node, index);
  for (struct vfs_cache_page *page = vfs_cache_hash[bucket]; page != NULL;
       page = page->hash_next) {
    if (page->mount == mount && page->index == index &&
        vfs_node_equal(&page->node, node)) {
      vfs_cache_promote(page);
      return page;
    }
  }
  return NULL;
}

static void vfs_cache_invalidate_node(struct vfs_mount *mount,
                                      const vfs_node_id_t *node) {
  struct vfs_cache_page *page = vfs_cache_head;
  while (page != NULL) {
    struct vfs_cache_page *next = page->next;
    if (page->mount == mount && vfs_node_equal(&page->node, node)) {
      vfs_cache_free_page(page);
    }
    page = next;
  }
}

static void vfs_cache_invalidate_mount(struct vfs_mount *mount) {
  struct vfs_cache_page *page = vfs_cache_head;
  while (page != NULL) {
    struct vfs_cache_page *next = page->next;
    if (page->mount == mount) {
      vfs_cache_free_page(page);
    }
    page = next;
  }
}

static struct vfs_cache_page *vfs_cache_load(struct vfs_dentry *dentry,
                                              uint32_t index) {
  struct vfs_cache_page *page = malloc(sizeof(*page));
  if (page == NULL) {
    return NULL;
  }
  uint32_t offset = index * VFS_CACHE_PAGE_SIZE;
  uint32_t length = dentry->node.size - offset;
  if (length > VFS_CACHE_PAGE_SIZE) {
    length = VFS_CACHE_PAGE_SIZE;
  }
  int read = vfs_mount_status(dentry->mount, dentry->mount->filesystem->read(
                                                 dentry->mount, &dentry->node,
                                                 offset, page->data, length));
  if (read < 0 || (uint32_t)read != length) {
    free(page);
    return NULL;
  }
  page->mount = dentry->mount;
  page->node = dentry->node.id;
  page->index = index;
  vfs_cache_insert(page);
  return page;
}

static bool vfs_cache_copy(struct vfs_dentry *dentry, uint32_t offset,
                           void *buffer, uint32_t length) {
  uint32_t first = offset / VFS_CACHE_PAGE_SIZE;
  uint32_t last = (offset + length - 1) / VFS_CACHE_PAGE_SIZE;
  for (uint32_t index = first; index <= last; index++) {
    if (vfs_cache_find(dentry->mount, &dentry->node.id, index) == NULL) {
      return false;
    }
  }

  uint32_t completed = 0;
  while (completed < length) {
    uint32_t position = offset + completed;
    uint32_t index = position / VFS_CACHE_PAGE_SIZE;
    uint32_t page_offset = position % VFS_CACHE_PAGE_SIZE;
    struct vfs_cache_page *page =
        vfs_cache_find(dentry->mount, &dentry->node.id, index);
    uint32_t chunk = VFS_CACHE_PAGE_SIZE - page_offset;
    if (chunk > length - completed) {
      chunk = length - completed;
    }
    memcpy((uint8_t *)buffer + completed, page->data + page_offset, chunk);
    completed += chunk;
  }
  return true;
}

static void vfs_cache_store_read(struct vfs_dentry *dentry, uint32_t offset,
                                 const void *buffer, uint32_t length) {
  uint64_t end = (uint64_t)offset + length;
  uint32_t first = offset / VFS_CACHE_PAGE_SIZE;
  uint32_t last = (uint32_t)((end - 1) / VFS_CACHE_PAGE_SIZE);
  uint32_t cacheable = 0;
  for (uint32_t index = first; index <= last; index++) {
    uint64_t page_start = (uint64_t)index * VFS_CACHE_PAGE_SIZE;
    uint64_t page_end = page_start + VFS_CACHE_PAGE_SIZE;
    if (page_end > dentry->node.size) {
      page_end = dentry->node.size;
    }
    if (page_start >= offset && page_end <= end) {
      cacheable++;
    }
  }
  if (cacheable == 0 || cacheable > vfs_cache_limit) {
    return;
  }

  for (uint32_t index = first; index <= last; index++) {
    uint64_t page_start = (uint64_t)index * VFS_CACHE_PAGE_SIZE;
    uint64_t page_end = page_start + VFS_CACHE_PAGE_SIZE;
    if (page_end > dentry->node.size) {
      page_end = dentry->node.size;
    }
    if (page_start < offset || page_end > end ||
        vfs_cache_find(dentry->mount, &dentry->node.id, index) != NULL) {
      continue;
    }
    struct vfs_cache_page *page = malloc(sizeof(*page));
    if (page == NULL) {
      return;
    }
    page->mount = dentry->mount;
    page->node = dentry->node.id;
    page->index = index;
    memcpy(page->data, (const uint8_t *)buffer + page_start - offset,
           page_end - page_start);
    vfs_cache_insert(page);
  }
}

static void vfs_destroy_mount(struct vfs_mount *mount) {
  uint32_t disk_index = mount->disk_number - 'A';
  vfs_cache_invalidate_mount(mount);
  if (mount->filesystem->unmount != NULL) {
    mount->filesystem->unmount(mount);
  }
  free(mount->root->name);
  free(mount->root);
  if (mount->retired_registered && disk_index < VFS_MAX_MOUNTS) {
    irq_state_t state = irq_save();
    if (vfs_disks[disk_index].retired_count != 0) {
      vfs_disks[disk_index].retired_count--;
    }
    irq_restore(state);
  }
  free(mount);
}

static void vfs_mount_release(struct vfs_mount *mount) {
  bool destroy = false;
  irq_state_t state = irq_save();
  if (mount->references == 0) {
    irq_restore(state);
    Panic_K("VFS mount reference underflow");
    return;
  }
  mount->references--;
  destroy = mount->references == 0 && mount->state == VFS_MOUNT_RETIRED;
  irq_restore(state);
  if (destroy) {
    vfs_destroy_mount(mount);
  }
}

static struct vfs_dentry *vfs_dentry_retain(struct vfs_dentry *dentry) {
  if (dentry == NULL) {
    return NULL;
  }
  dentry->references++;
  vfs_mount_reference(dentry->mount);
  return dentry;
}

static void vfs_dentry_release(struct vfs_dentry *dentry) {
  if (dentry == NULL) {
    return;
  }
  if (dentry->references <= (dentry->parent == NULL ? 1u : 0u)) {
    Panic_K("VFS dentry reference underflow");
    return;
  }
  dentry->references--;
  struct vfs_mount *mount = dentry->mount;
  if (dentry->parent != NULL && dentry->references == 0) {
    struct vfs_dentry *parent = dentry->parent;
    free(dentry->name);
    free(dentry);
    vfs_dentry_release(parent);
  }
  vfs_mount_release(mount);
}

static struct vfs_dentry *vfs_dentry_create(struct vfs_dentry *parent,
                                             const char *name,
                                             const vfs_node_t *node) {
  struct vfs_dentry *dentry = malloc(sizeof(*dentry));
  if (dentry == NULL) {
    return NULL;
  }
  memset(dentry, 0, sizeof(*dentry));
  size_t length = strlen(name);
  if (length >= INT_MAX) {
    free(dentry);
    return NULL;
  }
  dentry->name = malloc((int)length + 1);
  if (dentry->name == NULL) {
    free(dentry);
    return NULL;
  }
  memcpy(dentry->name, name, length + 1);
  dentry->mount = parent->mount;
  dentry->parent = vfs_dentry_retain(parent);
  dentry->node = *node;
  dentry->references = 1;
  vfs_mount_reference(dentry->mount);
  return dentry;
}

static int vfs_normalize_component(struct vfs_mount *mount,
                                   const char *component, size_t length,
                                   char normalized[255]) {
  if (length == 0 || length >= 255) {
    return VFS_ERROR_INVALID;
  }
  if (mount->filesystem->normalize_name != NULL) {
    return mount->filesystem->normalize_name(component, length, normalized,
                                              255);
  }
  memcpy(normalized, component, length);
  normalized[length] = '\0';
  return VFS_OK;
}

static struct vfs_dentry *vfs_root_for_drive(uint8_t drive) {
  uint8_t normalized;
  if (!vfs_normalize_drive(drive, &normalized)) {
    return NULL;
  }
  irq_state_t state = irq_save();
  struct vfs_mount *mount = vfs_find_active_mount(normalized);
  struct vfs_dentry *root = mount == NULL ? NULL : vfs_dentry_retain(mount->root);
  irq_restore(state);
  return root;
}

static int vfs_resolve(vfs_context_t *context, const char *path,
                       struct vfs_dentry **result) {
  if (context == NULL || context->cwd == NULL || path == NULL ||
      result == NULL) {
    return VFS_ERROR_INVALID;
  }
  const char *cursor = path;
  struct vfs_dentry *current;
  if (cursor[0] != '\0' && cursor[1] == ':') {
    current = vfs_root_for_drive((uint8_t)cursor[0]);
    if (current == NULL) {
      return VFS_ERROR_NO_ENTRY;
    }
    cursor += 2;
  } else if (*cursor == '/' || *cursor == '\\') {
    current = vfs_dentry_retain(context->cwd->mount->root);
  } else {
    current = vfs_dentry_retain(context->cwd);
  }

  if (vfs_mount_status(current->mount, VFS_OK) < 0) {
    vfs_dentry_release(current);
    return VFS_ERROR_IO;
  }
  while (*cursor != '\0') {
    while (*cursor == '/' || *cursor == '\\') {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }
    const char *start = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
      cursor++;
    }
    size_t length = cursor - start;
    if (length == 1 && start[0] == '.') {
      continue;
    }
    if (length == 2 && start[0] == '.' && start[1] == '.') {
      if (current->parent != NULL) {
        struct vfs_dentry *parent = vfs_dentry_retain(current->parent);
        vfs_dentry_release(current);
        current = parent;
      }
      continue;
    }
    if (current->node.type != VFS_NODE_DIRECTORY) {
      vfs_dentry_release(current);
      return VFS_ERROR_NOT_DIRECTORY;
    }
    char normalized[255];
    int status =
        vfs_normalize_component(current->mount, start, length, normalized);
    if (status < 0) {
      vfs_dentry_release(current);
      return status;
    }
    vfs_node_t node;
    lock(&current->mount->lock);
    status = vfs_mount_status(
        current->mount, current->mount->filesystem->lookup(
                            current->mount, &current->node, normalized, &node));
    unlock(&current->mount->lock);
    if (status < 0) {
      vfs_dentry_release(current);
      return status;
    }
    struct vfs_dentry *child = vfs_dentry_create(current, normalized, &node);
    vfs_dentry_release(current);
    if (child == NULL) {
      return VFS_ERROR_NO_MEMORY;
    }
    current = child;
  }
  *result = current;
  return VFS_OK;
}

static int vfs_resolve_parent(vfs_context_t *context, const char *path,
                              struct vfs_dentry **parent,
                              char normalized[255]) {
  if (path == NULL || *path == '\0') {
    return VFS_ERROR_INVALID;
  }
  *parent = NULL;
  size_t length = strlen(path);
  if (length >= INT_MAX) {
    return VFS_ERROR_OVERFLOW;
  }
  char *copy = malloc((int)length + 1);
  if (copy == NULL) {
    return VFS_ERROR_NO_MEMORY;
  }
  memcpy(copy, path, length + 1);
  for (size_t index = 0; index < length; index++) {
    if (copy[index] == '\\') {
      copy[index] = '/';
    }
  }
  while (length != 0 && copy[length - 1] == '/') {
    copy[--length] = '\0';
  }
  if (length == 0 || (length == 2 && copy[1] == ':')) {
    free(copy);
    return VFS_ERROR_INVALID;
  }
  char *leaf = copy;
  if (length > 2 && copy[1] == ':') {
    leaf = copy + 2;
  }
  for (char *scan = copy; *scan != '\0'; scan++) {
    if (*scan == '/') {
      leaf = scan + 1;
    }
  }
  if (*leaf == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
    free(copy);
    return VFS_ERROR_INVALID;
  }
  size_t leaf_length = strlen(leaf);
  if (leaf_length >= 255) {
    free(copy);
    return VFS_ERROR_INVALID;
  }
  char leaf_name[255];
  memcpy(leaf_name, leaf, leaf_length + 1);
  char *parent_path;
  if (leaf == copy) {
    parent_path = "";
  } else if (leaf == copy + 2 && copy[1] == ':') {
    copy[2] = '\0';
    parent_path = copy;
  } else {
    char *separator = leaf - 1;
    if (separator == copy) {
      separator[1] = '\0';
    } else {
      *separator = '\0';
    }
    parent_path = copy;
  }
  int status = vfs_resolve(context, parent_path, parent);
  if (status == VFS_OK && (*parent)->node.type != VFS_NODE_DIRECTORY) {
    status = VFS_ERROR_NOT_DIRECTORY;
  }
  if (status == VFS_OK) {
    status = vfs_normalize_component((*parent)->mount, leaf_name, leaf_length,
                                     normalized);
  }
  if (status < 0 && *parent != NULL) {
    vfs_dentry_release(*parent);
    *parent = NULL;
  }
  free(copy);
  return status;
}

static bool vfs_node_open(struct vfs_mount *mount,
                          const vfs_node_id_t *node) {
  for (struct vfs_handle *handle = vfs_handles; handle != NULL;
       handle = handle->next) {
    if (handle->dentry->mount == mount &&
        vfs_node_equal(&handle->dentry->node.id, node)) {
      return true;
    }
  }
  return false;
}

static bool vfs_node_is_cwd(struct vfs_mount *mount,
                            const vfs_node_id_t *node) {
  for (struct vfs_context *context = vfs_contexts; context != NULL;
       context = context->next) {
    for (struct vfs_dentry *dentry = context->cwd; dentry != NULL;
         dentry = dentry->parent) {
      if (dentry->mount == mount && vfs_node_equal(&dentry->node.id, node)) {
        return true;
      }
    }
  }
  return false;
}

static void vfs_update_open_sizes(struct vfs_mount *mount,
                                  const vfs_node_id_t *node, uint32_t size) {
  for (struct vfs_handle *handle = vfs_handles; handle != NULL;
       handle = handle->next) {
    if (handle->dentry->mount == mount &&
        vfs_node_equal(&handle->dentry->node.id, node)) {
      handle->dentry->node.size = size;
    }
  }
}

static void vfs_context_register(vfs_context_t *context) {
  context->next = vfs_contexts;
  if (vfs_contexts != NULL) {
    vfs_contexts->previous = context;
  }
  vfs_contexts = context;
}

static void vfs_context_unregister(vfs_context_t *context) {
  if (context->previous != NULL) {
    context->previous->next = context->next;
  } else {
    vfs_contexts = context->next;
  }
  if (context->next != NULL) {
    context->next->previous = context->previous;
  }
}

static bool vfs_fd_table_initialize(struct vfs_fd_table *table) {
  table->entries = malloc(VFS_INITIAL_FD_CAPACITY * sizeof(*table->entries));
  if (table->entries == NULL) {
    return false;
  }
  memset(table->entries, 0,
         VFS_INITIAL_FD_CAPACITY * sizeof(*table->entries));
  table->capacity = VFS_INITIAL_FD_CAPACITY;
  return true;
}

static void vfs_fd_table_destroy(struct vfs_fd_table *table) {
  if (table->entries == NULL) {
    return;
  }
  for (uint32_t descriptor = 3; descriptor < table->capacity; descriptor++) {
    if (table->entries[descriptor] != NULL) {
      vfs_close(table->entries[descriptor]);
    }
  }
  free(table->entries);
  table->entries = NULL;
  table->capacity = 0;
}

static bool vfs_fd_table_fork(struct vfs_fd_table *destination,
                              const struct vfs_fd_table *source) {
  destination->entries = malloc(source->capacity * sizeof(*source->entries));
  if (destination->entries == NULL) {
    return false;
  }
  memcpy(destination->entries, source->entries,
         source->capacity * sizeof(*source->entries));
  destination->capacity = source->capacity;
  for (uint32_t descriptor = 3; descriptor < destination->capacity;
       descriptor++) {
    if (destination->entries[descriptor] != NULL) {
      vfs_handle_retain(destination->entries[descriptor]);
    }
  }
  return true;
}

static vfs_context_t *vfs_context_allocate(struct vfs_dentry *cwd) {
  vfs_context_t *context = malloc(sizeof(*context));
  if (context == NULL) {
    return NULL;
  }
  memset(context, 0, sizeof(*context));
  if (!vfs_fd_table_initialize(&context->descriptors)) {
    free(context);
    return NULL;
  }
  context->cwd = vfs_dentry_retain(cwd);
  context->references = 1;
  vfs_context_register(context);
  return context;
}

void *vfs_mount_data(struct vfs_mount *mount) { return mount->data; }

void vfs_mount_set_data(struct vfs_mount *mount, void *data) {
  mount->data = data;
}

uint8_t vfs_mount_disk_number(const struct vfs_mount *mount) {
  return mount->disk_number;
}

uint8_t vfs_mount_drive(const struct vfs_mount *mount) { return mount->drive; }

void init_vfs(void) {
  memset(vfs_filesystems, 0, sizeof(vfs_filesystems));
  memset(vfs_mounts, 0, sizeof(vfs_mounts));
  memset(vfs_disks, 0, sizeof(vfs_disks));
  vfs_handles = NULL;
  vfs_contexts = NULL;
  vfs_cache_head = NULL;
  vfs_cache_tail = NULL;
  memset(vfs_cache_hash, 0, sizeof(vfs_cache_hash));
  vfs_cache_pages = 0;
  uint32_t cache_bytes = memsize / VFS_CACHE_MEMORY_DIVISOR;
  if (cache_bytes < VFS_CACHE_MIN_BYTES) {
    cache_bytes = VFS_CACHE_MIN_BYTES;
  }
  if (cache_bytes > VFS_CACHE_MAX_BYTES) {
    cache_bytes = VFS_CACHE_MAX_BYTES;
  }
  vfs_cache_limit = cache_bytes / sizeof(struct vfs_cache_page);
}

bool vfs_register_fs(const vfs_filesystem_t *filesystem) {
  if (filesystem == NULL || filesystem->name == NULL ||
      filesystem->check == NULL || filesystem->mount == NULL ||
      filesystem->unmount == NULL || filesystem->root == NULL ||
      filesystem->lookup == NULL || filesystem->read == NULL ||
      filesystem->iterate == NULL) {
    return false;
  }
  for (uint32_t index = 0; index < VFS_MAX_FILESYSTEMS; index++) {
    if (vfs_filesystems[index] == NULL) {
      vfs_filesystems[index] = filesystem;
      return true;
    }
  }
  return false;
}

bool vfs_mount_disk(uint8_t disk_number, uint8_t drive) {
  uint8_t normalized_disk;
  uint8_t normalized_drive;
  if (!vfs_normalize_drive(disk_number, &normalized_disk) ||
      !vfs_normalize_drive(drive, &normalized_drive)) {
    return false;
  }
  const vfs_filesystem_t *filesystem = NULL;
  for (uint32_t index = 0; index < VFS_MAX_FILESYSTEMS; index++) {
    if (vfs_filesystems[index] != NULL &&
        vfs_filesystems[index]->check(normalized_disk)) {
      filesystem = vfs_filesystems[index];
      break;
    }
  }
  if (filesystem == NULL) {
    return false;
  }
  struct vfs_mount *mount = malloc(sizeof(*mount));
  if (mount == NULL) {
    return false;
  }
  memset(mount, 0, sizeof(*mount));
  mount->filesystem = filesystem;
  mount->disk_number = normalized_disk;
  mount->drive = normalized_drive;
  mount->state = VFS_MOUNT_INITIALIZING;
  lock_init(&mount->lock);

  irq_state_t state = irq_save();
  uint32_t disk_index = normalized_disk - 'A';
  int slot = -1;
  if (!vfs_disks[disk_index].formatting) {
    bool conflict = false;
    for (uint32_t index = 0; index < VFS_MAX_MOUNTS; index++) {
      if (vfs_mounts[index] != NULL &&
          (vfs_mounts[index]->disk_number == normalized_disk ||
           vfs_mounts[index]->drive == normalized_drive)) {
        conflict = true;
        break;
      }
    }
    if (!conflict) {
      slot = vfs_find_mount_slot();
    }
  }
  if (slot >= 0) {
    vfs_mounts[slot] = mount;
  }
  irq_restore(state);
  if (slot < 0) {
    free(mount);
    return false;
  }

  if (vfs_mount_status(mount, filesystem->mount(mount)) < 0) {
    state = irq_save();
    if (vfs_mounts[slot] == mount) {
      vfs_mounts[slot] = NULL;
    }
    irq_restore(state);
    filesystem->unmount(mount);
    free(mount);
    return false;
  }
  mount->root = malloc(sizeof(*mount->root));
  if (mount->root == NULL) {
    filesystem->unmount(mount);
    state = irq_save();
    vfs_mounts[slot] = NULL;
    irq_restore(state);
    free(mount);
    return false;
  }
  memset(mount->root, 0, sizeof(*mount->root));
  mount->root->name = malloc(1);
  if (mount->root->name == NULL ||
      vfs_mount_status(mount, filesystem->root(mount, &mount->root->node)) <
          0) {
    free(mount->root->name);
    free(mount->root);
    filesystem->unmount(mount);
    state = irq_save();
    vfs_mounts[slot] = NULL;
    irq_restore(state);
    free(mount);
    return false;
  }
  mount->root->name[0] = '\0';
  mount->root->mount = mount;
  mount->root->references = 1;
  state = irq_save();
  mount->state = VFS_MOUNT_ACTIVE;
  irq_restore(state);
  return true;
}

bool vfs_mount_all_disks(void) {
  bool mounted = false;
  for (char drive = first_vdisk(); drive != 0; drive = next_vdisk(drive)) {
    if (vfs_check_mount(drive) || vfs_mount_disk(drive, drive)) {
      mounted = true;
    }
  }
  return mounted;
}

bool vfs_unmount_disk(uint8_t drive) {
  uint8_t normalized;
  if (!vfs_normalize_drive(drive, &normalized)) {
    return false;
  }
  irq_state_t state = irq_save();
  for (uint32_t index = 0; index < VFS_MAX_MOUNTS; index++) {
    struct vfs_mount *mount = vfs_mounts[index];
    if (mount != NULL && mount->state == VFS_MOUNT_ACTIVE &&
        mount->drive == normalized) {
      vfs_mounts[index] = NULL;
      mount->state = VFS_MOUNT_RETIRED;
      mount->retired_registered = true;
      vfs_disks[mount->disk_number - 'A'].retired_count++;
      bool destroy = mount->references == 0;
      irq_restore(state);
      if (destroy) {
        vfs_destroy_mount(mount);
      }
      return true;
    }
  }
  irq_restore(state);
  return false;
}

bool vfs_disk_reusable(uint8_t disk) {
  if (disk < 'A' || disk > 'Z' || vfs_disks[disk - 'A'].formatting ||
      vfs_disks[disk - 'A'].retired_count != 0) {
    return false;
  }
  for (unsigned i = 0; i < VFS_MAX_MOUNTS; i++) {
    if (vfs_mounts[i] != NULL && vfs_mounts[i]->disk_number == disk) {
      return false;
    }
  }
  return true;
}

void vfs_disk_removed(uint8_t disk) {
  for (unsigned i = 0; i < VFS_MAX_MOUNTS; i++) {
    struct vfs_mount *mount = vfs_mounts[i];
    if (mount != NULL && mount->disk_number == disk &&
        mount->state == VFS_MOUNT_ACTIVE) {
      vfs_cache_invalidate_mount(mount);
      vfs_unmount_disk(mount->drive);
    }
  }
}

bool vfs_check_mount(uint8_t drive) {
  uint8_t normalized;
  if (!vfs_normalize_drive(drive, &normalized)) {
    return false;
  }
  irq_state_t state = irq_save();
  bool mounted = vfs_find_active_mount(normalized) != NULL;
  irq_restore(state);
  return mounted;
}

int vfs_format(uint8_t disk_number, const char *filesystem_name) {
  uint8_t normalized;
  if (!vfs_normalize_drive(disk_number, &normalized) ||
      filesystem_name == NULL) {
    return VFS_ERROR_INVALID;
  }
  if (!disk_writable(normalized)) {
    return VFS_ERROR_READ_ONLY;
  }
  const vfs_filesystem_t *filesystem = NULL;
  for (uint32_t index = 0; index < VFS_MAX_FILESYSTEMS; index++) {
    if (vfs_filesystems[index] != NULL &&
        strcmp(vfs_filesystems[index]->name, filesystem_name) == 0) {
      filesystem = vfs_filesystems[index];
      break;
    }
  }
  if (filesystem == NULL || filesystem->format == NULL) {
    return VFS_ERROR_NOT_SUPPORTED;
  }
  uint32_t disk_index = normalized - 'A';
  struct vfs_mount *unused = NULL;
  irq_state_t state = irq_save();
  if (vfs_disks[disk_index].formatting ||
      vfs_disks[disk_index].retired_count != 0) {
    irq_restore(state);
    return VFS_ERROR_BUSY;
  }
  for (uint32_t index = 0; index < VFS_MAX_MOUNTS; index++) {
    struct vfs_mount *mount = vfs_mounts[index];
    if (mount != NULL && mount->disk_number == normalized) {
      if (mount->state != VFS_MOUNT_ACTIVE || mount->references != 0 ||
          unused != NULL) {
        irq_restore(state);
        return VFS_ERROR_BUSY;
      }
      unused = mount;
      vfs_mounts[index] = NULL;
      mount->state = VFS_MOUNT_RETIRED;
    }
  }
  vfs_disks[disk_index].formatting = true;
  irq_restore(state);
  if (unused != NULL) {
    vfs_destroy_mount(unused);
  }
  int result = filesystem->format(normalized) && DiskReady(normalized)
                   ? VFS_OK
                   : VFS_ERROR_IO;
  state = irq_save();
  vfs_disks[disk_index].formatting = false;
  irq_restore(state);
  return result;
}

vfs_context_t *vfs_context_create(uint8_t drive) {
  struct vfs_dentry *root = vfs_root_for_drive(drive);
  if (root == NULL) {
    return NULL;
  }
  vfs_context_t *context = vfs_context_allocate(root);
  vfs_dentry_release(root);
  return context;
}

vfs_context_t *vfs_context_clone_cwd(const vfs_context_t *source) {
  return source == NULL ? NULL : vfs_context_allocate(source->cwd);
}

vfs_context_t *vfs_context_fork(const vfs_context_t *source) {
  if (source == NULL) {
    return NULL;
  }
  vfs_context_t *context = malloc(sizeof(*context));
  if (context == NULL) {
    return NULL;
  }
  memset(context, 0, sizeof(*context));
  if (!vfs_fd_table_fork(&context->descriptors, &source->descriptors)) {
    free(context);
    return NULL;
  }
  context->cwd = vfs_dentry_retain(source->cwd);
  context->references = 1;
  vfs_context_register(context);
  return context;
}

void vfs_context_retain(vfs_context_t *context) {
  if (context != NULL) {
    context->references++;
  }
}

void vfs_context_release(vfs_context_t *context) {
  if (context == NULL) {
    return;
  }
  if (context->references == 0) {
    Panic_K("VFS context reference underflow");
    return;
  }
  if (--context->references != 0) {
    return;
  }
  vfs_context_unregister(context);
  vfs_fd_table_destroy(&context->descriptors);
  vfs_dentry_release(context->cwd);
  free(context);
}

bool vfs_context_transfer_cwd(vfs_context_t *source,
                              vfs_context_t *destination) {
  if (source == NULL || destination == NULL || source->cwd == NULL) {
    return false;
  }
  struct vfs_dentry *replacement = vfs_dentry_retain(source->cwd);
  struct vfs_dentry *previous = destination->cwd;
  destination->cwd = replacement;
  vfs_dentry_release(previous);
  return true;
}

int vfs_context_change_drive(vfs_context_t *context, uint8_t drive) {
  if (context == NULL) {
    return VFS_ERROR_INVALID;
  }
  struct vfs_dentry *root = vfs_root_for_drive(drive);
  if (root == NULL) {
    return VFS_ERROR_NO_ENTRY;
  }
  struct vfs_dentry *previous = context->cwd;
  context->cwd = root;
  vfs_dentry_release(previous);
  return VFS_OK;
}

int vfs_context_chdir(vfs_context_t *context, const char *path) {
  struct vfs_dentry *directory = NULL;
  int status = vfs_resolve(context, path, &directory);
  if (status < 0) {
    return status;
  }
  if (directory->node.type != VFS_NODE_DIRECTORY) {
    vfs_dentry_release(directory);
    return VFS_ERROR_NOT_DIRECTORY;
  }
  struct vfs_dentry *previous = context->cwd;
  context->cwd = directory;
  vfs_dentry_release(previous);
  return VFS_OK;
}

int vfs_context_getcwd(vfs_context_t *context, char *buffer,
                       size_t capacity) {
  if (context == NULL || context->cwd == NULL) {
    return VFS_ERROR_INVALID;
  }
  size_t length = 0;
  for (struct vfs_dentry *dentry = context->cwd; dentry->parent != NULL;
       dentry = dentry->parent) {
    length += strlen(dentry->name) + 1;
  }
  if (length == 0) {
    length = 1;
  }
  if (buffer == NULL) {
    return length;
  }
  if (capacity <= length) {
    return VFS_ERROR_OVERFLOW;
  }
  buffer[length] = '\0';
  size_t cursor = length;
  for (struct vfs_dentry *dentry = context->cwd; dentry->parent != NULL;
       dentry = dentry->parent) {
    size_t name_length = strlen(dentry->name);
    cursor -= name_length;
    memcpy(buffer + cursor, dentry->name, name_length);
    buffer[--cursor] = '/';
  }
  if (cursor != 0) {
    buffer[0] = '/';
  }
  return length;
}

uint8_t vfs_context_drive(const vfs_context_t *context) {
  return context == NULL || context->cwd == NULL ? 0 : context->cwd->mount->drive;
}

int vfs_open(vfs_context_t *context, const char *path, uint32_t flags,
             vfs_handle_t **handle_out) {
  if (context == NULL || path == NULL || *path == '\0' || handle_out == NULL ||
      (flags & (VFS_OPEN_READ | VFS_OPEN_WRITE)) == 0) {
    return VFS_ERROR_INVALID;
  }
  struct vfs_dentry *dentry = NULL;
  int status = vfs_resolve(context, path, &dentry);
  if (status == VFS_ERROR_NO_ENTRY && (flags & VFS_OPEN_CREATE) != 0) {
    struct vfs_dentry *parent = NULL;
    char name[255];
    status = vfs_resolve_parent(context, path, &parent, name);
    if (status < 0) {
      return status;
    }
    if (parent->mount->filesystem->create == NULL) {
      vfs_dentry_release(parent);
      return VFS_ERROR_READ_ONLY;
    }
    vfs_node_t node;
    lock(&parent->mount->lock);
    status = vfs_mount_status(
        parent->mount,
        (disk_writable(parent->mount->disk_number)
             ? parent->mount->filesystem->create(parent->mount, &parent->node,
                                                 name, VFS_NODE_FILE, &node)
             : VFS_ERROR_READ_ONLY));
    unlock(&parent->mount->lock);
    if (status == VFS_OK) {
      dentry = vfs_dentry_create(parent, name, &node);
      if (dentry == NULL) {
        status = VFS_ERROR_NO_MEMORY;
      }
    }
    vfs_dentry_release(parent);
  } else if (status == VFS_OK && (flags & VFS_OPEN_CREATE) != 0 &&
             (flags & VFS_OPEN_EXCLUSIVE) != 0) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_EXISTS;
  }
  if (status < 0) {
    return status;
  }
  if (dentry->node.type != VFS_NODE_FILE) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_IS_DIRECTORY;
  }
  if ((flags & VFS_OPEN_WRITE) != 0 &&
      (dentry->mount->filesystem->write == NULL ||
       !disk_writable(dentry->mount->disk_number))) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_READ_ONLY;
  }
  if ((flags & VFS_OPEN_TRUNCATE) != 0) {
    if ((flags & VFS_OPEN_WRITE) == 0 ||
        dentry->mount->filesystem->truncate == NULL) {
      vfs_dentry_release(dentry);
      return VFS_ERROR_INVALID;
    }
    lock(&dentry->mount->lock);
    status = vfs_mount_status(dentry->mount,
                              (disk_writable(dentry->mount->disk_number)
                                   ? dentry->mount->filesystem->truncate(
                                         dentry->mount, &dentry->node, 0)
                                   : VFS_ERROR_READ_ONLY));
    unlock(&dentry->mount->lock);
    if (status < 0) {
      vfs_dentry_release(dentry);
      return status;
    }
    dentry->node.size = 0;
    vfs_update_open_sizes(dentry->mount, &dentry->node.id, 0);
    vfs_cache_invalidate_node(dentry->mount, &dentry->node.id);
  }
  vfs_handle_t *handle = malloc(sizeof(*handle));
  if (handle == NULL) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_NO_MEMORY;
  }
  memset(handle, 0, sizeof(*handle));
  handle->dentry = dentry;
  handle->flags = flags;
  handle->offset = (flags & VFS_OPEN_APPEND) != 0 ? dentry->node.size : 0;
  handle->references = 1;
  handle->next = vfs_handles;
  if (vfs_handles != NULL) {
    vfs_handles->previous = handle;
  }
  vfs_handles = handle;
  *handle_out = handle;
  return VFS_OK;
}

void vfs_handle_retain(vfs_handle_t *handle) {
  if (handle != NULL) {
    handle->references++;
  }
}

int vfs_close(vfs_handle_t *handle) {
  if (handle == NULL || handle->references == 0) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (--handle->references != 0) {
    return VFS_OK;
  }
  if (handle->previous != NULL) {
    handle->previous->next = handle->next;
  } else {
    vfs_handles = handle->next;
  }
  if (handle->next != NULL) {
    handle->next->previous = handle->previous;
  }
  vfs_dentry_release(handle->dentry);
  free(handle);
  return VFS_OK;
}

int vfs_read(vfs_handle_t *handle, void *buffer, uint32_t length) {
  if (handle == NULL || (length != 0 && buffer == NULL) ||
      (handle->flags & VFS_OPEN_READ) == 0) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (length == 0 || handle->offset >= handle->dentry->node.size) {
    return 0;
  }
  uint32_t available = handle->dentry->node.size - handle->offset;
  if (length > available) {
    length = available;
  }
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  if (vfs_mount_status(mount, VFS_OK) < 0) {
    unlock(&mount->lock);
    return VFS_ERROR_IO;
  }
  if (length >= VFS_CACHE_BULK_READ_SIZE) {
    uint32_t read_offset = handle->offset;
    if (vfs_cache_copy(handle->dentry, read_offset, buffer, length)) {
      handle->offset += length;
      unlock(&mount->lock);
      return length;
    }
    int read = vfs_mount_status(
        mount, mount->filesystem->read(mount, &handle->dentry->node,
                                       handle->offset, buffer, length));
    if (read > 0 && (uint32_t)read <= length) {
      handle->offset += (uint32_t)read;
      vfs_cache_store_read(handle->dentry, read_offset, buffer,
                           (uint32_t)read);
    } else if (read > 0) {
      read = VFS_ERROR_IO;
    }
    unlock(&mount->lock);
    return read;
  }
  uint32_t completed = 0;
  while (completed < length) {
    uint32_t page_index = handle->offset / VFS_CACHE_PAGE_SIZE;
    uint32_t page_offset = handle->offset % VFS_CACHE_PAGE_SIZE;
    struct vfs_cache_page *page =
        vfs_cache_find(mount, &handle->dentry->node.id, page_index);
    if (page == NULL) {
      page = vfs_cache_load(handle->dentry, page_index);
      if (page == NULL) {
        unlock(&mount->lock);
        return completed == 0 ? VFS_ERROR_IO : (int)completed;
      }
    }
    uint32_t chunk = VFS_CACHE_PAGE_SIZE - page_offset;
    if (chunk > length - completed) {
      chunk = length - completed;
    }
    memcpy((uint8_t *)buffer + completed, page->data + page_offset, chunk);
    completed += chunk;
    handle->offset += chunk;
  }
  unlock(&mount->lock);
  return completed;
}

int vfs_write(vfs_handle_t *handle, const void *buffer, uint32_t length) {
  if (handle == NULL || (length != 0 && buffer == NULL) ||
      (handle->flags & VFS_OPEN_WRITE) == 0) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (length == 0) {
    return 0;
  }
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  if (vfs_mount_status(mount, VFS_OK) < 0) {
    unlock(&mount->lock);
    return VFS_ERROR_IO;
  }
  if ((handle->flags & VFS_OPEN_APPEND) != 0) {
    handle->offset = handle->dentry->node.size;
  }
  if (handle->offset > UINT_MAX - length) {
    unlock(&mount->lock);
    return VFS_ERROR_OVERFLOW;
  }
  uint32_t previous_size = handle->dentry->node.size;
  int written = vfs_mount_status(
      mount, (disk_writable(mount->disk_number)
                  ? mount->filesystem->write(mount, &handle->dentry->node,
                                             handle->offset, buffer, length)
                  : VFS_ERROR_READ_ONLY));
  if (written > 0) {
    uint32_t write_offset = handle->offset;
    uint32_t write_end = write_offset + (uint32_t)written;
    if (write_end > handle->dentry->node.size) {
      handle->dentry->node.size = write_end;
    }
    for (uint32_t copied = 0; copied < (uint32_t)written;) {
      uint32_t page_index = (write_offset + copied) / VFS_CACHE_PAGE_SIZE;
      uint32_t page_offset = (write_offset + copied) % VFS_CACHE_PAGE_SIZE;
      struct vfs_cache_page *page =
          vfs_cache_find(mount, &handle->dentry->node.id, page_index);
      uint32_t chunk = VFS_CACHE_PAGE_SIZE - page_offset;
      if (chunk > (uint32_t)written - copied) {
        chunk = (uint32_t)written - copied;
      }
      if (page != NULL) {
        uint32_t page_start = page_index * VFS_CACHE_PAGE_SIZE;
        if (previous_size > page_start && previous_size < write_offset + copied) {
          memset(page->data + (previous_size - page_start), 0,
                 write_offset + copied - previous_size);
        }
        memcpy(page->data + page_offset, (const uint8_t *)buffer + copied,
               chunk);
      }
      copied += chunk;
    }
    handle->offset = write_end;
    vfs_update_open_sizes(mount, &handle->dentry->node.id,
                          handle->dentry->node.size);
  }
  unlock(&mount->lock);
  return written;
}

int vfs_seek(vfs_handle_t *handle, int32_t offset, int whence) {
  if (handle == NULL) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  int64_t base;
  if (whence == SEEK_SET) {
    base = 0;
  } else if (whence == SEEK_CUR) {
    base = handle->offset;
  } else if (whence == SEEK_END) {
    base = handle->dentry->node.size;
  } else {
    return VFS_ERROR_INVALID;
  }
  int64_t position = base + offset;
  if (position < 0 || position > INT_MAX) {
    return VFS_ERROR_INVALID;
  }
  handle->offset = position;
  return (int)position;
}

int vfs_sync(vfs_handle_t *handle) {
  if (handle == NULL) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  int status = vfs_mount_status(mount, VFS_OK);
  if (status == VFS_OK && mount->filesystem->sync != NULL) {
    status = vfs_mount_status(mount, mount->filesystem->sync(mount));
  }
  if (status == VFS_OK && !disk_sync(mount->disk_number)) {
    status = VFS_ERROR_IO;
  }
  unlock(&mount->lock);
  return status;
}

int vfs_fstat(vfs_handle_t *handle, vfs_stat_t *status) {
  if (handle == NULL || status == NULL) {
    return VFS_ERROR_INVALID;
  }
  if (vfs_mount_status(handle->dentry->mount, VFS_OK) < 0) {
    return VFS_ERROR_IO;
  }
  status->type = handle->dentry->node.type;
  status->attributes = handle->dentry->node.attributes;
  status->size = handle->dentry->node.size;
  status->modified_time = handle->dentry->node.modified_time;
  return VFS_OK;
}

int vfs_stat(vfs_context_t *context, const char *path, vfs_stat_t *status) {
  if (status == NULL) {
    return VFS_ERROR_INVALID;
  }
  struct vfs_dentry *dentry = NULL;
  int result = vfs_resolve(context, path, &dentry);
  if (result < 0) {
    return result;
  }
  status->type = dentry->node.type;
  status->attributes = dentry->node.attributes;
  status->size = dentry->node.size;
  status->modified_time = dentry->node.modified_time;
  vfs_dentry_release(dentry);
  return VFS_OK;
}

int vfs_list_directory(vfs_context_t *context, const char *path,
                       vfs_file *entries, size_t capacity, size_t *count) {
  if (context == NULL || path == NULL || count == NULL) {
    return VFS_ERROR_INVALID;
  }
  struct vfs_dentry *directory = NULL;
  int status = vfs_resolve(context, path, &directory);
  if (status < 0) {
    return status;
  }
  if (directory->node.type != VFS_NODE_DIRECTORY) {
    vfs_dentry_release(directory);
    return VFS_ERROR_NOT_DIRECTORY;
  }
  size_t total = 0;
  lock(&directory->mount->lock);
  for (;;) {
    vfs_dir_entry_t entry;
    int iterated = vfs_mount_status(
        directory->mount,
        directory->mount->filesystem->iterate(directory->mount,
                                              &directory->node, total, &entry));
    if (iterated < 0) {
      status = iterated;
      break;
    }
    if (iterated == 0) {
      break;
    }
    if (entries != NULL && total < capacity) {
      memset(&entries[total], 0, sizeof(entries[total]));
      strcpy(entries[total].name, entry.name);
      entries[total].type = entry.node.type == VFS_NODE_DIRECTORY
                                ? DIR
                                : entry.node.attributes;
      entries[total].size = entry.node.size;
    }
    total++;
  }
  unlock(&directory->mount->lock);
  vfs_dentry_release(directory);
  if (status < 0) {
    return status;
  }
  *count = total;
  return entries != NULL && capacity < total ? VFS_ERROR_OVERFLOW : VFS_OK;
}

static int vfs_create_directory_entry(vfs_context_t *context,
                                      const char *path,
                                      vfs_node_type_t type) {
  struct vfs_dentry *parent = NULL;
  char name[255];
  int status = vfs_resolve_parent(context, path, &parent, name);
  if (status < 0) {
    return status;
  }
  if (parent->mount->filesystem->create == NULL) {
    vfs_dentry_release(parent);
    return VFS_ERROR_READ_ONLY;
  }
  vfs_node_t ignored;
  lock(&parent->mount->lock);
  status = vfs_mount_status(
      parent->mount,
      (disk_writable(parent->mount->disk_number)
           ? parent->mount->filesystem->create(parent->mount, &parent->node,
                                               name, type, &ignored)
           : VFS_ERROR_READ_ONLY));
  unlock(&parent->mount->lock);
  vfs_dentry_release(parent);
  return status;
}

int vfs_mkdir(vfs_context_t *context, const char *path) {
  return vfs_create_directory_entry(context, path, VFS_NODE_DIRECTORY);
}

static int vfs_remove_entry(vfs_context_t *context, const char *path,
                            vfs_node_type_t type) {
  struct vfs_dentry *target = NULL;
  int status = vfs_resolve(context, path, &target);
  if (status < 0) {
    return status;
  }
  if (target->node.type != type) {
    vfs_dentry_release(target);
    return type == VFS_NODE_DIRECTORY ? VFS_ERROR_NOT_DIRECTORY
                                      : VFS_ERROR_IS_DIRECTORY;
  }
  if (vfs_node_open(target->mount, &target->node.id) ||
      (type == VFS_NODE_DIRECTORY &&
       vfs_node_is_cwd(target->mount, &target->node.id))) {
    vfs_dentry_release(target);
    return VFS_ERROR_BUSY;
  }
  struct vfs_dentry *parent = NULL;
  char name[255];
  status = vfs_resolve_parent(context, path, &parent, name);
  if (status == VFS_OK && parent->mount != target->mount) {
    status = VFS_ERROR_INVALID;
  }
  if (status == VFS_OK && parent->mount->filesystem->remove == NULL) {
    status = VFS_ERROR_READ_ONLY;
  }
  if (status == VFS_OK) {
    lock(&parent->mount->lock);
    status = vfs_mount_status(
        parent->mount, (disk_writable(parent->mount->disk_number)
                            ? parent->mount->filesystem->remove(
                                  parent->mount, &parent->node, name, type)
                            : VFS_ERROR_READ_ONLY));
    unlock(&parent->mount->lock);
  }
  if (status == VFS_OK) {
    vfs_cache_invalidate_node(target->mount, &target->node.id);
  }
  vfs_dentry_release(parent);
  vfs_dentry_release(target);
  return status;
}

int vfs_unlink(vfs_context_t *context, const char *path) {
  return vfs_remove_entry(context, path, VFS_NODE_FILE);
}

int vfs_rmdir(vfs_context_t *context, const char *path) {
  return vfs_remove_entry(context, path, VFS_NODE_DIRECTORY);
}

int vfs_rename(vfs_context_t *context, const char *source,
               const char *destination) {
  struct vfs_dentry *target = NULL;
  int status = vfs_resolve(context, source, &target);
  if (status < 0) {
    return status;
  }
  if (vfs_node_open(target->mount, &target->node.id) ||
      (target->node.type == VFS_NODE_DIRECTORY &&
       vfs_node_is_cwd(target->mount, &target->node.id))) {
    vfs_dentry_release(target);
    return VFS_ERROR_BUSY;
  }
  struct vfs_dentry *source_parent = NULL;
  struct vfs_dentry *destination_parent = NULL;
  char source_name[255];
  char destination_name[255];
  status = vfs_resolve_parent(context, source, &source_parent, source_name);
  if (status == VFS_OK) {
    status = vfs_resolve_parent(context, destination, &destination_parent,
                                destination_name);
  }
  if (status == VFS_OK && source_parent->mount != destination_parent->mount) {
    status = VFS_ERROR_NOT_SUPPORTED;
  }
  if (status == VFS_OK && source_parent->mount->filesystem->rename == NULL) {
    status = VFS_ERROR_READ_ONLY;
  }
  if (status == VFS_OK) {
    lock(&source_parent->mount->lock);
    status = vfs_mount_status(
        source_parent->mount,
        (disk_writable(source_parent->mount->disk_number)
             ? source_parent->mount->filesystem->rename(
                   source_parent->mount, &source_parent->node, source_name,
                   &destination_parent->node, destination_name)
             : VFS_ERROR_READ_ONLY));
    unlock(&source_parent->mount->lock);
  }
  if (status == VFS_OK) {
    vfs_cache_invalidate_node(target->mount, &target->node.id);
  }
  vfs_dentry_release(destination_parent);
  vfs_dentry_release(source_parent);
  vfs_dentry_release(target);
  return status;
}

static vfs_handle_t *vfs_fd_get(vfs_context_t *context, int descriptor) {
  if (context == NULL || descriptor < 3 ||
      (uint32_t)descriptor >= context->descriptors.capacity) {
    return NULL;
  }
  return context->descriptors.entries[descriptor];
}

int vfs_fd_open(vfs_context_t *context, const char *path, uint32_t flags) {
  if (context == NULL) {
    return VFS_ERROR_INVALID;
  }
  uint32_t descriptor;
  for (descriptor = 3; descriptor < context->descriptors.capacity;
       descriptor++) {
    if (context->descriptors.entries[descriptor] == NULL) {
      break;
    }
  }
  if (descriptor == context->descriptors.capacity) {
    if (context->descriptors.capacity > UINT_MAX / 2 / sizeof(vfs_handle_t *)) {
      return VFS_ERROR_TOO_MANY_FILES;
    }
    uint32_t capacity = context->descriptors.capacity * 2;
    vfs_handle_t **entries =
        realloc(context->descriptors.entries, capacity * sizeof(*entries));
    if (entries == NULL) {
      return VFS_ERROR_NO_MEMORY;
    }
    memset(entries + context->descriptors.capacity, 0,
           (capacity - context->descriptors.capacity) * sizeof(*entries));
    context->descriptors.entries = entries;
    context->descriptors.capacity = capacity;
  }
  vfs_handle_t *handle = NULL;
  int status = vfs_open(context, path, flags, &handle);
  if (status < 0) {
    return status;
  }
  context->descriptors.entries[descriptor] = handle;
  return descriptor;
}

int vfs_fd_close(vfs_context_t *context, int descriptor) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (handle == NULL) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  context->descriptors.entries[descriptor] = NULL;
  return vfs_close(handle);
}

int vfs_fd_read(vfs_context_t *context, int descriptor, void *buffer,
                uint32_t length) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  return handle == NULL ? VFS_ERROR_BAD_DESCRIPTOR
                        : vfs_read(handle, buffer, length);
}

int vfs_fd_write(vfs_context_t *context, int descriptor, const void *buffer,
                 uint32_t length) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  return handle == NULL ? VFS_ERROR_BAD_DESCRIPTOR
                        : vfs_write(handle, buffer, length);
}

int vfs_fd_seek(vfs_context_t *context, int descriptor, int32_t offset,
                int whence) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  return handle == NULL ? VFS_ERROR_BAD_DESCRIPTOR
                        : vfs_seek(handle, offset, whence);
}

int vfs_fd_sync(vfs_context_t *context, int descriptor) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  return handle == NULL ? VFS_ERROR_BAD_DESCRIPTOR : vfs_sync(handle);
}

int vfs_fd_stat(vfs_context_t *context, int descriptor, vfs_stat_t *status) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  return handle == NULL ? VFS_ERROR_BAD_DESCRIPTOR : vfs_fstat(handle, status);
}
