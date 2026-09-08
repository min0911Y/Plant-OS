#ifndef PLANT_STDIO_INTERNAL_H
#define PLANT_STDIO_INTERNAL_H
#include <stdio.h>
void stdio_stream_lock(FILE *stream) __attribute__((visibility("hidden")));
void stdio_stream_unlock(FILE *stream) __attribute__((visibility("hidden")));
#endif
