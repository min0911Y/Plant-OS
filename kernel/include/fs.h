#ifndef _FS_H
#define _FS_H

#include "../../apps/include/vfs_stat.h"
#include <define.h>
#include <io_poll.h>

typedef struct FILE FILE;
typedef struct vfs_handle vfs_handle_t;
typedef struct vfs_context vfs_context_t;

/* File mapping backing owns pinned cache pages and an open-file reference.
 * References change only while the VM mapping lock is held. */
typedef struct vfs_mapping {
  vfs_handle_t *handle;
  size_t count;
  uint64_t offset;
  uint32_t references;
  bool shared, writable;
  void *pages[];
} vfs_mapping_t;
int vfs_mapping_create(vfs_context_t *context, int descriptor, uint64_t offset,
                       size_t length, bool shared, vfs_mapping_t **result);
void vfs_mapping_retain(vfs_mapping_t *mapping);
void vfs_mapping_release(vfs_mapping_t *mapping);
int vfs_mapping_sync(vfs_mapping_t *mapping, size_t first, size_t count);

enum vfs_error {
  VFS_OK = 0,
  VFS_ERROR_NO_ENTRY = -2,
  VFS_ERROR_INTERRUPTED = -4,
  VFS_ERROR_AGAIN = -11,
  VFS_ERROR_NOT_SEEKABLE = -29,
  VFS_ERROR_PIPE = -32,
  VFS_ERROR_IO = -5,
  VFS_ERROR_BAD_DESCRIPTOR = -9,
  VFS_ERROR_NO_MEMORY = -12,
  VFS_ERROR_ACCESS = -13,
  VFS_ERROR_BUSY = -16,
  VFS_ERROR_EXISTS = -17,
  VFS_ERROR_NOT_DIRECTORY = -20,
  VFS_ERROR_IS_DIRECTORY = -21,
  VFS_ERROR_INVALID = -22,
  VFS_ERROR_TOO_MANY_FILES = -24,
  VFS_ERROR_NO_SPACE = -28,
  VFS_ERROR_READ_ONLY = -30,
  VFS_ERROR_NOT_EMPTY = -39,
  VFS_ERROR_OVERFLOW = -75,
  VFS_ERROR_NOT_SUPPORTED = -95,
};

enum vfs_open_flags {
  VFS_OPEN_READ = 1u << 0,
  VFS_OPEN_WRITE = 1u << 1,
  VFS_OPEN_CREATE = 1u << 2,
  VFS_OPEN_EXCLUSIVE = 1u << 3,
  VFS_OPEN_TRUNCATE = 1u << 4,
  VFS_OPEN_APPEND = 1u << 5,
  VFS_OPEN_DIRECTORY = 1u << 6,
  VFS_OPEN_NONBLOCK = 1u << 7,
  VFS_OPEN_CLOEXEC = 1u << 8,
};

typedef vfs_file_stat_t vfs_stat_t;

enum vfs_syscall_operation {
  VFS_SYSCALL_OPEN,
  VFS_SYSCALL_CLOSE,
  VFS_SYSCALL_READ,
  VFS_SYSCALL_WRITE,
  VFS_SYSCALL_SEEK,
  VFS_SYSCALL_SYNC,
  VFS_SYSCALL_STAT,
  VFS_SYSCALL_FSTAT,
  VFS_SYSCALL_LIST_DIRECTORY,
  VFS_SYSCALL_MKDIR,
  VFS_SYSCALL_UNLINK,
  VFS_SYSCALL_RMDIR,
  VFS_SYSCALL_RENAME,
  VFS_SYSCALL_CHDIR,
  VFS_SYSCALL_GETCWD,
  VFS_SYSCALL_CURRENT_DRIVE,
  VFS_SYSCALL_MOUNT_CHECK,
  VFS_SYSCALL_MOUNT,
  VFS_SYSCALL_UNMOUNT,
  VFS_SYSCALL_CHANGE_DRIVE,
  VFS_SYSCALL_FORMAT,
  VFS_SYSCALL_TRUNCATE,
  VFS_SYSCALL_REALPATH,
  VFS_SYSCALL_PREAD,
  VFS_SYSCALL_FCHDIR,
  VFS_SYSCALL_FCNTL,
  VFS_SYSCALL_PIPE,
  VFS_SYSCALL_POLL,
  VFS_SYSCALL_AVAILABLE,
  VFS_SYSCALL_COUNT,
};

typedef struct {
  uint32_t size;
  union {
    struct {
      uintptr_t fds;
      uint32_t count;
      int32_t timeout;
    } poll;
    struct {
      uintptr_t path;
      uint32_t flags;
    } open;
    struct {
      int32_t descriptor;
    } descriptor;
    struct {
      int32_t descriptor;
      int32_t command;
      uintptr_t argument;
    } fcntl;
    struct {
      int32_t descriptor;
      uintptr_t buffer;
      uint32_t length;
    } io;
    struct {
      int32_t descriptor;
      uintptr_t buffer;
      uint32_t length;
      uint32_t offset;
    } positioned_io;
    struct {
      int32_t descriptor;
      int32_t offset;
      int32_t whence;
    } seek;
    struct {
      int32_t descriptor;
      uint32_t length;
    } truncate;
    struct {
      uintptr_t path;
      uintptr_t status;
    } stat;
    struct {
      int32_t descriptor;
      uintptr_t status;
    } fstat;
    struct {
      uintptr_t path;
      uintptr_t entries;
      uint32_t capacity;
    } list;
    struct {
      uintptr_t path;
      uintptr_t buffer;
      uint32_t capacity;
    } canonical;
    struct {
      uintptr_t path;
    } path;
    struct {
      uintptr_t source;
      uintptr_t destination;
    } rename;
    struct {
      uintptr_t buffer;
      uint32_t capacity;
    } cwd;
    struct {
      uint32_t disk;
      uint32_t drive;
    } mount;
    struct {
      uint32_t disk;
      uint32_t filesystem;
    } format;
  } arguments;
} vfs_syscall_request_t;

void Register_fat_fileSys(void);
void init_iso9660(void);
void init_devfs(void);

void init_vfs(void);
bool vfs_register_fs(const vfs_filesystem_t *filesystem);
bool vfs_mount_disk(uint8_t disk_number, uint8_t drive);
bool vfs_mount_all_disks(void);
bool vfs_unmount_disk(uint8_t drive);
bool vfs_disk_reusable(uint8_t disk);
void vfs_disk_removed(uint8_t disk);
bool vfs_check_mount(uint8_t drive);
int vfs_format(uint8_t disk_number, const char *filesystem_name);
void *vfs_mount_data(struct vfs_mount *mount);
void vfs_mount_set_data(struct vfs_mount *mount, void *data);
uint8_t vfs_mount_disk_number(const struct vfs_mount *mount);
uint8_t vfs_mount_drive(const struct vfs_mount *mount);

vfs_context_t *vfs_context_create(uint8_t drive);
vfs_context_t *vfs_context_clone_cwd(const vfs_context_t *source);
vfs_context_t *vfs_context_fork(const vfs_context_t *source);
void vfs_context_retain(vfs_context_t *context);
void vfs_context_release(vfs_context_t *context);
bool vfs_context_transfer_cwd(vfs_context_t *source,
                              vfs_context_t *destination);
int vfs_context_change_drive(vfs_context_t *context, uint8_t drive);
int vfs_context_chdir(vfs_context_t *context, const char *path);
int vfs_context_fchdir(vfs_context_t *context, int descriptor);
int vfs_context_getcwd(vfs_context_t *context, char *buffer,
                       size_t capacity);
uint8_t vfs_context_drive(const vfs_context_t *context);

int vfs_open(vfs_context_t *context, const char *path, uint32_t flags,
             vfs_handle_t **handle);
void vfs_handle_retain(vfs_handle_t *handle);
int vfs_close(vfs_handle_t *handle);
int vfs_read(vfs_handle_t *handle, void *buffer, uint32_t length);
int vfs_pread(vfs_handle_t *handle, void *buffer, uint32_t length,
              uint32_t offset);
int vfs_write(vfs_handle_t *handle, const void *buffer, uint32_t length);
int vfs_seek(vfs_handle_t *handle, int32_t offset, int whence);
int vfs_sync(vfs_handle_t *handle);
int vfs_fstat(vfs_handle_t *handle, vfs_stat_t *status);
int vfs_stat(vfs_context_t *context, const char *path, vfs_stat_t *status);
int vfs_list_directory(vfs_context_t *context, const char *path,
                       vfs_file *entries, size_t capacity, size_t *count);
int vfs_mkdir(vfs_context_t *context, const char *path);
int vfs_unlink(vfs_context_t *context, const char *path);
int vfs_rmdir(vfs_context_t *context, const char *path);
int vfs_rename(vfs_context_t *context, const char *source,
               const char *destination);

int vfs_fd_pipe(vfs_context_t *context, int descriptors[2], uint32_t flags);
int vfs_fd_available(vfs_context_t *context, int descriptor);
short vfs_fd_poll(vfs_context_t *context, int descriptor, short events,
                  io_poll_watch_t *watch);
int vfs_fd_open(vfs_context_t *context, const char *path, uint32_t flags);
int vfs_fd_close(vfs_context_t *context, int descriptor);
int vfs_fd_fcntl(vfs_context_t *context, int descriptor, int command,
                 uintptr_t argument);
int vfs_fd_read(vfs_context_t *context, int descriptor, void *buffer,
                uint32_t length);
int vfs_fd_pread(vfs_context_t *context, int descriptor, void *buffer,
                 uint32_t length, uint32_t offset);
int vfs_fd_write(vfs_context_t *context, int descriptor, const void *buffer,
                 uint32_t length);
int vfs_fd_seek(vfs_context_t *context, int descriptor, int32_t offset,
                int whence);
int vfs_fd_sync(vfs_context_t *context, int descriptor);
int vfs_fd_stat(vfs_context_t *context, int descriptor, vfs_stat_t *status);
int vfs_fd_truncate(vfs_context_t *context, int descriptor, uint32_t length);
int vfs_realpath(vfs_context_t *context, const char *path, char **result);

FILE *fopen(const char *path, const char *mode);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
int fclose(FILE *stream);
long ftell(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fputs(const char *str, FILE *stream);
int fprintf(FILE *stream, const char *format, ...);
int feof(FILE *stream);
int getc(FILE *stream);
int ferror(FILE *stream);

void env_init(void);
void env_write(char *name, char *value);
void env_save(void);
void env_reload(void);
char *env_read(char *name);
bool Path_Find_File(char *fileName, char *PATH_ADDR);
void Path_Find_FileName(char *Result, char *fileName, char *PATH_ADDR);

bool BMPVIEW8(char *path, unsigned char *vram, int xsize);
bool BMPVIEW32(char *path, unsigned char *vram, int xsize);

#endif
