#ifndef RUNTIME_ARGS_H
#define RUNTIME_ARGS_H

#include <ctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int argc;
  char **argv;
  char *storage;
} runtime_arguments_t;

int runtime_arguments_parse(const char *line, runtime_arguments_t *arguments);
int runtime_arguments_take(char *line, runtime_arguments_t *arguments);
int runtime_arguments_load(runtime_arguments_t *arguments);
void runtime_arguments_destroy(runtime_arguments_t *arguments);
int runtime_command_line_build(int argc, char *const argv[], char **line,
                               size_t *length);

#ifdef __cplusplus
}
#endif

#endif
