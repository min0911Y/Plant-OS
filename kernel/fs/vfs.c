#include <dos.h>
#include <fcntl.h>
#include <io_poll.h>
#include <irq.h>
#include <limits.h>
#include <stdint.h>
#include <user_vm.h>

#define VFS_MAX_FILESYSTEMS 26
#define VFS_MAX_MOUNTS 26
#define VFS_CACHE_PAGE_SIZE 4096u
#define VFS_CACHE_READ_PAGES 32u
#define VFS_CACHE_HASH_BUCKETS 4096u
#define VFS_CACHE_MEMORY_DIVISOR 8u
#define VFS_CACHE_MIN_BYTES (256u * 1024u)
#define VFS_CACHE_MAX_BYTES (256u * 1024u * 1024u)
#define VFS_INITIAL_FD_CAPACITY 16u
#define VFS_IDENTITY_BUCKETS 64u

typedef struct vfs_identity {
  struct vfs_identity *next;
  vfs_node_id_t node;
  uint64_t number;
} vfs_identity_t;
static uint64_t vfs_identity_sequence;

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
  uint64_t device_identity;
  vfs_identity_t *identities[VFS_IDENTITY_BUCKETS];
};

enum { VFS_PIPE_CAPACITY = 65536, VFS_PIPE_ATOMIC = 4096 };
typedef struct {
  io_poll_queue_t readers, writers;
  uint64_t identity;
  uint32_t head, used;
  bool read_open, write_open;
  unsigned char data[VFS_PIPE_CAPACITY];
} vfs_pipe_t;

struct vfs_handle {
  struct vfs_handle *previous;
  struct vfs_handle *next;
  struct vfs_dentry *dentry;
  vfs_pipe_t *pipe;
  uint32_t offset;
  uint32_t flags;
  uint32_t references;
};

typedef struct {
  vfs_handle_t *handle;
  bool close_on_exec;
} vfs_descriptor_t;

struct vfs_fd_table {
  vfs_descriptor_t *entries;
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
  uint32_t mappings, writers;
  uint8_t *data;
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
/* Only unpinned pages enter the reclaimable LRU; mappings remain in the hash.
 */
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
  vfs_cache_pages--;
}

static void vfs_cache_link(struct vfs_cache_page *page) {
  page->previous = NULL;
  page->next = vfs_cache_head;
  if (vfs_cache_head)
    vfs_cache_head->previous = page;
  else
    vfs_cache_tail = page;
  vfs_cache_head = page;
  vfs_cache_pages++;
}

static void vfs_cache_promote(struct vfs_cache_page *page) {
  if (page->mappings || page == vfs_cache_head)
    return;
  vfs_cache_unlink(page);
  vfs_cache_link(page);
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
  page_free(page->data, VFS_CACHE_PAGE_SIZE);
  free(page);
}

static void vfs_cache_evict(void) {
  while (vfs_cache_pages > vfs_cache_limit)
    vfs_cache_free_page(vfs_cache_tail);
}

static void vfs_cache_insert(struct vfs_cache_page *page) {
  uint32_t bucket = vfs_cache_bucket(page->mount, &page->node, page->index);
  page->hash_next = vfs_cache_hash[bucket];
  vfs_cache_hash[bucket] = page;
  vfs_cache_link(page);
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

static bool vfs_cache_mapped(struct vfs_mount *mount, const vfs_node_id_t *node) {
  for (unsigned bucket = 0; bucket < VFS_CACHE_HASH_BUCKETS; bucket++)
    for (struct vfs_cache_page *page = vfs_cache_hash[bucket]; page;
         page = page->hash_next)
      if (page->mappings && page->mount == mount &&
          vfs_node_equal(&page->node, node))
        return true;
  return false;
}

static int vfs_cache_writeback(struct vfs_dentry *dentry, uint32_t first, size_t count) {
  struct vfs_mount *mount = dentry->mount;
  for (unsigned bucket = 0; bucket < VFS_CACHE_HASH_BUCKETS; bucket++)
    for (struct vfs_cache_page *page = vfs_cache_hash[bucket]; page;
         page = page->hash_next) {
      if (!page->writers || page->mount != mount ||
          !vfs_node_equal(&page->node, &dentry->node.id) ||
          page->index < first || page->index - first >= count)
        continue;
      uint64_t offset = (uint64_t)page->index * VFS_CACHE_PAGE_SIZE;
      if (offset >= dentry->node.size)
        continue;
      uint32_t length = dentry->node.size - offset;
      if (length > VFS_CACHE_PAGE_SIZE)
        length = VFS_CACHE_PAGE_SIZE;
      int result = vfs_mount_status(
          mount, disk_writable(mount->disk_number) && mount->filesystem->write
                     ? mount->filesystem->write(mount, &dentry->node, offset,
                                                page->data, length)
                     : VFS_ERROR_READ_ONLY);
      if (result < 0 || (uint32_t)result != length)
        return result < 0 ? result : VFS_ERROR_IO;
    }
  return VFS_OK;
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

/* Fill a consecutive cache miss with one filesystem read. The allocator's
 * contiguous run is split into independently owned pages, without a bounce
 * copy. A fragmented/pressured allocator can fall back to smaller reads and
 * reclaim clean LRU pages; active mappings are never candidates for
 * reclamation. */
static struct vfs_cache_page *vfs_cache_load(struct vfs_dentry *dentry,
                                             uint32_t index, uint32_t count) {
  if (!dentry->node.size || !count)
    return NULL;
  uint32_t file_pages = (dentry->node.size - 1) / VFS_CACHE_PAGE_SIZE + 1;
  if (index >= file_pages)
    return NULL;
  if (count > VFS_CACHE_READ_PAGES)
    count = VFS_CACHE_READ_PAGES;
  if (count > vfs_cache_limit)
    count = vfs_cache_limit;
  if (count > file_pages - index)
    count = file_pages - index;
  for (uint32_t i = 1; i < count; i++) {
    if (vfs_cache_find(dentry->mount, &dentry->node.id, index + i)) {
      count = i;
      break;
    }
  }
  uint8_t *data;
  while (!(data = page_malloc(count * VFS_CACHE_PAGE_SIZE))) {
    if (count > 1)
      count /= 2;
    else if (vfs_cache_tail)
      vfs_cache_free_page(vfs_cache_tail);
    else
      return NULL;
  }
  struct vfs_cache_page *pages[VFS_CACHE_READ_PAGES];
  uint32_t allocated = 0;
  for (; allocated < count; allocated++) {
    pages[allocated] = malloc(sizeof(*pages[allocated]));
    if (!pages[allocated])
      break;
    *pages[allocated] =
        (struct vfs_cache_page){.mount = dentry->mount,
                                .node = dentry->node.id,
                                .index = index + allocated,
                                .data = data + allocated * VFS_CACHE_PAGE_SIZE};
  }
  uint32_t offset = index * VFS_CACHE_PAGE_SIZE;
  uint32_t length = dentry->node.size - offset;
  if (length > count * VFS_CACHE_PAGE_SIZE)
    length = count * VFS_CACHE_PAGE_SIZE;
  int read =
      allocated == count
          ? vfs_mount_status(dentry->mount, dentry->mount->filesystem->read(
                                                dentry->mount, &dentry->node,
                                                offset, data, length))
          : VFS_ERROR_NO_MEMORY;
  if (read < 0 || (uint32_t)read != length) {
    for (uint32_t i = 0; i < allocated; i++)
      free(pages[i]);
    page_free(data, count * VFS_CACHE_PAGE_SIZE);
    return NULL;
  }
  for (uint32_t i = 0; i < count; i++)
    vfs_cache_insert(pages[i]);
  return pages[0];
}

static void vfs_destroy_mount(struct vfs_mount *mount) {
  uint32_t disk_index = mount->disk_number - 'A';
  vfs_cache_invalidate_mount(mount);
  if (mount->filesystem->unmount != NULL) {
    mount->filesystem->unmount(mount);
  }
  free(mount->root->name);
  free(mount->root);
  for (unsigned i = 0; i < VFS_IDENTITY_BUCKETS; i++) {
    while (mount->identities[i]) {
      vfs_identity_t *entry = mount->identities[i];
      mount->identities[i] = entry->next;
      free(entry);
    }
  }
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
    if (table->entries[descriptor].handle != NULL) {
      vfs_close(table->entries[descriptor].handle);
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
    if (destination->entries[descriptor].handle != NULL) {
      vfs_handle_retain(destination->entries[descriptor].handle);
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
  uintptr_t cache_bytes = memsize / VFS_CACHE_MEMORY_DIVISOR;
  if (cache_bytes < VFS_CACHE_MIN_BYTES) {
    cache_bytes = VFS_CACHE_MIN_BYTES;
  }
  if (cache_bytes > VFS_CACHE_MAX_BYTES) {
    cache_bytes = VFS_CACHE_MAX_BYTES;
  }
  vfs_cache_limit = cache_bytes / (sizeof(struct vfs_cache_page) + VFS_CACHE_PAGE_SIZE);
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

static int vfs_dentry_path(struct vfs_dentry *directory, char *buffer,
                           size_t capacity, bool drive) {
  size_t length = 0;
  for (struct vfs_dentry *dentry = directory; dentry->parent != NULL;
       dentry = dentry->parent) {
    size_t size = strlen(dentry->name) + 1;
    if (size > INT_MAX - length - 3)
      return VFS_ERROR_OVERFLOW;
    length += size;
  }
  if (length == 0) {
    length = 1;
  }
  size_t prefix = drive ? 2 : 0;
  length += prefix;
  if (buffer == NULL) {
    return length;
  }
  if (capacity <= length) {
    return VFS_ERROR_OVERFLOW;
  }
  buffer[length] = '\0';
  size_t cursor = length;
  for (struct vfs_dentry *dentry = directory; dentry->parent != NULL;
       dentry = dentry->parent) {
    size_t name_length = strlen(dentry->name);
    cursor -= name_length;
    memcpy(buffer + cursor, dentry->name, name_length);
    buffer[--cursor] = '/';
  }
  if (cursor != prefix) {
    buffer[prefix] = '/';
  }
  if (drive) {
    buffer[0] = directory->mount->drive;
    buffer[1] = ':';
  }
  return length;
}

int vfs_context_getcwd(vfs_context_t *context, char *buffer, size_t capacity) {
  return context && context->cwd
             ? vfs_dentry_path(context->cwd, buffer, capacity, false)
             : VFS_ERROR_INVALID;
}

int vfs_realpath(vfs_context_t *context, const char *path, char **result) {
  *result = NULL;
  struct vfs_dentry *dentry;
  int status = vfs_resolve(context, path, &dentry);
  if (status < 0)
    return status;
  status = vfs_dentry_path(dentry, NULL, 0, true);
  if (status >= 0) {
    *result = malloc((size_t)status + 1);
    status = *result
                 ? vfs_dentry_path(dentry, *result, (size_t)status + 1, true)
                 : VFS_ERROR_NO_MEMORY;
  }
  vfs_dentry_release(dentry);
  if (status < 0) {
    free(*result);
    *result = NULL;
  }
  return status;
}

uint8_t vfs_context_drive(const vfs_context_t *context) {
  return context == NULL || context->cwd == NULL ? 0 : context->cwd->mount->drive;
}

int vfs_open(vfs_context_t *context, const char *path, uint32_t flags,
             vfs_handle_t **handle_out) {
  if (context == NULL || path == NULL || *path == '\0' || handle_out == NULL ||
      (flags & (VFS_OPEN_READ | VFS_OPEN_WRITE)) == 0 ||
      ((flags & VFS_OPEN_DIRECTORY) &&
       (flags & (VFS_OPEN_WRITE | VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE |
                 VFS_OPEN_APPEND)))) {
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
  if ((flags & VFS_OPEN_DIRECTORY) && dentry->node.type != VFS_NODE_DIRECTORY) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_NOT_DIRECTORY;
  }
  if (!(flags & VFS_OPEN_DIRECTORY) && dentry->node.type != VFS_NODE_FILE) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_IS_DIRECTORY;
  }
  if ((flags & VFS_OPEN_WRITE) != 0 &&
      (dentry->mount->filesystem->write == NULL ||
       !disk_writable(dentry->mount->disk_number))) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_READ_ONLY;
  }
  if ((flags & VFS_OPEN_TRUNCATE) && vfs_cache_mapped(dentry->mount, &dentry->node.id)) {
    vfs_dentry_release(dentry);
    return VFS_ERROR_BUSY;
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
  handle->flags = flags & ~VFS_OPEN_CLOEXEC;
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
  if (handle->pipe) {
    vfs_pipe_t *pipe = handle->pipe;
    if (handle->flags & VFS_OPEN_READ)
      pipe->read_open = false;
    else
      pipe->write_open = false;
    io_poll_wake(&pipe->readers);
    io_poll_wake(&pipe->writers);
    if (!pipe->read_open && !pipe->write_open)
      free(pipe);
    free(handle);
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

static int vfs_read_at(vfs_handle_t *handle, void *buffer, uint32_t length,
                       uint32_t *offset) {
  if (handle == NULL || (length != 0 && buffer == NULL) ||
      (handle->flags & VFS_OPEN_READ) == 0) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (handle->pipe)
    return VFS_ERROR_NOT_SEEKABLE;
  if (handle->dentry->node.type != VFS_NODE_FILE)
    return VFS_ERROR_IS_DIRECTORY;
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  if (vfs_mount_status(mount, VFS_OK) < 0) {
    unlock(&mount->lock);
    return VFS_ERROR_IO;
  }
  if (length == 0 || *offset >= handle->dentry->node.size) {
    unlock(&mount->lock);
    return 0;
  }
  uint32_t available = handle->dentry->node.size - *offset;
  if (length > available) {
    length = available;
  }
  uint32_t completed = 0;
  while (completed < length) {
    uint32_t page_index = *offset / VFS_CACHE_PAGE_SIZE;
    uint32_t page_offset = *offset % VFS_CACHE_PAGE_SIZE;
    struct vfs_cache_page *page =
        vfs_cache_find(mount, &handle->dentry->node.id, page_index);
    if (page == NULL) {
      page = vfs_cache_load(handle->dentry, page_index,
                            ((uint64_t)page_offset + length - completed - 1) /
                                    VFS_CACHE_PAGE_SIZE +
                                1);
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
    *offset += chunk;
  }
  unlock(&mount->lock);
  return completed;
}

/* Nonblocking ring operations; fd dispatch below owns waiting and revalidates
 * user buffers/descriptors after every wake. Writes up to PIPE_BUF are atomic.
 */
static int vfs_pipe_io(vfs_handle_t *handle, void *buffer, uint32_t length,
                       bool write) {
  if (!(handle->flags & (write ? VFS_OPEN_WRITE : VFS_OPEN_READ)))
    return VFS_ERROR_BAD_DESCRIPTOR;
  if (!length)
    return 0;
  vfs_pipe_t *pipe = handle->pipe;
  if (write && !pipe->read_open)
    return VFS_ERROR_PIPE;
  uint32_t available = write ? VFS_PIPE_CAPACITY - pipe->used : pipe->used;
  if (!available || (write && length <= VFS_PIPE_ATOMIC && length > available))
    return !write && !pipe->write_open ? 0 : VFS_ERROR_AGAIN;
  if (length > available)
    length = available;
  uint32_t position =
      write ? (pipe->head + pipe->used) % VFS_PIPE_CAPACITY : pipe->head;
  uint32_t first = VFS_PIPE_CAPACITY - position;
  if (first > length)
    first = length;
  if (write) {
    memcpy(pipe->data + position, buffer, first);
    memcpy(pipe->data, (char *)buffer + first, length - first);
    pipe->used += length;
    io_poll_wake(&pipe->readers);
  } else {
    memcpy(buffer, pipe->data + position, first);
    memcpy((char *)buffer + first, pipe->data, length - first);
    pipe->head = (pipe->head + length) % VFS_PIPE_CAPACITY;
    pipe->used -= length;
    io_poll_wake(&pipe->writers);
  }
  return length;
}

int vfs_read(vfs_handle_t *handle, void *buffer, uint32_t length) {
  if (handle == NULL)
    return VFS_ERROR_BAD_DESCRIPTOR;
  return handle->pipe ? vfs_pipe_io(handle, buffer, length, false)
                      : vfs_read_at(handle, buffer, length, &handle->offset);
}

int vfs_pread(vfs_handle_t *handle, void *buffer, uint32_t length,
              uint32_t offset) {
  return vfs_read_at(handle, buffer, length, &offset);
}

int vfs_write(vfs_handle_t *handle, const void *buffer, uint32_t length) {
  if (handle == NULL || (length != 0 && buffer == NULL) ||
      (handle->flags & VFS_OPEN_WRITE) == 0) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (length == 0) {
    return 0;
  }
  if (handle->pipe)
    return vfs_pipe_io(handle, (void *)buffer, length, true);
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
  if (handle->pipe)
    return VFS_ERROR_NOT_SEEKABLE;
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
  if (handle->pipe)
    return VFS_ERROR_INVALID;
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  int status = vfs_mount_status(mount, VFS_OK);
  if (status == VFS_OK)
    status = vfs_cache_writeback(handle->dentry, 0, (size_t)UINT32_MAX / VFS_CACHE_PAGE_SIZE + 1);
  if (status == VFS_OK && mount->filesystem->sync != NULL) {
    status = vfs_mount_status(mount, mount->filesystem->sync(mount));
  }
  if (status == VFS_OK && !disk_sync(mount->disk_number)) {
    status = VFS_ERROR_IO;
  }
  unlock(&mount->lock);
  return status;
}

static int vfs_dentry_stat(struct vfs_dentry *dentry, vfs_stat_t *status) {
  struct vfs_mount *mount = dentry->mount;
  irq_state_t state = irq_save();
  unsigned bucket =
      vfs_cache_bucket(mount, &dentry->node.id, 0) % VFS_IDENTITY_BUCKETS;
  vfs_identity_t *entry = mount->identities[bucket];
  while (entry && !vfs_node_equal(&entry->node, &dentry->node.id))
    entry = entry->next;
  if (!entry) {
    if (vfs_identity_sequence > UINT64_MAX - (mount->device_identity ? 1 : 2)) {
      irq_restore(state);
      return VFS_ERROR_OVERFLOW;
    }
    entry = malloc(sizeof(*entry));
    if (!entry) {
      irq_restore(state);
      return VFS_ERROR_NO_MEMORY;
    }
    if (!mount->device_identity)
      mount->device_identity = ++vfs_identity_sequence;
    *entry = (vfs_identity_t){mount->identities[bucket], dentry->node.id,
                              ++vfs_identity_sequence};
    mount->identities[bucket] = entry;
  }
  *status = (vfs_stat_t){.type = dentry->node.type,
                         .attributes = disk_writable(mount->disk_number)
                                           ? dentry->node.attributes
                                           : RDO,
                         .size = dentry->node.size,
                         .modified_time = dentry->node.modified_time,
                         .device = mount->device_identity,
                         .inode = entry->number};
  irq_restore(state);
  return VFS_OK;
}

int vfs_fstat(vfs_handle_t *handle, vfs_stat_t *status) {
  if (handle == NULL || status == NULL) {
    return VFS_ERROR_INVALID;
  }
  if (handle->pipe) {
    *status = (vfs_stat_t){.type = VFS_STAT_PIPE,
                           .size = handle->pipe->used,
                           .inode = handle->pipe->identity};
    return VFS_OK;
  }
  if (vfs_mount_status(handle->dentry->mount, VFS_OK) < 0) {
    return VFS_ERROR_IO;
  }
  return vfs_dentry_stat(handle->dentry, status);
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
  result = vfs_dentry_stat(dentry, status);
  vfs_dentry_release(dentry);
  return result;
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
  return context->descriptors.entries[descriptor].handle;
}

int vfs_context_fchdir(vfs_context_t *context, int descriptor) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (handle == NULL) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (handle->pipe || handle->dentry->node.type != VFS_NODE_DIRECTORY) {
    return VFS_ERROR_NOT_DIRECTORY;
  }
  struct vfs_dentry *replacement = vfs_dentry_retain(handle->dentry);
  struct vfs_dentry *previous = context->cwd;
  context->cwd = replacement;
  vfs_dentry_release(previous);
  return VFS_OK;
}

static int vfs_fd_reserve(vfs_context_t *context, uint32_t minimum) {
  if (!context)
    return VFS_ERROR_INVALID;
  uint32_t descriptor;
  for (descriptor = minimum; descriptor < context->descriptors.capacity;
       descriptor++) {
    if (context->descriptors.entries[descriptor].handle == NULL) {
      break;
    }
  }
  if (descriptor == context->descriptors.capacity) {
    if (context->descriptors.capacity >
        UINT_MAX / 2 / sizeof(vfs_descriptor_t)) {
      return VFS_ERROR_TOO_MANY_FILES;
    }
    uint32_t capacity = context->descriptors.capacity * 2;
    vfs_descriptor_t *entries =
        realloc(context->descriptors.entries, capacity * sizeof(*entries));
    if (entries == NULL) {
      return VFS_ERROR_NO_MEMORY;
    }
    memset(entries + context->descriptors.capacity, 0,
           (capacity - context->descriptors.capacity) * sizeof(*entries));
    context->descriptors.entries = entries;
    context->descriptors.capacity = capacity;
  }
  return descriptor;
}

int vfs_fd_pipe(vfs_context_t *context, int descriptors[2], uint32_t flags) {
  if (flags & ~(VFS_OPEN_NONBLOCK | VFS_OPEN_CLOEXEC))
    return VFS_ERROR_INVALID;
  irq_state_t state = irq_save();
  int read_fd = vfs_fd_reserve(context, 3);
  vfs_handle_t *reader = read_fd < 0 ? NULL : malloc(sizeof(*reader));
  if (!reader) {
    irq_restore(state);
    return read_fd < 0 ? read_fd : VFS_ERROR_NO_MEMORY;
  }
  int write_fd = vfs_fd_reserve(context, read_fd + 1);
  vfs_handle_t *writer = write_fd < 0 ? NULL : malloc(sizeof(*writer));
  vfs_pipe_t *pipe = writer ? malloc(sizeof(*pipe)) : NULL;
  if (!pipe || vfs_identity_sequence == UINT64_MAX) {
    free(reader);
    free(writer);
    free(pipe);
    irq_restore(state);
    return write_fd < 0 ? write_fd : VFS_ERROR_NO_MEMORY;
  }
  memset(pipe, 0, sizeof(*pipe));
  pipe->read_open = pipe->write_open = true;
  pipe->identity = ++vfs_identity_sequence;
  *reader =
      (vfs_handle_t){.pipe = pipe,
                     .references = 1,
                     .flags = (flags & ~VFS_OPEN_CLOEXEC) | VFS_OPEN_READ};
  *writer =
      (vfs_handle_t){.pipe = pipe,
                     .references = 1,
                     .flags = (flags & ~VFS_OPEN_CLOEXEC) | VFS_OPEN_WRITE};
  context->descriptors.entries[read_fd] =
      (vfs_descriptor_t){reader, !!(flags & VFS_OPEN_CLOEXEC)};
  context->descriptors.entries[write_fd] =
      (vfs_descriptor_t){writer, !!(flags & VFS_OPEN_CLOEXEC)};
  descriptors[0] = read_fd;
  descriptors[1] = write_fd;
  irq_restore(state);
  return 0;
}

int vfs_fd_open(vfs_context_t *context, const char *path, uint32_t flags) {
  vfs_handle_t *handle = NULL;
  int status = vfs_open(context, path, flags, &handle);
  if (status < 0)
    return status;
  irq_state_t state = irq_save();
  int descriptor = vfs_fd_reserve(context, 3);
  if (descriptor < 0)
    vfs_close(handle);
  else
    context->descriptors.entries[descriptor] =
        (vfs_descriptor_t){handle, !!(flags & VFS_OPEN_CLOEXEC)};
  irq_restore(state);
  return descriptor;
}

int vfs_fd_close(vfs_context_t *context, int descriptor) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (handle == NULL) {
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  context->descriptors.entries[descriptor] = (vfs_descriptor_t){0};
  if (handle->pipe) {
    io_poll_wake(&handle->pipe->readers);
    io_poll_wake(&handle->pipe->writers);
  }
  return vfs_close(handle);
}

static int vfs_handle_status_flags(const vfs_handle_t *handle) {
  int flags = (handle->flags & VFS_OPEN_WRITE) == 0
                  ? O_RDONLY
                  : (handle->flags & VFS_OPEN_READ) == 0 ? O_WRONLY : O_RDWR;
  if (handle->flags & VFS_OPEN_APPEND)
    flags |= O_APPEND;
  if (handle->flags & VFS_OPEN_NONBLOCK)
    flags |= O_NONBLOCK;
  return flags;
}

int vfs_fd_fcntl(vfs_context_t *context, int descriptor, int command,
                 uintptr_t argument) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (handle == NULL)
    return VFS_ERROR_BAD_DESCRIPTOR;

  switch (command) {
  case F_GETFL:
    return vfs_handle_status_flags(handle);
  case F_SETFL:
    handle->flags &= ~(VFS_OPEN_APPEND | VFS_OPEN_NONBLOCK);
    if (argument & O_APPEND)
      handle->flags |= VFS_OPEN_APPEND;
    if (argument & O_NONBLOCK)
      handle->flags |= VFS_OPEN_NONBLOCK;
    return VFS_OK;
  case F_GETFD:
    return context->descriptors.entries[descriptor].close_on_exec ? FD_CLOEXEC
                                                                  : 0;
  case F_SETFD:
    if (argument & ~((uintptr_t)FD_CLOEXEC))
      return VFS_ERROR_INVALID;
    context->descriptors.entries[descriptor].close_on_exec =
        (argument & FD_CLOEXEC) != 0;
    return VFS_OK;
  case F_GETLK:
  case F_SETLK:
  case F_SETLKW:
    return VFS_ERROR_NOT_SUPPORTED;
  default:
    return VFS_ERROR_INVALID;
  }
}

short vfs_fd_poll(vfs_context_t *context, int descriptor, short events,
                  io_poll_watch_t *watch) {
  if (descriptor == 1 || descriptor == 2)
    return events & POLLOUT;
  if (descriptor == 0)
    return POLLERR; /* Console input has no pollable byte-stream ABI yet. */
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (!handle)
    return POLLNVAL;
  if (!handle->pipe)
    return events & (POLLIN | POLLOUT);
  vfs_pipe_t *pipe = handle->pipe;
  if (handle->flags & VFS_OPEN_READ) {
    io_poll_watch(&pipe->readers, watch);
    return (pipe->used ? events & POLLIN : 0) |
           (!pipe->write_open ? POLLHUP : 0);
  }
  io_poll_watch(&pipe->writers, watch);
  return (VFS_PIPE_CAPACITY - pipe->used >= VFS_PIPE_ATOMIC ? events & POLLOUT
                                                            : 0) |
         (!pipe->read_open ? POLLERR : 0);
}

int vfs_fd_available(vfs_context_t *context, int descriptor) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (!handle)
    return VFS_ERROR_BAD_DESCRIPTOR;
  if (handle->pipe)
    return handle->pipe->used;
  return VFS_ERROR_NOT_SUPPORTED;
}

static int vfs_fd_io(vfs_context_t *context, int descriptor, void *buffer,
                     uint32_t length, bool write) {
  irq_state_t state = irq_save();
  int result;
  uint64_t identity = 0;
  for (;;) {
    vfs_handle_t *handle = vfs_fd_get(context, descriptor);
    if (!handle ||
        (identity && (!handle->pipe || identity != handle->pipe->identity))) {
      result = VFS_ERROR_BAD_DESCRIPTOR;
      break;
    }
    if (handle->pipe && length &&
        !(write ? user_vm_readable((uintptr_t)buffer, length)
                : user_vm_prepare_write((uintptr_t)buffer, length))) {
      result = -14;
      break;
    }
    result = write ? vfs_write(handle, buffer, length)
                   : vfs_read(handle, buffer, length);
    if (!handle->pipe || result != VFS_ERROR_AGAIN ||
        (handle->flags & VFS_OPEN_NONBLOCK))
      break;
    identity = handle->pipe->identity;
    result =
        io_poll_wait(write ? &handle->pipe->writers : &handle->pipe->readers);
    if (result < 0)
      break;
  }
  irq_restore(state);
  return result;
}

int vfs_fd_read(vfs_context_t *context, int descriptor, void *buffer,
                uint32_t length) {
  return vfs_fd_io(context, descriptor, buffer, length, false);
}

int vfs_fd_pread(vfs_context_t *context, int descriptor, void *buffer,
                 uint32_t length, uint32_t offset) {
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  return handle == NULL ? VFS_ERROR_BAD_DESCRIPTOR
                        : vfs_pread(handle, buffer, length, offset);
}

int vfs_fd_write(vfs_context_t *context, int descriptor, const void *buffer,
                 uint32_t length) {
  return vfs_fd_io(context, descriptor, (void *)buffer, length, true);
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

int vfs_fd_truncate(vfs_context_t *context, int descriptor, uint32_t length) {
  irq_state_t state = irq_save();
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (!handle || !(handle->flags & VFS_OPEN_WRITE)) {
    irq_restore(state);
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (handle->pipe) {
    irq_restore(state);
    return VFS_ERROR_NOT_SUPPORTED;
  }
  vfs_handle_retain(handle);
  irq_restore(state);
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  int result = vfs_mount_status(mount, VFS_OK);
  if (result == VFS_OK &&
      (!disk_writable(mount->disk_number) || !mount->filesystem->truncate))
    result = VFS_ERROR_READ_ONLY;
  if (result == VFS_OK && vfs_cache_mapped(mount, &handle->dentry->node.id))
    result = VFS_ERROR_BUSY;
  if (result == VFS_OK)
    result = mount->filesystem->truncate(mount, &handle->dentry->node, length);
  if (result == VFS_OK) {
    handle->dentry->node.size = length;
    vfs_update_open_sizes(mount, &handle->dentry->node.id, length);
    vfs_cache_invalidate_node(mount, &handle->dentry->node.id);
  }
  unlock(&mount->lock);
  state = irq_save();
  vfs_close(handle);
  irq_restore(state);
  return result;
}

int vfs_mapping_create(vfs_context_t *context, int descriptor, uint64_t offset,
                       size_t length, bool shared, vfs_mapping_t **result) {
  /* Claim the descriptor atomically; a sibling thread may close it while the
   * mapping is still being prepared. */
  irq_state_t state = irq_save();
  vfs_handle_t *handle = vfs_fd_get(context, descriptor);
  if (!handle || !(handle->flags & VFS_OPEN_READ)) {
    irq_restore(state);
    return VFS_ERROR_BAD_DESCRIPTOR;
  }
  if (handle->pipe) {
    irq_restore(state);
    return VFS_ERROR_NOT_SUPPORTED;
  }
  vfs_handle_retain(handle);
  irq_restore(state);
  struct vfs_mount *mount = handle->dentry->mount;
  lock(&mount->lock);
  /* The file may have been truncated or replaced since the descriptor was
   * opened, so its identity and range are only valid under the mount lock. */
  uint64_t end =
      ((uint64_t)handle->dentry->node.size + VFS_CACHE_PAGE_SIZE - 1) &
      ~(uint64_t)(VFS_CACHE_PAGE_SIZE - 1);
  int status = vfs_mount_status(mount, VFS_OK);
  if (status == VFS_OK && handle->dentry->node.type != VFS_NODE_FILE)
    status = VFS_ERROR_IS_DIRECTORY;
  if (status == VFS_OK && (offset >= end || length > end - offset))
    status = VFS_ERROR_NOT_SUPPORTED;
  size_t count = length / VFS_CACHE_PAGE_SIZE;
  if (status == VFS_OK &&
      count > (SIZE_MAX - sizeof(vfs_mapping_t)) / sizeof(void *))
    status = VFS_ERROR_OVERFLOW;
  vfs_mapping_t *mapping = NULL;
  if (status == VFS_OK) {
    mapping = malloc(sizeof(*mapping) + count * sizeof(void *));
    if (!mapping)
      status = VFS_ERROR_NO_MEMORY;
  }
  if (status == VFS_OK) {
    memset(mapping, 0, sizeof(*mapping) + count * sizeof(void *));
    mapping->handle = handle;
    mapping->offset = offset;
    mapping->shared = shared;
    mapping->writable = !shared || (handle->flags & VFS_OPEN_WRITE);
    mapping->references = 1;
  }
  for (size_t i = 0; status == VFS_OK && i < count; i++) {
    uint32_t index = offset / VFS_CACHE_PAGE_SIZE + i;
    struct vfs_cache_page *page =
        vfs_cache_find(mount, &handle->dentry->node.id, index);
    if (!page)
      page = vfs_cache_load(handle->dentry, index, count - i);
    if (!page) {
      status = VFS_ERROR_IO;
      break;
    }
    if (!page->mappings)
      vfs_cache_unlink(page);
    page->mappings++;
    page->writers += shared && mapping->writable;
    mapping->pages[mapping->count++] = page->data;
  }
  unlock(&mount->lock);
  if (status == VFS_OK) {
    *result = mapping;
    return VFS_OK;
  }
  /* Release outside the mount lock: it evicts cache pages and closes the file. */
  if (mapping)
    vfs_mapping_release(mapping);
  else
    vfs_close(handle);
  return status;
}

void vfs_mapping_retain(vfs_mapping_t *mapping) { mapping->references++; }

void vfs_mapping_release(vfs_mapping_t *mapping) {
  if (--mapping->references)
    return;
  struct vfs_dentry *dentry = mapping->handle->dentry;
  for (size_t i = 0; i < mapping->count; i++) {
    struct vfs_cache_page *page =
        vfs_cache_find(dentry->mount, &dentry->node.id,
                       mapping->offset / VFS_CACHE_PAGE_SIZE + i);
    if (page) {
      page->writers -= mapping->shared && mapping->writable;
      if (!--page->mappings)
        vfs_cache_link(page);
    }
  }
  vfs_close(mapping->handle);
  free(mapping);
  vfs_cache_evict();
}

int vfs_mapping_sync(vfs_mapping_t *mapping, size_t first, size_t count) {
  if (!mapping->shared || !mapping->writable)
    return VFS_OK;
  struct vfs_dentry *dentry = mapping->handle->dentry;
  struct vfs_mount *mount = dentry->mount;
  lock(&mount->lock);
  int status = vfs_mount_status(mount, VFS_OK);
  if (!status)
    status = vfs_cache_writeback(dentry,
                                 mapping->offset / VFS_CACHE_PAGE_SIZE + first,
                                 count);
  if (!status && mount->filesystem->sync)
    status = vfs_mount_status(mount, mount->filesystem->sync(mount));
  if (!status && !disk_sync(mount->disk_number))
    status = VFS_ERROR_IO;
  unlock(&mount->lock);
  return status;
}
