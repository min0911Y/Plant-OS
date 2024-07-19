#ifndef _FS_H
#define _FS_H
#include <define.h>
// fat.c
void      Register_fat_fileSys();
// file.c
FILE     *fopen(char *path, char *mode);
int       fputc(int c, FILE *fp);
int       fgetc(FILE *fp);
char     *fgets(char *s, int size, FILE *fp);
int       fseek(FILE *fp, int offset, int whence);
int       fclose(FILE *fp);
long      ftell(FILE *stream);
u32       fread(void *buffer, u32 size, u32 count, FILE *stream);
u32       fwrite(const void *ptr, u32 size, u32 nmemb, FILE *stream);
int       fputs(const char *str, FILE *stream);
int       fprintf(FILE *stream, const char *format, ...);
int       feof(FILE *stream);
int       getc(FILE *stream);
int       ferror(FILE *stream);
int       fsz(char *filename);
void      EDIT_FILE(char *name, char *dest, int length, int offset);
int       Copy(char *path, char *path1);
// bmp.c
bool      BMPVIEW8(char *path, u8 *vram, int xsize);
bool      BMPVIEW32(char *path, u8 *vram, int xsize);
// path.c
void      env_init(void);
void      env_write(char *name, char *val);
void      env_save();
void      env_reload();
char     *env_read(char *name);
bool      Path_Find_File(char *fileName, char *PATH_ADDR);
void      Path_Find_FileName(char *Result, char *fileName, char *PATH_ADDR);
// vfs.c
List     *vfs_listfile(char *dictpath);
bool      vfs_readfile(char *path, char *buffer);
bool      vfs_writefile(char *path, char *buffer, int size);
u32       vfs_filesize(char *filename);
bool      vfs_register_fs(vfs_t vfs);
bool      vfs_renamefile(char *filename, char *filename_of_new);
bool      vfs_change_path(char *dictName);
bool      vfs_deldir(char *dictname);
bool      vfs_delfile(char *filename);
bool      vfs_createfile(char *filename);
bool      vfs_createdict(char *filename);
void      vfs_getPath(char *buffer);
void      vfs_getPath_no_drive(char *buffer);
bool      vfs_change_disk(u8 drive);
bool      vfs_mount_disk(u8 disk_number, u8 drive);
u32       vfs_filesize(char *filename);
bool      vfs_readfile(char *path, char *buffer);
bool      vfs_change_disk_for_task(u8 drive, mtask *task);
bool      vfs_format(u8 disk_number, char *FSName);
bool      vfs_check_mount(u8 drive);
bool      vfs_unmount_disk(u8 drive);
bool      vfs_attrib(char *filename, ftype type);
vfs_file *vfs_fileinfo(char *filename);
#endif
