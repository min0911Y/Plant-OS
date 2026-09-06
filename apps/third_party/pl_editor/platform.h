/**
 * platform.h - Platform abstraction interface
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include "pleditor.h"

/**
 * Platform abstraction interface
 * These functions must be implemented for each platform
 */

/* Initialize terminal settings */
bool pleditor_platform_init(void);

/* Restore terminal settings before exit */
void pleditor_platform_cleanup(void);

/* Get terminal window size */
bool pleditor_platform_get_size(int *rows, int *cols);

/* Read a key from the terminal */
int pleditor_platform_read_key(void);

/* Write string to terminal */
void pleditor_platform_write(const char *s, size_t len);

/* File operations */
bool pleditor_platform_read_file(const char *filename, char **buffer, size_t *len);
bool pleditor_platform_write_file(const char *filename, const char *buffer, size_t len);

/* A failed allocation terminates the editor after restoring the terminal. */
void *pleditor_platform_reallocate(void *pointer, size_t size);
_Noreturn void pleditor_platform_error(const char *message);

#endif /* PLATFORM_H */
