#include <ctype.h>
#include <limits.h>
#include <mst.h>
#include <pl_readline.h>
#include <runtime_args.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
static char *search_path;
static MST_Object *environment;

static char *env_read(char *name) {
  if (MST_get_var(name, MST_get_root_space(environment)) == NULL) {
    return NULL;
  }
  return MST_get_string_in_space(environment, name,
                                 MST_get_root_space(environment));
}

static bool env_init(int command_mode) {
  search_path = "";
  environment = NULL;

  int size = filesize("env.cfg");
  if (size < 0) {
    if (command_mode) {
      return true;
    }
    mkfile("env.cfg");
    Edit_File("env.cfg", "# created by psh", 16, 0);
    size = filesize("env.cfg");
    if (size < 0) {
      printf("Unable to create env.cfg.\n");
      return false;
    }
  }

  size_t file_size = (size_t)size;
  if (file_size >= (size_t)INT_MAX || file_size > SIZE_MAX - 1) {
    printf("env.cfg is too large.\n");
    return false;
  }
  size_t buffer_size = file_size + 1;
  char *buff = (char *)malloc(buffer_size);
  if (buff == NULL) {
    printf("Unable to load env.cfg.\n");
    return false;
  }
  if (!api_ReadFile("env.cfg", buff)) {
    free(buff);
    printf("Unable to read env.cfg.\n");
    return false;
  }
  buff[buffer_size - 1] = '\0';
  environment = MST_init(buff);
  free(buff);
  if (environment == NULL) {
    printf("Unable to parse env.cfg.\n");
    return false;
  }
  if (environment->err) {
    printf("Unable to parse env.cfg: %s\n", MST_strerror(environment));
    MST_free(environment);
    environment = NULL;
    return false;
  }

  char *path = env_read("path");
  if (path != NULL) {
    search_path = path;
  }
  return true;
}

static bool find_in_search_path(const char *file_name, char **result) {
  if (file_name == NULL || result == NULL) {
    return false;
  }
  *result = NULL;
  const char *cursor = search_path;
  const size_t file_name_length = strlen(file_name);

  while (*cursor != '\0') {
    while (*cursor == ';') {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }

    const char *entry = cursor;
    while (*cursor != '\0' && *cursor != ';') {
      cursor++;
    }
    size_t entry_length = (size_t)(cursor - entry);
    bool add_separator = entry[entry_length - 1] != '\\' &&
                         entry[entry_length - 1] != '/';
    size_t separator = add_separator;
    if (entry_length > UINT_MAX - separator) {
      return false;
    }
    size_t prefix = entry_length + separator;
    if (prefix == UINT_MAX || file_name_length > UINT_MAX - prefix - 1) {
      return false;
    }
    size_t size = prefix + file_name_length + 1;
    char *path = malloc(size);
    if (path == NULL) {
      return false;
    }
    memcpy(path, entry, entry_length);
    size_t offset = entry_length;
    if (add_separator) {
      path[offset++] = '\\';
    }
    memcpy(path + offset, file_name, file_name_length + 1);
    if (filesize(path) != -1) {
      *result = path;
      return true;
    }
    free(path);
  }
  return false;
}
static unsigned div_round_up(unsigned num, unsigned size) {
  return (num + size - 1) / size;
}
static int print_directory(const char *path) {
  struct finfo_block *entries;
  size_t count;
  if (list_directory(path, &entries, &count) != 0) {
    printf("Unable to list directory.\n");
    return 1;
  }
  for (size_t i = 0; i < count; i++) {
    if (entries[i].type == DIR) {
      int c = get_cons_color();
      set_cons_color(0x0a);
      printf("%s ", entries[i].name);
      set_cons_color(c);
    } else {
      printf("%s ", entries[i].name);
    }
  }
  printf("\n");
  free(entries);
  return 0;
}
static void print_memory_usage(void) {
  printf("Used/Total: %u/%u\n", mem_used(), div_round_up(mem_total(), 0x1000));
}

static void pause_shell(void) {
  printf("Press any key to continue. . .");
  getch();
  printf("\n");
}

static void list_modules(void) {
  module_handle_t modules[32];
  int count = module_list(modules, 32);
  if (count <= 0) {
    printf("No modules loaded.\n");
    return;
  }
  for (int i = 0; i < count && i < 32; i++) {
    printf("%u %s %s size=%u sections=%u exports=%u\n", modules[i].id,
           modules[i].name, modules[i].path, modules[i].image_size,
           modules[i].section_count, modules[i].export_count);
  }
}

struct simple_command {
  const char *name;
  void (*handler)(void);
};

static const struct simple_command simple_commands[] = {
    {"cls", clear},
    {"mem", print_memory_usage},
    {"pause", pause_shell},
    {"lsmod", list_modules},
};

static const char *const argument_commands[] = {
    "dir", "del", "cd", "mkfile", "type", "remount_drive",
    "color", "mkdir", "insmod", "rmmod", "format",
};

static int execute_external_command(int argc, char **argv, int *ok) {
  size_t name_length = strlen(argv[0]);
  if (name_length > UINT_MAX - sizeof(".bin")) {
    *ok = 0;
    return 0;
  }
  char *name = malloc(name_length + sizeof(".bin"));
  if (name == NULL) {
    *ok = 0;
    return 0;
  }
  memcpy(name, argv[0], name_length + 1);
  char *path = NULL;
  char *executable = name;
  if (filesize(name) == -1 && !find_in_search_path(name, &path)) {
    memcpy(name + name_length, ".bin", sizeof(".bin"));
    if (filesize(name) == -1 && !find_in_search_path(name, &path)) {
      free(name);
      *ok = 0;
      return 0;
    }
  }
  if (path != NULL) {
    executable = path;
  }
  char *command_line;
  size_t command_length;
  if (runtime_command_line_build(argc, argv, &command_line,
                                 &command_length) != 0) {
    free(path);
    free(name);
    *ok = 0;
    return 0;
  }
  (void)command_length;
  int result = exec(executable, command_line);
  free(command_line);
  free(path);
  free(name);
  *ok = 1;
  printf("\n");
  return result;
}

static int remount_drive(const char *argument) {
  char drive = argument[0];
  if (drive >= 'a' && drive <= 'z') {
    drive -= 'a' - 'A';
  }
  if (drive < 'A' || drive > 'Z' ||
      !((argument[1] == '\0') ||
        (argument[1] == ':' && argument[2] == '\0'))) {
    printf("remount_drive <drive>\n");
    return 1;
  }
  if (vfs_check_mount(drive) && !vfs_unmount_disk(drive)) {
    logkf("psh: remount_drive %c: unmount failed\n", drive);
    printf("Unable to unmount drive %c:.\n", drive);
    return 1;
  }
  if (!vfs_mount(drive, drive)) {
    logkf("psh: remount_drive %c: mount failed\n", drive);
    printf("Disk not ready!\n");
    return 1;
  }
  if (!vfs_change_disk(drive)) {
    logkf("psh: remount_drive %c: switch failed\n", drive);
    printf("Unable to switch to drive %c:.\n", drive);
    return 1;
  }
  logkf("psh: remount_drive %c: ok\n", drive);
  return 0;
}

static int run_command(int argc, char **argv) {
  int result = 0;
  if (argc == 0) {
    return 1;
  }
  if (strcmp("dir", argv[0]) == 0) {
    if (argc > 2) {
      printf("dir [directory]\n");
      return 1;
    }
    return print_directory(argc == 2 ? argv[1] : "");
  }
  for (unsigned i = 0; i < sizeof(simple_commands) / sizeof(simple_commands[0]);
       i++) {
    if (strcmp(simple_commands[i].name, argv[0]) == 0) {
      if (argc != 1) {
        printf("%s takes no arguments.\n", argv[0]);
        return 1;
      }
      simple_commands[i].handler();
      return 0;
    }
  }
  if (strcmp("del", argv[0]) == 0) {
    if (argc != 2 || !vfs_delfile(argv[1])) {
      printf("File not found.\n");
      return 1;
    }
  } else if (strcmp("cd", argv[0]) == 0) {
    if (argc != 2 || !vfs_change_path(argv[1])) {
      printf("Invalid path.\n");
      return 1;
    }
  } else if (strcmp("mkfile", argv[0]) == 0) {
    if (argc != 2 || !mkfile(argv[1])) {
      printf("Unable to create file.\n");
      return 1;
    }
  } else if (strcmp("type", argv[0]) == 0) {
    if (argc != 2) {
      printf("type <file>\n");
      return 1;
    }
    int file_size = filesize(argv[1]);
    if (file_size < 0) {
      printf("File not found.\n");
      return 1;
    }
    char *contents = file_size == 0 ? NULL : malloc((size_t)file_size);
    if ((file_size != 0 && contents == NULL) ||
        (file_size != 0 && !api_ReadFile(argv[1], contents))) {
      free(contents);
      printf("Unable to read file.\n");
      return 1;
    }
    for (int i = 0; i < file_size; i++) {
      printf("%c", contents[i]);
    }
    printf("\n");
    free(contents);
  } else if (strcmp("remount_drive", argv[0]) == 0) {
    return argc == 2 ? remount_drive(argv[1]) : 1;
  } else if (strcmp("color", argv[0]) == 0) {
    if (argc != 2) {
      return 1;
    }
    int c = strtol(argv[1], NULL, 16);
    T_DrawBox(0, 0, tty_get_xsize(), tty_get_ysize(), c);
    set_cons_color(c);
  } else if (strcmp("mkdir", argv[0]) == 0) {
    if (argc != 2 || !mkdir(argv[1])) {
      printf("Unable to create directory.\n");
      return 1;
    }
  } else if (strcmp("insmod", argv[0]) == 0) {
    if (argc != 2 || module_load(argv[1]) != 0) {
      printf("Module load failed.\n");
      return 1;
    }
  } else if (strcmp("rmmod", argv[0]) == 0) {
    if (argc != 2 || module_unload(argv[1]) != 0) {
      printf("Module unload failed.\n");
      return 1;
    }
  } else if (strcmp("format", argv[0]) == 0) {
    if (argc != 3) {
      printf("format <drive> <fsname>\n"
             "fsname can be FAT and PFS; the drive must be unmounted and "
             "have no retired references.\n");
      return 1;
    }
    char normalized_drive = toupper((unsigned char)argv[1][0]);
    if (normalized_drive < 'A' || normalized_drive > 'Z' ||
        !((argv[1][1] == '\0') ||
          (argv[1][1] == ':' && argv[1][2] == '\0'))) {
      printf("Invalid drive.\n");
      return 1;
    }
    if (!format(normalized_drive, argv[2])) {
      printf("Unable to format drive %c:. Ensure it is fully unmounted.\n",
             normalized_drive);
      return 1;
    }
  } else if (argc == 1 && strlen(argv[0]) == 2 && argv[0][1] == ':') {
    if (!vfs_check_mount(argv[0][0])) {
      if (!vfs_mount(argv[0][0], argv[0][0])) {
        printf("disk not ready!\n");
        return 1;
      }
    }
    if (!vfs_change_disk(argv[0][0])) {
      printf("Unable to switch drive.\n");
      return 1;
    }
  } else {
    int ok;
    result = execute_external_command(argc, argv, &ok);
    if (ok == 0) {
      printf("Command not found.\n");
      return 1;
    }
  }
  return result;
}

static void psh_readline_words(char *prefix, pl_readline_words_t words) {
  (void)prefix;
  for (size_t i = 0; i < sizeof(simple_commands) / sizeof(simple_commands[0]);
       i++) {
    if (pl_readline_word_maker_add(simple_commands[i].name, words, true,
                                   PL_COLOR_CYAN, ' ') != PL_READLINE_SUCCESS) {
      return;
    }
  }
  for (size_t i = 0;
       i < sizeof(argument_commands) / sizeof(argument_commands[0]); i++) {
    if (pl_readline_word_maker_add(argument_commands[i], words, true,
                                   PL_COLOR_CYAN, ' ') != PL_READLINE_SUCCESS) {
      return;
    }
  }
}

static int psh_readline_getch(void) {
  for (;;) {
    int input = getch();
    switch (input) {
    case KEY_INPUT_UP:
      return PL_READLINE_KEY_UP;
    case KEY_INPUT_DOWN:
      return PL_READLINE_KEY_DOWN;
    case KEY_INPUT_LEFT:
      return PL_READLINE_KEY_LEFT;
    case KEY_INPUT_RIGHT:
      return PL_READLINE_KEY_RIGHT;
    case '\n':
      return PL_READLINE_KEY_ENTER;
    case '\b':
      return PL_READLINE_KEY_BACKSPACE;
    case '\t':
      return PL_READLINE_KEY_TAB;
    default:
      break;
    }
    if (input >= ' ' && input <= UCHAR_MAX && input != 0x7f) {
      return input;
    }
  }
}

static int psh_readline_putch(int input) {
  putch((char)input);
  return input;
}

static void psh_readline_flush(void) {}

static bool run_shell(void) {
  pl_readline_t reader =
      pl_readline_init(psh_readline_getch, psh_readline_putch,
                       psh_readline_flush, psh_readline_words);
  if (reader == NULL) {
    return false;
  }

  printf("Plant OS 0.8a\n");
  for (;;) {
    char cwd[255];
    char prompt[sizeof(cwd) + sizeof("psh| ~ ")];
    api_getcwd(cwd);
    size_t cwd_length = strlen(cwd);
    size_t offset = 0;
    memcpy(prompt + offset, "psh|", sizeof("psh|") - 1);
    offset += sizeof("psh|") - 1;
    memcpy(prompt + offset, cwd, cwd_length);
    offset += cwd_length;
    memcpy(prompt + offset, " ~ ", sizeof(" ~ "));

    const char *line = pl_readline(reader, prompt);
    if (line == NULL) {
      printf("Unable to read command.\n");
      continue;
    }
    runtime_arguments_t arguments;
    if (runtime_arguments_parse(line, &arguments) == 0) {
      run_command(arguments.argc, arguments.argv);
      runtime_arguments_destroy(&arguments);
    } else {
      printf("Invalid command line.\n");
    }
  }
}
int main(int argc, char **argv) {
  if (argc > 1) {
    if (argc < 3 || strcmp(argv[1], "-c") != 0) {
      printf("Usage: %s -c <command> [arguments...]\n", argv[0]);
      return 1;
    }
    if (!env_init(1)) {
      return 1;
    }
    return run_command(argc - 2, argv + 2);
  }
  if (!env_init(0)) {
    return 1;
  }
  return run_shell() ? 0 : 1;
}
