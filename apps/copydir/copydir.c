#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>

static bool restore_source_drive(char drive) {
  if (!vfs_check_mount(drive)) {
    printf("Source drive %c: is no longer mounted.\n", drive);
    return false;
  }
  char command[] = "X:";
  command[0] = drive;
  if (system(command) != 0 || toupper(api_current_drive()) != drive) {
    printf("Unable to switch back to source drive %c:.\n", drive);
    return false;
  }
  return true;
}

static bool ensure_directory(const char *path) {
  if (mkdir(path) == 0) {
    return true;
  }
  struct stat status;
  return errno == EEXIST && stat(path, &status) == 0 &&
         S_ISDIR(status.st_mode);
}

static bool copy_directory_recursive(const char *source_directory,
                                     char *destination,
                                     size_t destination_size) {
  if (chdir(source_directory) != 0) {
    printf("Unable to enter source directory %s.\n", source_directory);
    return false;
  }

  bool success = true;
  struct finfo_block *files;
  size_t file_count;
  if (list_directory("", &files, &file_count) != 0) {
    printf("Unable to list source directory %s.\n", source_directory);
    success = false;
    goto restore_directory;
  }

  size_t base_length = strlen(destination);
  for (size_t i = 0; i < file_count; i++) {
    if (strcmp(files[i].name, ".") == 0 ||
        strcmp(files[i].name, "..") == 0) {
      continue;
    }

    size_t name_length = strlen(files[i].name);
    size_t suffix_length = files[i].type == DIR ? 2 : 1;
    if (base_length >= destination_size ||
        suffix_length > destination_size - base_length ||
        name_length > destination_size - base_length - suffix_length) {
      printf("Destination path is too long: %s%s\n", destination,
             files[i].name);
      success = false;
      break;
    }

    memcpy(destination + base_length, files[i].name, name_length + 1);
    if (files[i].type == DIR) {
      if (!ensure_directory(destination)) {
        printf("Unable to create destination directory %s.\n", destination);
        success = false;
      } else {
        destination[base_length + name_length] = '\\';
        destination[base_length + name_length + 1] = '\0';
        success = copy_directory_recursive(files[i].name, destination,
                                           destination_size);
      }
    } else {
      printf("copy %s to %s\n", files[i].name, destination);
      if (Copy(files[i].name, destination) != 0) {
        printf("Unable to copy %s.\n", files[i].name);
        success = false;
      }
    }
    destination[base_length] = '\0';
    if (!success) {
      break;
    }
  }
  free(files);

restore_directory:
  if (chdir("..") != 0) {
    printf("Unable to restore the source directory.\n");
    success = false;
  }
  return success;
}

int main(int argc, char **argv) {
  if (argc != 3 || argv[1][0] == '\0' ||
      !((argv[1][1] == '\0') ||
        (argv[1][1] == ':' && argv[1][2] == '\0'))) {
    printf("Usage: %s <Dest drive> <dir>\n", argv[0]);
    return 1;
  }

  char destination_drive = toupper(argv[1][0]);
  char source_drive = toupper(api_current_drive());
  if (destination_drive < 'A' || destination_drive > 'Z' ||
      source_drive < 'A' || source_drive > 'Z') {
    printf("Invalid source or destination drive.\n");
    return 1;
  }
  size_t directory_length = strlen(argv[2]);
  char destination[255];
  if (directory_length == 0 || directory_length > sizeof(destination) - 5) {
    printf("Destination directory is empty or too long.\n");
    return 1;
  }

  char remount_command[] = "remount_drive X:";
  remount_command[sizeof("remount_drive ") - 1] = destination_drive;
  if (system(remount_command) != 0) {
    printf("Unable to remount drive %c:.\n", destination_drive);
    restore_source_drive(source_drive);
    return 1;
  }

  bool copied = false;
  bool restored = false;
  destination[0] = destination_drive;
  destination[1] = ':';
  destination[2] = '\\';
  memcpy(destination + 3, argv[2], directory_length);
  destination[directory_length + 3] = '\0';
  if (!ensure_directory(destination)) {
    printf("Unable to create destination directory %s.\n", destination);
    goto cleanup;
  }
  destination[directory_length + 3] = '\\';
  destination[directory_length + 4] = '\0';

  restored = restore_source_drive(source_drive);
  copied = restored &&
           copy_directory_recursive(argv[2], destination, sizeof(destination));

cleanup:
  if (!restore_source_drive(source_drive)) {
    restored = false;
  }
  return copied && restored ? 0 : 1;
}
