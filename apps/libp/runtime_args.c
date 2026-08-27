#include <limits.h>
#include <runtime_args.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

static int runtime_argument_count(const char *line, size_t *count_out) {
  const char *cursor = line;
  size_t count = 0;
  while (true) {
    while (*cursor == ' ') {
      cursor++;
    }
    if (*cursor == '\0') {
      *count_out = count;
      return 0;
    }
    count++;
    bool quoted = false;
    while (*cursor != '\0') {
      if (*cursor == '"') {
        quoted = !quoted;
        cursor++;
        continue;
      }
      if (*cursor == '\\' &&
          (cursor[1] == '"' || cursor[1] == '\\' || cursor[1] == ' ')) {
        cursor += 2;
        continue;
      }
      if (!quoted && *cursor == ' ') {
        break;
      }
      cursor++;
    }
    if (quoted) {
      return -1;
    }
  }
}

int runtime_arguments_take(char *line, runtime_arguments_t *arguments) {
  if (line == NULL || arguments == NULL) {
    free(line);
    return -1;
  }
  arguments->argc = 0;
  arguments->argv = NULL;
  arguments->storage = NULL;
  size_t count;
  if (runtime_argument_count(line, &count) != 0 || count > INT_MAX ||
      count > UINT_MAX / sizeof(char *) - 1) {
    free(line);
    return -1;
  }
  char **argv = malloc((count + 1) * sizeof(char *));
  if (argv == NULL) {
    free(line);
    return -1;
  }

  char *read_cursor = line;
  char *write_cursor = line;
  size_t index = 0;
  while (index < count) {
    while (*read_cursor == ' ') {
      read_cursor++;
    }
    argv[index++] = write_cursor;
    bool quoted = false;
    while (*read_cursor != '\0') {
      if (*read_cursor == '"') {
        quoted = !quoted;
        read_cursor++;
        continue;
      }
      if (*read_cursor == '\\' &&
          (read_cursor[1] == '"' || read_cursor[1] == '\\' ||
           read_cursor[1] == ' ')) {
        read_cursor++;
        *write_cursor++ = *read_cursor++;
        continue;
      }
      if (!quoted && *read_cursor == ' ') {
        break;
      }
      *write_cursor++ = *read_cursor++;
    }
    while (*read_cursor == ' ') {
      read_cursor++;
    }
    *write_cursor++ = '\0';
  }
  argv[count] = NULL;
  arguments->argc = (int)count;
  arguments->argv = argv;
  arguments->storage = line;
  return 0;
}

int runtime_arguments_parse(const char *line, runtime_arguments_t *arguments) {
  if (line == NULL || arguments == NULL) {
    return -1;
  }
  size_t length = strlen(line);
  if (length >= INT_MAX) {
    return -1;
  }
  char *copy = malloc(length + 1);
  if (copy == NULL) {
    return -1;
  }
  memcpy(copy, line, length + 1);
  return runtime_arguments_take(copy, arguments);
}

int runtime_arguments_load(runtime_arguments_t *arguments) {
  char *line;
  size_t length;
  if (get_command_line(&line, &length) != 0) {
    return -1;
  }
  (void)length;
  return runtime_arguments_take(line, arguments);
}

void runtime_arguments_destroy(runtime_arguments_t *arguments) {
  if (arguments == NULL) {
    return;
  }
  free(arguments->argv);
  free(arguments->storage);
  arguments->argc = 0;
  arguments->argv = NULL;
  arguments->storage = NULL;
}

int runtime_command_line_build(int argc, char *const argv[], char **line,
                               size_t *length) {
  if (argc < 0 || (argc != 0 && argv == NULL) || line == NULL ||
      length == NULL) {
    return -1;
  }
  *line = NULL;
  *length = 0;
  size_t required = 0;
  for (int i = 0; i < argc; i++) {
    if (argv[i] == NULL) {
      return -1;
    }
    if (i != 0) {
      if (required == UINT_MAX) {
        return -1;
      }
      required++;
    }
    if (required > UINT_MAX - 2) {
      return -1;
    }
    required += 2;
    for (const char *cursor = argv[i]; *cursor != '\0'; cursor++) {
      size_t bytes = *cursor == '"' || *cursor == '\\' ? 2 : 1;
      if (required > UINT_MAX - bytes) {
        return -1;
      }
      required += bytes;
    }
  }
  if (required >= INT_MAX) {
    return -1;
  }
  char *result = malloc(required + 1);
  if (result == NULL) {
    return -1;
  }
  char *output = result;
  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      *output++ = ' ';
    }
    *output++ = '"';
    for (const char *cursor = argv[i]; *cursor != '\0'; cursor++) {
      if (*cursor == '"' || *cursor == '\\') {
        *output++ = '\\';
      }
      *output++ = *cursor;
    }
    *output++ = '"';
  }
  *output = '\0';
  *line = result;
  *length = required;
  return 0;
}
