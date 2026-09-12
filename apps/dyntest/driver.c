#include <elf.h>
#include <errno.h>
#include <ipc.h>
#include <pthread.h>
#include <runtime_args.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
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

static int reserved_memory(void) {
  const size_t page = VM_PAGE_SIZE;
  const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  char *base = mmap(NULL, 5 * page, PROT_NONE, flags, -1, 0);
  if (base == MAP_FAILED)
    return check(0, "reserve address space");
  int failures = check(mmap(base, page, PROT_READ, flags | MAP_FIXED_NOREPLACE,
                            -1, 0) == MAP_FAILED &&
                           errno == EEXIST,
                       "PROT_NONE remains occupied");
  for (unsigned access = 0; access < 3; access++) {
    int child = fork();
    if (!child) {
      if (access == 0)
        _exit(*(volatile char *)base);
      if (access == 1)
        *(volatile char *)base = 1;
      else
        ((void (*)(void))base)();
      _exit(0);
    }
    failures +=
        check(child > 0 && waittid(child) != 0,
              "reserved page rejects read, write and execution after fork");
  }
  char *data = base + page;
  if (mprotect(data, 3 * page, PROT_READ | PROT_WRITE)) {
    munmap(base, 5 * page);
    return failures + check(0, "commit reserved range");
  }
  memset(data, 42, 3 * page);
  failures +=
      check(mprotect(data, page, PROT_NONE) == 0, "protect committed contents");
  int child = fork();
  if (!child) {
    if (mprotect(data, page, PROT_READ | PROT_WRITE) || data[0] != 42)
      _exit(1);
    data[0] = 7;
    _exit(0);
  }
  failures += check(child > 0 && waittid(child) == 0 &&
                        mprotect(data, page, PROT_READ | PROT_WRITE) == 0 &&
                        data[0] == 42,
                    "hidden contents survive fork and remain private");
  child = fork();
  if (!child) {
    if (madvise(data, 2 * page, MADV_DONTNEED) || data[0] || data[page])
      _exit(1);
    data[0] = 9;
    _exit(0);
  }
  failures += check(child > 0 && waittid(child) == 0 && data[0] == 42 &&
                        data[page] == 42,
                    "discard does not modify fork parent's contents");
  failures +=
      check(mprotect(data, page, PROT_READ) == 0 &&
                madvise(data, page, MADV_DONTNEED) == 0 &&
                mprotect(data, page, PROT_READ | PROT_WRITE) == 0 && !data[0],
            "decommit retains reservation and recommits zero");
  failures +=
      check(mprotect(data, page, PROT_NONE) == 0 &&
                madvise(data, page, MADV_DONTNEED) == -1 && errno == EINVAL &&
                mprotect(data, page, PROT_READ | PROT_WRITE) == 0,
            "discard rejects pages without access permission");
  failures += check(mmap(data + page, page, PROT_READ | PROT_WRITE,
                         flags | MAP_FIXED, -1, 0) == data + page &&
                        data[page] == 0 && data[2 * page] == 42,
                    "fixed replacement clears only selected pages");
  failures += check(munmap(data + page, page) == 0 &&
                        mprotect(data, 3 * page, PROT_NONE) == -1,
                    "protection rejects holes without changing earlier pages");
  data[0] = 13;
  failures +=
      check(mmap(data, 3 * page, PROT_NONE, flags | MAP_FIXED, -1, 0) == data &&
                mprotect(data, 3 * page, PROT_READ | PROT_WRITE) == 0 &&
                !data[0] && !data[page] && !data[2 * page],
            "fixed replacement spans mapped pages and holes");
  failures +=
      check(munmap(data + page, page) == 0 && munmap(base, 5 * page) == 0,
            "release mixed protected pages and holes");
  failures += check(
      mmap(NULL, (size_t)-1, PROT_NONE, flags, -1, 0) == MAP_FAILED &&
          errno == EINVAL &&
          vm_map_aligned(NULL, page, 3 * page, 0, 0) == NULL && errno == EINVAL,
      "reject overflow and non-power-of-two alignment");
  const size_t alignment = 2 * 1024 * 1024;
  size_t large = sizeof(uintptr_t) == 8 ? (size_t)1024 * 1024 * 1024 * 4
                                        : 64 * 1024 * 1024;
  long before = sysconf(_SC_AVPHYS_PAGES);
  base = vm_map_aligned(NULL, large, alignment, 0, 0);
  if (!base)
    return failures + check(0, "large aligned reservation");
  long after = sysconf(_SC_AVPHYS_PAGES);
  failures += check(
      !((uintptr_t)base & (alignment - 1)) &&
          before - after < (long)(large / page / 32),
      "reservation uses page tables, not one physical page per virtual page");
  failures +=
      check(mprotect(base + large - page, page, PROT_READ | PROT_WRITE) == 0,
            "commit far end of large reservation");
  base[large - 1] = 1;
  failures += check(munmap(base, large) == 0, "release large reservation");
  if (!failures)
    logk("DYNTEST MMAN PASS\n");
  return failures;
}

static int file_mappings(void) {
  const size_t page = VM_PAGE_SIZE, length = 3 * VM_PAGE_SIZE + 17;
  unsigned char *seed = malloc(length), *readback = malloc(length);
  if (!seed || !readback) {
    free(seed);
    free(readback);
    return check(0, "file mapping fixture allocation");
  }
  for (size_t i = 0; i < length; i++)
    seed[i] = (unsigned char)(i * 17 + 31);
  int fd = open("/mapped.dat", O_CREAT | O_TRUNC | O_RDWR, 0600);
  if (fd < 0 || write(fd, seed, length) != (ssize_t)length) {
    if (fd >= 0)
      close(fd);
    free(seed);
    free(readback);
    return check(0, "file mapping fixture write");
  }
  unsigned char *shared =
      mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  unsigned char *alias = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
  unsigned char *private =
      mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  int failures = check(shared != MAP_FAILED && alias != MAP_FAILED &&
                           private != MAP_FAILED,
                       "shared and private file mappings");
  if (failures)
    goto done;
  failures += check(
      lseek(fd, 0, SEEK_CUR) == (off_t)length &&
          !memcmp(shared, seed, length) && !shared[length] &&
          !shared[4 * page - 1],
      "file mapping preserves descriptor offset and zeroes final page tail");
  private[0] = 77;
  failures += check(shared[0] == seed[0] && alias[0] == seed[0],
                    "private writes use COW");
  shared[0] = 91;
  shared[2 * page] = 92;
  failures +=
      check(alias[0] == 91 && alias[2 * page] == 92 && private[0] == 77,
            "shared aliases are coherent and private copies stay isolated");
  failures += check(lseek(fd, 0, SEEK_SET) == 0 &&
                        read(fd, readback, length) == (ssize_t)length &&
                        readback[0] == 91 && readback[2 * page] == 92,
                    "ordinary reads observe mapped writes across a bulk read");
  unsigned char byte = 93;
  failures += check(lseek(fd, page, SEEK_SET) == (off_t)page &&
                        write(fd, &byte, 1) == 1 && shared[page] == 93 &&
                        alias[page] == 93,
                    "ordinary writes update mapped cache pages");
  failures +=
      check(madvise(private, page, MADV_DONTNEED) == 0 && private[0] == 91,
            "discard private file contents reloads file backing");
  int child = fork();
  if (!child) {
    shared[1] = 94;
    private[1] = 95;
    if (msync(shared, page, MS_SYNC))
      _exit(1);
    _exit(0);
  }
  failures += check(child > 0 && waittid(child) == 0 && shared[1] == 94 &&
                        private[1] != 95,
                    "fork retains shared writes and private COW");
  failures += check(mprotect(private, page, PROT_READ) == 0 &&
                        madvise(private, page, MADV_DONTNEED) == 0 &&
                        mprotect(private, page, PROT_READ | PROT_WRITE) == 0 &&
                        private[1] == 94,
                    "file-backed protection and discard");
  failures += check(mprotect(private, page, PROT_NONE) == 0 &&
                        madvise(private, page, MADV_DONTNEED) == -1 &&
                        errno == EINVAL &&
                        mprotect(private, page, PROT_READ | PROT_WRITE) == 0 &&
                        private[1] == 94,
                    "discard rejects unreadable file pages");
  failures += check(mprotect(private + page, page, PROT_READ) == 0 &&
                        madvise(private, 2 * page, MADV_DONTNEED) == 0 &&
                        private[0] == 91 && private[page] == 93,
                    "discard reloads file pages with mixed protection");
  child = fork();
  if (!child) {
    *(volatile unsigned char *)(private + page) = 1;
    _exit(0);
  }
  failures += check(child > 0 && waittid(child) != 0,
                    "discard preserves the second file page's read-only permission");
  failures += check(mprotect(private + page, page, PROT_READ | PROT_WRITE) == 0,
                    "restore private file page permission");
  unsigned char *mixed = mmap(NULL, 3 * page, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mixed != MAP_FAILED) {
    mixed[0] = 7;
    mixed[2 * page] = 8;
    bool mapped = mmap(mixed + page, page, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_FIXED, fd, page) == mixed + page;
    failures += check(mapped, "file page between anonymous pages");
    if (mapped) {
      mixed[page] = 9;
      failures += check(madvise(mixed, 3 * page, MADV_DONTNEED) == 0 &&
                            !mixed[0] && mixed[page] == 93 && !mixed[2 * page],
                        "discard crosses anonymous and private file boundaries");
    }
    failures += check(munmap(mixed, 3 * page) == 0, "release mixed mappings");
  } else {
    failures += check(0, "mixed mapping allocation");
  }
  int readonly = open("/mapped.dat", O_RDONLY);
  unsigned char *ro = mmap(NULL, page, PROT_READ, MAP_SHARED, readonly, 0);
  failures += check(
      ro != MAP_FAILED && mprotect(ro, page, PROT_READ | PROT_WRITE) == -1 &&
          errno == EACCES &&
          mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, readonly, 0) ==
              MAP_FAILED &&
          errno == EACCES,
      "read-only descriptor cannot acquire shared write permission");
  if (ro != MAP_FAILED)
    munmap(ro, page);
  close(readonly);
  failures += check(ftruncate(fd, page) == -1 && errno == EBUSY,
                    "mapped file truncation is explicitly rejected");
  failures += check(
      mmap(NULL, page, PROT_READ, MAP_PRIVATE, fd, 4 * page) == MAP_FAILED &&
          errno == ENOTSUP &&
          mmap(NULL, page, PROT_READ, MAP_PRIVATE, fd, 1) == MAP_FAILED &&
          errno == EINVAL,
      "reject beyond-EOF and unaligned file offsets");
  failures += check(msync(shared, length, MS_SYNC) == 0 && fsync(fd) == 0,
                    "file mapping synchronous writeback");
  failures += check(munmap(alias + page, page) == 0 &&
                        mmap(alias + 2 * page, page, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1,
                             0) == alias + 2 * page &&
                        alias[0] == 91,
                    "split and partially replace file extents");
  close(fd);
  fd = -1;
  failures +=
      check(unlink("/mapped.dat") == -1 && errno == EBUSY && shared[0] == 91,
            "mappings retain file after descriptor close");
  child = fork();
  if (!child)
    _exit(exec("/dynempty.bin", "dynempty.bin"));
  failures += check(child > 0 && waittid(child) == 0,
                    "exec releases inherited file mappings");
done:
  if (shared != MAP_FAILED)
    failures +=
        check(munmap(shared, length) == 0, "release shared file mapping");
  if (alias != MAP_FAILED)
    failures +=
        check(munmap(alias, length) == 0, "release split file mappings");
  if (private != MAP_FAILED)
    failures +=
        check(munmap(private, length) == 0, "release private file mapping");
  if (fd >= 0)
    close(fd);
  fd = open("/mapped.dat", O_RDONLY);
  failures += check(fd >= 0 && read(fd, readback, length) == (ssize_t)length &&
                        readback[0] == 91 && readback[1] == 94 &&
                        readback[page] == 93 && readback[2 * page] == 92,
                    "file retains synchronized contents");
  if (fd >= 0)
    close(fd);
  failures +=
      check(unlink("/mapped.dat") == 0, "last unmap releases file lifetime");
  free(seed);
  free(readback);
  if (!failures)
    logk("DYNTEST FILEMAP PASS\n");
  return failures;
}

/* Every worker hammers the descriptor-crossing VM paths at once: anonymous
 * reserve/commit/discard against one shared file mapping, with a private
 * descriptor exercising ordinary reads while the mapping is remapped. Four
 * workers on four CPUs contend for the VM lock, so a lost wakeup shows up as a
 * stuck join rather than as a wrong value. */
typedef struct {
  pthread_barrier_t *start;
  int descriptor;
  void *file;
  void *area_slot;
  void *alias_slot;
  unsigned index, rounds;
} vm_race_t;

enum { WORKER_COUNT = 4, WORKER_INDEX_LIMIT = WORKER_COUNT, SLOT_PAGES = 10 };

static void *vm_race_worker(void *argument) {
  vm_race_t *race = argument;
  const size_t page = VM_PAGE_SIZE;
  /* A private descriptor keeps the shared file offset out of the race. */
  int reader = open("/vmmap.dat", O_RDONLY);
  unsigned char buffer[64];
  pthread_barrier_wait(race->start);
  if (reader < 0)
    return (void *)1;
  for (unsigned round = 0; round < race->rounds; round++) {
    char *area = mmap(race->area_slot, 8 * page, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (area == MAP_FAILED)
      return (void *)2;
    if (mprotect(area, 6 * page, PROT_READ | PROT_WRITE)) {
      munmap(area, 8 * page);
      return (void *)30;
    }
    area[0] = 1;
    if (area[2 * page] || madvise(area + 2 * page, page, MADV_DONTNEED) ||
        area[2 * page] || mprotect(area + 2 * page, page, PROT_READ) ||
        munmap(area + 4 * page, page) || munmap(area, 8 * page)) {
      munmap(area, 8 * page);
      return (void *)31;
    }
    unsigned char *alias = mmap(race->alias_slot, page, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_FIXED, race->descriptor, 0);
    if (alias == MAP_FAILED)
      return (void *)40;
    alias[1] = (unsigned char)race->index;
    /* Peers overwrite the same byte, so only its domain can be asserted; the
     * coherent read-back is checked single threaded in file_mappings(). */
    int sync_status = msync(alias, page, MS_SYNC);
    int unmap_status = sync_status ? -1 : munmap(alias, page);
    off_t seek_status = unmap_status ? -1 : lseek(reader, 0, SEEK_SET);
    ssize_t read_status =
        seek_status < 0 ? -1 : read(reader, buffer, sizeof(buffer));
    if (sync_status || unmap_status || seek_status != 0 ||
        read_status != (ssize_t)sizeof(buffer) ||
        buffer[1] >= WORKER_INDEX_LIMIT) {
      return (void *)50;
    }
    if (mprotect(race->file, page, PROT_READ) ||
        madvise(race->file, page, MADV_DONTNEED) ||
        mprotect(race->file, page, PROT_READ | PROT_WRITE) ||
        ((unsigned char *)race->file)[1] >= WORKER_INDEX_LIMIT)
      return (void *)60;
  }
  close(reader);
  return NULL;
}

static int concurrent_mappings(void) {
  const size_t page = VM_PAGE_SIZE;
  int failures = 0;
  int descriptor = open("/vmmap.dat", O_CREAT | O_TRUNC | O_RDWR, 0600);
  unsigned char seed[4 * VM_PAGE_SIZE];
  for (size_t i = 0; i < sizeof(seed); i++)
    seed[i] = (unsigned char)i;
  if (descriptor < 0 ||
      write(descriptor, seed, sizeof(seed)) != (ssize_t)sizeof(seed)) {
    if (descriptor >= 0)
      close(descriptor);
    return check(0, "concurrent mapping fixture");
  }
  unsigned char *file =
      mmap(NULL, 4 * page, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
  if (file == MAP_FAILED) {
    close(descriptor);
    return check(0, "concurrent mapping fixture map");
  }
  enum { WORKERS = WORKER_COUNT };
  void *arena = mmap(NULL, WORKERS * SLOT_PAGES * page, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (arena == MAP_FAILED) {
    munmap(file, 4 * page);
    close(descriptor);
    return check(0, "concurrent mapping fixture arena");
  }
  pthread_barrier_t start;
  if (pthread_barrier_init(&start, NULL, WORKERS)) {
    munmap(arena, WORKERS * SLOT_PAGES * page);
    munmap(file, 4 * page);
    close(descriptor);
    return check(0, "concurrent mapping barrier");
  }
  pthread_t threads[WORKERS];
  vm_race_t races[WORKERS];
  unsigned created = 0;
  for (; created < WORKERS; created++) {
    uintptr_t slot = (uintptr_t)arena + created * SLOT_PAGES * page;
    races[created] = (vm_race_t){
        &start,  descriptor, file, (void *)slot, (void *)(slot + 8 * page),
        created, 60};
    if (pthread_create(&threads[created], NULL, vm_race_worker,
                       &races[created]))
      break;
  }
  failures += check(created == WORKERS, "concurrent mapping workers");
  if (created == WORKERS) {
    for (unsigned i = 0; i < WORKERS; i++) {
      void *result = (void *)1;
      int joined = pthread_join(threads[i], &result);
      if (joined || result)
        failures += check(0, "concurrent mapping worker");
    }
  }
  pthread_barrier_destroy(&start);
  failures += check(munmap(arena, WORKERS * SLOT_PAGES * page) == 0,
                    "concurrent mapping arena teardown");
  failures += check(munmap(file, 4 * page) == 0 && close(descriptor) == 0 &&
                        unlink("/vmmap.dat") == 0,
                    "concurrent mapping teardown");
  if (!failures)
    logk("DYNTEST THREADS PASS\n");
  return failures;
}

int main(int argc, char **argv) {
  int failures = cached_translations() + reserved_memory() + file_mappings();

  failures += concurrent_mappings();
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
