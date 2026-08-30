#include <cmd.h>
#include <dos.h>
#include <limits.h>

int run_shell_command(const char *command, size_t command_length) {
  if (command == NULL ||
      command_length > (size_t)INT_MAX - sizeof("psh.bin -c ")) {
    return -1;
  }

  const size_t line_size = command_length + sizeof("psh.bin -c ");
  char *line = malloc(line_size);
  if (line == NULL) {
    return -1;
  }

  const size_t command_offset = sizeof("psh.bin -c ") - 1;
  memcpy(line, "psh.bin -c ", command_offset);
  memcpy(line + command_offset, command, command_length);
  line[line_size - 1] = '\0';
  int status = os_execute_shell(line, line_size - 1);
  free(line);
  return status;
}
