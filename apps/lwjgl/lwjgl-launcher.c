// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <string.h>

#include <rpc.h>
#include <runtime_args.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *directory = NULL;
  while (argc > 2) {
    if (!strcmp(argv[1], "--directory"))
      directory = argv[2];
    else
      break;
    argc -= 2;
    argv += 2;
  }
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

  if (directory && chdir(directory) != 0)
    return 1;

  char *command;
  size_t length;
  if (runtime_command_line_build(argc - 1, argv + 1, &command, &length) != 0)
    return 1;
  int status = exec(argv[1], command);
  free(command);
  return status;
}
