// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <rpc.h>
#include <syscall.h>
#include <unistd.h>

static char *build_command(int argc, char **argv) {
  size_t length = 1;
  for (int index = 1; index < argc; index++) {
    size_t part_length = strlen(argv[index]);
    size_t separator = index == 1 ? 0 : 1;
    if (length > SIZE_MAX - separator ||
        part_length > SIZE_MAX - length - separator)
      return NULL;
    length += part_length + separator;
  }

  char *command = malloc(length);
  if (!command)
    return NULL;
  char *cursor = command;
  for (int index = 1; index < argc; index++) {
    if (index != 1)
      *cursor++ = ' ';
    size_t part_length = strlen(argv[index]);
    memcpy(cursor, argv[index], part_length);
    cursor += part_length;
  }
  *cursor = '\0';
  return command;
}

int main(int argc, char **argv) {
  if (argc < 2)
    return 2;

  rpc_endpoint_t gui;
  if (rpc_connect("gui", &gui, 0) != RPC_OK) {
    int child = fork();
    if (child < 0)
      return 1;
    if (child == 0)
      return exec("gui.bin", "gui.bin");
    if (rpc_connect("gui", &gui, 30000) != RPC_OK)
      return 1;
  }

  char *command = build_command(argc, argv);
  if (!command)
    return 1;
  int status = exec(argv[1], command);
  free(command);
  return status;
}
