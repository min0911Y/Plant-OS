#include <elf.h>
#include <errno.h>
#include <ipc.h>
#include <runtime_args.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <vm.h>

static int check(int condition, const char *name) {
  if (condition)
    return 0;
  printf("DYNTEST FAIL: %s\n", name);
  logkf("DYNTEST FAIL: %s\n", name);
  return 1;
}

static int malformed(char *image, size_t size, int expected, const char *name) {
  FILE *output = fopen("/badelf.bin", "wb");
  if (!output)
    return check(0, name);
  int written = fwrite(image, 1, size, output) == size;
  int closed = fclose(output) == 0;
  int result = written && closed ? exec("/badelf.bin", "badelf.bin") : 0;
  remove("/badelf.bin");
  return check(written && closed && result == expected, name);
}

static void map_from_thread(uintptr_t parent) {
  char *mapping = vm_map(NULL, VM_PAGE_SIZE);
  if (mapping)
    mapping[0] = 42;
  ipc_send_to(parent, 0, 0, &mapping, sizeof(mapping), 5000);
  _exit(0);
}

static int cached_translations(void) {
  enum { PAGE_COUNT = 64, ROUNDS = 16, LENGTH = PAGE_COUNT * VM_PAGE_SIZE };
  volatile unsigned *pages = vm_map(NULL, LENGTH);
  if (!pages)
    return check(0, "TLB test allocation");
  for (unsigned i = 0; i < PAGE_COUNT; i++)
    pages[i * VM_PAGE_SIZE / sizeof(*pages)] = i + 1;
  unsigned parent = NowTaskID();
  int child = fork();
  ipc_msg_t message;
  if (!child) {
    for (unsigned round = 1; round <= ROUNDS; round++) {
      unsigned received = 0;
      if (ipc_recv_from(parent, &received, sizeof(received), &message, 5000) !=
              sizeof(received) ||
          received != round)
        _exit(1);
      /* The parent wrote its warm writable pages after fork, then woke us. */
      for (unsigned i = 0; i < PAGE_COUNT; i++) {
        unsigned expected = round == 1 ? i + 1 : (round - 1) * 256 + i;
        if (pages[i * VM_PAGE_SIZE / sizeof(*pages)] != expected)
          _exit(2);
      }
      if (vm_unmap((void *)pages, LENGTH) ||
          vm_map((void *)pages, LENGTH) != (void *)pages)
        _exit(3);
      for (unsigned i = 0; i < PAGE_COUNT; i++) {
        if (pages[i * VM_PAGE_SIZE / sizeof(*pages)])
          _exit(4);
        pages[i * VM_PAGE_SIZE / sizeof(*pages)] = round * 256 + i;
      }
      /* Exercise a whole-context invalidation as well as address reuse. */
      if (vm_protect((void *)pages, LENGTH, VM_READ) ||
          vm_protect((void *)pages, LENGTH, VM_READ | VM_WRITE) ||
          ipc_send_to(parent, 0, 0, &round, sizeof(round), 5000) != IPC_OK)
        _exit(5);
    }
    _exit(0);
  }
  int failures = child < 0;
  for (unsigned round = 1; child > 0 && round <= ROUNDS; round++) {
    for (unsigned i = 0; i < PAGE_COUNT; i++)
      pages[i * VM_PAGE_SIZE / sizeof(*pages)] = 0x80000000u + round * 256 + i;
    unsigned received = 0;
    if (ipc_send_to(child, 0, 0, &round, sizeof(round), 5000) != IPC_OK ||
        ipc_recv_from(child, &received, sizeof(received), &message, 5000) !=
            sizeof(received) ||
        received != round) {
      failures++;
      break;
    }
    for (unsigned i = 0; i < PAGE_COUNT; i++)
      failures += pages[i * VM_PAGE_SIZE / sizeof(*pages)] !=
                  0x80000000u + round * 256 + i;
  }
  if (child > 0)
    failures += waittid(child) != 0;
  failures += vm_unmap((void *)pages, LENGTH) != 0;
  if (!failures)
    logk("DYNTEST TLB PASS\n");
  return check(!failures, "cached translations, parent COW and address reuse");
}

int main(int argc, char **argv) {
  int failures = cached_translations();
  char *pages = vm_map(NULL, 2 * VM_PAGE_SIZE);
  if (!pages)
    return 1;
  for (size_t i = 0; i < 2 * VM_PAGE_SIZE; i++) {
    if (pages[i]) {
      failures++;
      break;
    }
  }
  pages[0] = 42;
  failures += check(vm_map(pages, VM_PAGE_SIZE) == NULL && errno == EEXIST,
                    "map collision");
  failures +=
      check(vm_protect(pages, VM_PAGE_SIZE, VM_READ | VM_WRITE | VM_EXEC) == -1,
            "reject write+execute");
  failures +=
      check(vm_protect(pages, VM_PAGE_SIZE, VM_READ) == 0, "read-only mapping");
  int child = fork();
  if (!child) {
    volatile char *read_only = pages;
    *read_only = 7;
    _exit(0);
  }
  failures += check(child > 0 && waittid(child) != 0 && pages[0] == 42,
                    "read-only survives fork");
  failures += check(vm_protect(pages, VM_PAGE_SIZE, VM_READ | VM_WRITE) == 0,
                    "restore write permission");
  unsigned char code[] = {0xb8, 42, 0, 0, 0, 0xc3};
  memcpy(pages, code, sizeof(code));
  failures += check(vm_protect(pages, VM_PAGE_SIZE, VM_READ | VM_EXEC) == 0 &&
                        ((int (*)(void))pages)() == 42,
                    "executable mapping");
  failures += check(vm_unmap(pages, 2 * VM_PAGE_SIZE) == 0, "unmap");
  void *thread_stack = malloc(32 * 1024);
  if (!thread_stack)
    return 1;
  int tid = AddThread("vm-test", (uintptr_t)map_from_thread,
                      (uintptr_t)thread_stack + 32 * 1024, NowTaskID());
  char *mapping = NULL;
  ipc_msg_t message;
  int received = tid > 0 && ipc_recv_from(tid, &mapping, sizeof(mapping),
                                          &message, 5000) == sizeof(mapping);
  if (tid > 0)
    SubThread(tid);
  failures += check(received && mapping && mapping[0] == 42 &&
                        vm_unmap(mapping, VM_PAGE_SIZE) == 0,
                    "mapping survives creator thread exit");
  free(thread_stack);

  failures +=
      check(exec("/dynmain.bin", "\"custom argv0\" \"hello world\" \"\"") == 0,
            "PIE, dependencies, relocations, constructors, argv, fork");
  failures +=
      check(exec("/dynbad.bin", "dynbad.bin") == 127, "unresolved symbol");
  failures +=
      check(exec("/dyncopy.bin", "dyncopy.bin") == 0, "data relocations");
  failures += check(exec("/dynempty.bin", "dynempty.bin") == 0,
                    "GNU hash with only imports");
  char cwd[64];
  failures += check(system("cd /lib") == 0 && getcwd(cwd, sizeof(cwd)) &&
                        !strcasecmp(cwd, "/lib"),
                    "system command returns cwd");
  failures += check(system("cd /") == 0, "restore working directory");
  failures += check(exec("/psh.bin", "psh.bin -c cd /lib") == 0 &&
                        getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/"),
                    "ordinary child keeps cwd private");
  int source_drive = api_current_drive();
  bool devfs_mounted = vfs_check_mount('B') > 0;
  if ((devfs_mounted || vfs_mount('B', 'B')) && vfs_change_disk('B')) {
    char executable[] = "?:/dynempty.bin";
    executable[0] = source_drive;
    failures +=
        check(exec(executable, "dynempty.bin") == 0 && system("mem") == 0,
              "system interpreter and shell across drives");
    if (!vfs_change_disk(source_drive))
      return 1;
    if (!devfs_mounted)
      failures += check(vfs_unmount_disk('B'), "release test mount");
  } else {
    failures += check(0, "mount test drive");
  }
  if (rename("/lib/libbase.so", "/lib/base.tmp") == 0) {
    int result = exec("/dynmain.bin", "dynmain.bin");
    int restored = rename("/lib/base.tmp", "/lib/libbase.so");
    failures += check(result == 127 && restored == 0, "missing dependency");
  } else {
    failures++;
  }

  FILE *input = fopen("/dynmain.bin", "rb");
  if (!input)
    return 1;
  fseek(input, 0, SEEK_END);
  long size = ftell(input);
  rewind(input);
  char *image = malloc(size);
  if (!image || fread(image, 1, size, input) != size)
    return 1;
  fclose(input);
  Elf_Ehdr *header = (void *)image;
  Elf_Phdr *segments = (void *)(image + header->e_phoff);
  for (size_t i = 0; i < header->e_phnum; i++) {
    if (segments[i].p_type == PT_LOAD) {
      Elf_Phdr saved = segments[i];
      segments[i].p_filesz = segments[i].p_memsz + 1;
      failures += malformed(image, size, 127, "malformed load segment");
      segments[i] = saved;
      segments[i].p_flags = PF_R | PF_W | PF_X;
      failures += malformed(image, size, 127, "write+execute segment");
      segments[i] = saved;
      segments[i].p_memsz = (uintptr_t)-1;
      failures += malformed(image, size, 127, "segment overflow");
      segments[i] = saved;
      segments[i].p_type = PT_TLS;
      failures +=
          malformed(image, size, 127, "TLS cannot replace a load segment");
      segments[i] = saved;
      break;
    }
  }
  Elf_Ehdr saved_header = *header;
  header->e_machine = 0;
  failures += malformed(image, size, -1, "foreign machine");
  *header = saved_header;
  header->e_phoff = size;
  failures += malformed(image, size, -1, "truncated program headers");
  *header = saved_header;
  for (size_t i = 0; i < header->e_phnum; i++) {
    if (segments[i].p_type != PT_DYNAMIC)
      continue;
    Elf_Dyn *dynamic = (void *)(image + segments[i].p_offset);
    for (size_t j = 0; dynamic[j].d_tag != DT_NULL; j++) {
      Elf_Dyn saved = dynamic[j];
      if (saved.d_tag == DT_STRTAB) {
        dynamic[j].d_un.d_ptr = (uintptr_t)-1;
        failures += malformed(image, size, 127, "invalid string table");
      } else if (saved.d_tag == DT_RELSZ || saved.d_tag == DT_RELASZ) {
        dynamic[j].d_un.d_val++;
        failures += malformed(image, size, 127, "truncated relocation");
      } else if (saved.d_tag == DT_FLAGS) {
        dynamic[j].d_un.d_val |= DF_TEXTREL;
        failures += malformed(image, size, 127, "unsupported text relocation");
      } else if (saved.d_tag == DT_SYMENT) {
        dynamic[j].d_un.d_val++;
        failures += malformed(image, size, 127, "invalid symbol entry size");
      }
      dynamic[j] = saved;
    }
  }
  free(image);
  if (argc == 2 && !strcmp(argv[1], "--all")) {
    FILE *manifest = fopen("/apps.lst", "rb");
    if (!manifest || fseek(manifest, 0, SEEK_END))
      return 1;
    long length = ftell(manifest);
    char *paths = length > 0 ? malloc((size_t)length + 1) : NULL;
    if (!paths || fseek(manifest, 0, SEEK_SET) ||
        fread(paths, 1, length, manifest) != length)
      return 1;
    fclose(manifest);
    paths[length] = 0;
    unsigned count = 0;
    for (char *path = paths; *path;) {
      char *next = strchr(path, '\n');
      if (next)
        *next++ = 0;
      char *arguments[] = {"/lib/ld.so", "--verify", path};
      char *line;
      size_t line_size;
      if (runtime_command_line_build(3, arguments, &line, &line_size))
        return 1;
      int status = exec(arguments[0], line);
      free(line);
      int expected = !strcasecmp(path, "/DYNBAD.BIN") ? 127 : 0;
      logkf("DYNAPPS %s status=%d\n", path, status);
      failures += check(status == expected, path);
      count++;
      if (!next)
        break;
      path = next;
    }
    free(paths);
    if (!failures)
      logkf("DYNAPPS PASS count=%u\n", count);
  }
  if (!failures) {
    puts("DYNTEST PASS");
    logk("DYNTEST PASS\n");
  }
  return failures != 0;
}
