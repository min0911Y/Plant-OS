#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

static bool copy_to_drive(char drive, const char *source,
                          const char *destination) {
  size_t length = strlen(destination);
  if (length > (size_t)UINT_MAX - 4) {
    return false;
  }
  char *path = malloc(length + 4);
  if (path == NULL) {
    return false;
  }
  if (destination[0] == '/') {
    sprintf(path, "%c:%s", drive, destination);
  } else {
    sprintf(path, "%c:\\%s", drive, destination);
  }
  bool copied = Copy((char *)source, path) == 0;
  free(path);
  return copied;
}

static bool remount_drive(char drive) {
  char command[] = "remount_drive X:";
  drive = toupper((unsigned char)drive);
  command[sizeof("remount_drive ") - 1] = drive;
  return system(command) == 0 && toupper(api_current_drive()) == drive;
}

int main() {
  char source_drive = toupper(api_current_drive());
  if (source_drive < 'A' || source_drive > 'Z') {
    return 1;
  }

  char destination_drive;
  for (;;) {
    printf("Which drive do you want to install? [A-Z]\n");
    destination_drive = toupper(getch());
    if (destination_drive < 'A' || destination_drive > 'Z') {
      printf("Invalid drive.\n");
      continue;
    }
    printf("tcc will install in Drive %c [y/n]\n", destination_drive);
    if (getch() != 'n') {
      break;
    }
  }

  int result = 1;
  struct finfo_block *headers = NULL;
  if (!remount_drive(destination_drive)) {
    printf("Unable to remount destination drive.\n");
    goto cleanup;
  }
  printf("installation is making tcc dict....\n -> tcc\n");
  if (mkdir("tcc") != 0 || system("cd tcc") != 0 || mkdir("crt") != 0 ||
      mkdir("lib") != 0 || mkdir("inst") != 0 || mkdir("include") != 0) {
    printf("Unable to create installation directories.\n");
    goto cleanup;
  }
  if (!remount_drive(source_drive)) {
    printf("Unable to restore source drive.\n");
    goto cleanup;
  }

  printf("now, copy binary\n");
  if (!copy_to_drive(destination_drive, "tcc.bin", "/tcc.bin") ||
      !copy_to_drive(destination_drive, "tcc/lib/libp.a",
                     "/tcc/lib/libp.a") ||
      !copy_to_drive(destination_drive, "tcc/lib/libabi.a",
                     "/tcc/lib/libabi.a") ||
      !copy_to_drive(destination_drive, "tcc/inst/libtcc1.a",
                     "/tcc/inst/libtcc1.a")) {
    printf("Unable to copy TCC binaries.\n");
    goto cleanup;
  }

  size_t header_count;
  if (list_directory("tcc/include", &headers, &header_count) != 0) {
    printf("Unable to list TCC headers.\n");
    goto cleanup;
  }
  printf("now, copy headers\n");
  for (size_t i = 0; i < header_count; i++) {
    if (headers[i].type == FILE_DIRECTORY) {
      continue;
    }
    for (size_t j = 0; headers[i].name[j] != '\0'; j++) {
      headers[i].name[j] = tolower((unsigned char)headers[i].name[j]);
    }
    size_t name_length = strlen(headers[i].name);
    if (name_length > (size_t)UINT_MAX - sizeof("tcc/include/")) {
      goto cleanup;
    }
    char *source = malloc(sizeof("tcc/include/") + name_length);
    if (source == NULL) {
      goto cleanup;
    }
    sprintf(source, "tcc/include/%s", headers[i].name);
    printf(" -> %s\n", headers[i].name);
    bool copied = copy_to_drive(destination_drive, source, source);
    free(source);
    if (!copied) {
      printf("Unable to copy header.\n");
      goto cleanup;
    }
  }

  char command[] = "tcc.bin -c crti.c -o X:\\tcc\\crt\\crti.o";
  command[sizeof("tcc.bin -c crti.c -o ") - 1] = destination_drive;
  printf("execute: %s\n", command);
  if (system(command) != 0) {
    printf("Unable to build crti.o.\n");
    goto cleanup;
  }
  result = 0;

cleanup:
  free(headers);
  if (!remount_drive(source_drive)) {
    result = 1;
  }
  return result;
}
