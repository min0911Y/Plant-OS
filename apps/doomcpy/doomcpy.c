#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>

static bool remount_drive(char drive) {
  char command[] = "remount_drive X:";
  drive = toupper((unsigned char)drive);
  command[sizeof("remount_drive ") - 1] = drive;
  return system(command) == 0 && toupper(api_current_drive()) == drive;
}

static bool read_file(const char *path, char **buffer, int *size) {
  struct stat status;
  int file_size = stat(path, &status) == 0 ? (int)status.st_size : -1;
  if (file_size < 0) {
    return false;
  }
  char *data = file_size == 0 ? NULL : malloc((size_t)file_size);
  if (file_size != 0 && data == NULL) {
    return false;
  }
  FILE *stream = fopen(path, "rb");
  if (stream == NULL ||
      (file_size != 0 &&
       fread(data, 1, file_size, stream) != (size_t)file_size)) {
    if (stream != NULL) {
      fclose(stream);
    }
    free(data);
    return false;
  }
  fclose(stream);
  *buffer = data;
  *size = file_size;
  return true;
}

int main() {
  char source_drive = toupper(api_current_drive());
  if (source_drive < 'A' || source_drive > 'Z') {
    printf("Invalid source drive.\n");
    return 1;
  }

  char destination_drive;
  for (;;) {
    printf("Which drive do you want to copy to? [A-Z]\n");
    destination_drive = toupper(getch());
    if (destination_drive < 'A' || destination_drive > 'Z') {
      printf("Invalid drive.\n");
      continue;
    }
    printf("the file doom.zip will copy to Drive %c [y/n]\n",
           destination_drive);
    if (getch() != 'n') {
      break;
    }
  }

  printf("Reading 001doom.bin file...");
  char *first = NULL;
  int first_size;
  if (!read_file("001doom.bin", &first, &first_size)) {
    printf("failed.\n");
    return 1;
  }
  printf("done.\n");

  char *whole_file = NULL;
  int result = 1;
  for (;;) {
    printf("Please insert doom2 disk and press enter to continue...");
    while (getch() != '\n') {
    }
    printf("\n");
    if (!remount_drive(source_drive)) {
      printf("Unable to remount source drive.\n");
      goto cleanup;
    }

    struct stat status;
    int second_size =
        stat("002doom.bin", &status) == 0 ? (int)status.st_size : -1;
    if (second_size < 0) {
      printf("Insert a wrong disk, retry...\n");
      continue;
    }
    if (first_size > INT_MAX - second_size) {
      printf("doom.zip is too large.\n");
      goto cleanup;
    }
    int whole_size = first_size + second_size;
    whole_file = whole_size == 0 ? NULL : malloc((size_t)whole_size);
    if (whole_size != 0 && whole_file == NULL) {
      printf("Not enough memory.\n");
      goto cleanup;
    }
    if (first_size != 0) {
      memcpy(whole_file, first, first_size);
    }
    printf("Reading 002doom.bin file...");
    FILE *stream = fopen("002doom.bin", "rb");
    if (stream == NULL ||
        (second_size != 0 &&
         fread(whole_file + first_size, 1, second_size, stream) !=
             (size_t)second_size)) {
      if (stream != NULL) {
        fclose(stream);
      }
      printf("failed.\n");
      goto cleanup;
    }
    fclose(stream);
    printf("done.\nMerging and copying doom.zip...");
    stream = remount_drive(destination_drive) ? fopen("doom.zip", "wb") : NULL;
    if (stream == NULL ||
        fwrite(whole_file, 1, whole_size, stream) != (size_t)whole_size ||
        fclose(stream) != 0) {
      printf("failed.\n");
      goto cleanup;
    }
    printf("done.\nand then, you can extract it by miniunz.bin.\n  Have Fun! ^-");
    result = 0;
    break;
  }

cleanup:
  free(whole_file);
  free(first);
  if (!remount_drive(source_drive)) {
    result = 1;
  }
  return result;
}
