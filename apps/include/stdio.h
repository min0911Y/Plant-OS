#ifndef _STDIO_H
#define _STDIO_H
#ifdef __cplusplus
extern "C" {
#endif
#define EOF -1
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ (4096*2)
#define FILENAME_MAX 255
#include <ctypes.h>
#include <fcntl.h>
int getch(void);
int getchar(void);
typedef struct FILE FILE;
typedef long fpos_t;
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
extern FILE *stdout;
extern FILE *stdin;
extern FILE *stderr;
#include <stdarg.h>
int printf(const char* format, ...);
int sprintf(char *s, const char *format, ...);
int vsprintf(char *s, const char *format, va_list arg);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int fscanf(FILE *f, const char *fmt, ...);
int vfscanf(FILE *stream, const char *format, va_list arguments);
int vscanf(const char *format, va_list arguments);
int puts(const char *str);
char *gets(char *str);
int remove(const char *filename);
int rename(const char *filename1, const char *filename2);
char *tmpnam(char *str);
FILE *fopen(const char *filename, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *tmpfile(void);
void setbuf(FILE *stream, char *buffer);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
int fgetpos(FILE *stream, fpos_t *position);
int fsetpos(FILE *stream, const fpos_t *position);
int fclose(FILE *fp);
int fseek(FILE *fp, long offset, int whence);
long ftell(FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
int fgetc(FILE *stream);
int ungetc(int c, FILE *fp);
int fputc(int ch,FILE *stream);
int putc(int ch, FILE *stream);
int putchar(int ch);
int fflush(FILE *stream);
char *fgets(char *str, int n, FILE *stream);
int fputs(const char *str, FILE *stream);
int fprintf (FILE* stream, const char*format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int getc(FILE *stream);
int sscanf(const char * s, const char * fmt, ...);
int vsscanf(const char *source, const char *format, va_list arguments);
int vasprintf(char **text, const char *format, va_list arguments);
int asprintf(char **text, const char *format, ...);
int snprintf(char *s, size_t n, const char *fmt, ...);
int scanf(const char * fmt, ...);
int fileno(FILE *fp);
char *getcwd(char *buf, size_t size);
int unlink(const char *pathname);
void rewind(FILE *stream);
int vprintf(const char *fmt, va_list ap);
void perror(const char *message);
void stdio_initialize(void);
void stdio_shutdown(void);
FILE *open_memstream(char **buffer, size_t *size);
#ifdef __cplusplus
}
#endif
#endif
