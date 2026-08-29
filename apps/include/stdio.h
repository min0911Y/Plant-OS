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
#define getchar getch
typedef struct FILE FILE;
extern FILE *stdout;
extern FILE *stdin;
extern FILE *stderr;
#include <stdarg.h>
int printf(const char* format, ...);
int sprintf(char *s, const char *format, ...);
int vsprintf(char *s, const char *format, va_list arg);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int fscanf(FILE *f, const char *fmt, ...);
int puts(char *str);
char *gets(char *str);
int remove(const char *filename);
int rename(char *filename1, char *filename2);
char *tmpnam(char *str);
FILE *fopen(const char *filename, const char *mode);
FILE *fdopen(int fd, const char *mode);
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
int getc(FILE *stream);
int sscanf(const char * s, const char * fmt, ...);
int snprintf(char * s, unsigned n, const char *fmt, ...);
int scanf(const char * fmt, ...);
int fileno(FILE *fp);
char *getcwd(char *buf, size_t size);
int unlink(const char *pathname);
void rewind(FILE *stream);
int vprintf(const char *fmt, va_list ap);
void perror(const char *message);
void stdio_initialize(void);
void stdio_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
