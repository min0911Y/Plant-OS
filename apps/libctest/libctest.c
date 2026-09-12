#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

static unsigned failures;
static void check(int valid, const char *name) {
  if (!valid) {
    failures++;
    logkf("LIBCTEST FAIL: %s\n", name);
  }
}
static void memory_copy(void) {
  unsigned char source[192], destination[192];
  for (size_t i = 0; i < sizeof(source); i++)
    source[i] = (unsigned char)(i * 37);
  for (unsigned from = 0; from < 16; from++) {
    for (unsigned to = 0; to < 16; to++) {
      for (size_t size = 0; size <= 160; size++) {
        memset(destination, 0xa5, sizeof(destination));
        bool valid = memcpy(destination + to, source + from, size) ==
                     destination + to;
        for (size_t i = 0; i < sizeof(destination); i++)
          valid &= destination[i] ==
                   (i >= to && i - to < size ? source[from + i - to] : 0xa5);
        if (!valid) {
          check(false, "memcpy alignment, tails and destination bounds");
          return;
        }
      }
    }
  }
  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  unsigned char *mapping = mmap(NULL, 4 * page, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  check(mapping != MAP_FAILED, "memcpy guard mapping");
  if (mapping == MAP_FAILED)
    return;
  bool guards[2] = {mprotect(mapping + page, page, PROT_NONE) == 0,
                    mprotect(mapping + 3 * page, page, PROT_NONE) == 0};
  if (guards[0] && guards[1]) {
    for (size_t i = 0; i < page; i++)
      mapping[i] = (unsigned char)(i * 37);
    for (size_t size = 0; size <= page; size++) {
      unsigned char *input = mapping + page - size;
      unsigned char *output = mapping + 3 * page - size;
      check(memcpy(output, input, size) == output &&
                memcmp(output, input, size) == 0,
            "memcpy does not cross a guard page");
    }
  } else {
    check(false, "memcpy guard protection");
  }
  check(munmap(mapping, 4 * page) == 0, "memcpy guard release");
}
static void allocations(void) {
  void *blocks[17] = {0};
  for (unsigned i = 0; i < 17; i++) {
    size_t alignment = (size_t)1 << i;
    size_t size = alignment * 3;
    blocks[i] = aligned_alloc(alignment, size);
    check(blocks[i] && !((uintptr_t)blocks[i] & (alignment - 1)),
          "aligned allocation");
    if (blocks[i])
      memset(blocks[i], 37 + i, size);
  }
  for (unsigned parity = 0; parity < 2; parity++) {
    for (unsigned i = parity; i < 17; i += 2) {
      size_t size = ((size_t)1 << i) * 3;
      unsigned char *bytes = blocks[i];
      if (!bytes)
        continue;
      bool valid = true;
      for (size_t j = 0; j < size; j++)
        valid &= bytes[j] == 37 + i;
      check(valid, "aligned allocations do not overlap");
      free(bytes);
    }
  }
  for (unsigned i = 0; i < 80; i++) {
    void *pointer = NULL;
    check(!posix_memalign(&pointer, 256, 4097) && pointer, "POSIX alignment");
    if (pointer) {
      memset(pointer, 0x5a, 4097);
      void *grown = realloc(pointer, 8192);
      check(grown && ((unsigned char *)grown)[4096] == 0x5a,
            "realloc accepts aligned allocations");
      free(grown ? grown : pointer);
    }
  }
  void *unchanged = (void *)1;
  check(posix_memalign(&unchanged, 3, 32) == EINVAL && unchanged == (void *)1,
        "invalid alignment preserves output");
  check(!aligned_alloc(32, 33) && errno == EINVAL,
        "aligned_alloc size multiple");
  check(!calloc(SIZE_MAX, 2) && errno == ENOMEM, "calloc overflow");
  check(!malloc(SIZE_MAX) && errno == ENOMEM, "malloc rounding overflow");
}

static void numbers(void) {
  char formatted[64];
  intmax_t signed_value = 0;
  uintmax_t unsigned_value = 0;
  snprintf(formatted, sizeof(formatted), "%" PRIdMAX " %" PRIuMAX, INTMAX_MIN,
           UINTMAX_MAX);
  check(sscanf(formatted, "%" SCNdMAX " %" SCNuMAX, &signed_value,
               &unsigned_value) == 2 &&
            signed_value == INTMAX_MIN && unsigned_value == UINTMAX_MAX,
        "intmax formatting and scanning");
  char *end;
  check(strtod("  -0x1.8p+1tail", &end) == -3.0 && !strcmp(end, "tail"),
        "hexadecimal floating-point syntax");
  check(strtod("1e+tail", &end) == 1.0 && !strcmp(end, "e+tail"),
        "incomplete exponent rollback");
  const char *invalid = "  +oops";
  check(strtod(invalid, &end) == 0.0 && end == invalid,
        "no conversion pointer");
  check(strtod("0.1", &end) == 0.1 && !*end, "decimal rounding");
  check(strtof("0x1p-149", &end) == FLT_TRUE_MIN && !*end, "float subnormal");
  errno = 0;
  check(__builtin_isinf(strtod("1e99999", &end)) && !*end && errno == ERANGE,
        "floating-point overflow");
  errno = 0;
  check(strtod("1e-99999", &end) == 0.0 && !*end && errno == ERANGE,
        "floating-point underflow");
  check(__builtin_isnan(strtod("nan(payload)", &end)) && !*end,
        "NaN payload syntax");
  for (unsigned i = 0; i < 64; i++) {
    long double number = strtold("0x1.0000000000000002p0", &end);
    check(number == 1.0L + 0x1p-63L && !*end, "native long double precision");
  }
#if LDBL_MAX_EXP > DBL_MAX_EXP
  long double large = strtold("1e4000", &end);
  check(large > 0 && !__builtin_isinf(large) && !*end, "x87 exponent range");
#endif
}

static void tokenization(void) {
  char first[] = ",alpha,,beta;gamma", second[] = "/one/two/";
  char *a = NULL, *b = NULL;
  char *token = strtok_r(first, ",", &a);
  check(token && !strcmp(token, "alpha"), "strtok_r leading delimiters");
  token = strtok_r(second, "/", &b);
  check(token && !strcmp(token, "one"), "strtok_r independent state");
  token = strtok_r(NULL, ",;", &a);
  check(token && !strcmp(token, "beta"), "strtok_r changed delimiters");
  token = strtok_r(NULL, "", &a);
  check(token && !strcmp(token, "gamma") && !strtok_r(NULL, "", &a),
        "strtok_r empty delimiter set and end");
  token = strtok_r(NULL, "/", &b);
  check(token && !strcmp(token, "two") && !strtok_r(NULL, "/", &b),
        "strtok_r trailing delimiters");
  char legacy[] = "a:b";
  token = strtok(legacy, ":");
  check(token && !strcmp(token, "a"), "strtok first token");
  token = strtok(NULL, ":");
  check(token && !strcmp(token, "b") && !strtok(NULL, ":"),
        "strtok continuation");
}

static void times_and_locale(void) {
  struct tm tm = {.tm_year = 70, .tm_mon = 0, .tm_mday = 1};
  check(timegm(&tm) == 0, "UTC epoch");
  tm = (struct tm){.tm_year = 70,
                   .tm_mon = 0,
                   .tm_mday = 1,
                   .tm_hour = 7,
                   .tm_min = 59,
                   .tm_sec = 59};
  check(mktime(&tm) == -1 && timegm(&tm) == 28799,
        "valid negative local timestamp");
  time_t negative = -1;
  check(gmtime_r(&negative, &tm) && tm.tm_year == 69 && tm.tm_mon == 11 &&
            tm.tm_mday == 31 && tm.tm_hour == 23 && tm.tm_sec == 59,
        "negative UTC timestamp");
  tm = (struct tm){.tm_year = 207, .tm_mon = 0, .tm_mday = 1};
  check(timegm(&tm) > UINT32_MAX, "64-bit calendar");
  struct timespec start, finish, duration = {0, 20000000};
  clock_gettime(CLOCK_MONOTONIC, &start);
  while (nanosleep(&duration, &duration) && errno == EINTR) {
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  int64_t elapsed = (finish.tv_sec - start.tv_sec) * 1000000000ll +
                    finish.tv_nsec - start.tv_nsec;
  check(elapsed >= 20000000, "nanosleep deadline");
  locale_t locale = newlocale(LC_ALL_MASK, "C.UTF-8", 0);
  locale_t previous = uselocale(locale);
  check(locale && previous && MB_CUR_MAX == 4, "thread locale");
  uselocale(previous);
  check(MB_CUR_MAX == 1, "restore C locale");
  freelocale(locale);
  check(!newlocale(LC_ALL_MASK, "unavailable", 0) && errno == ENOENT,
        "unsupported locale fails");
  char *text = NULL;
  check(asprintf(&text, "value=%d %s", 42, "ok") == 11 && text &&
            !strcmp(text, "value=42 ok"),
        "allocated formatting");
  free(text);
}

struct stream_writer {
  FILE *stream;
  unsigned index;
};

static void *write_stream(void *argument) {
  struct stream_writer *writer = argument;
  for (unsigned sequence = 0; sequence < 1000; sequence++) {
    if (fprintf(writer->stream, "%c%04u\n", 'A' + writer->index, sequence) != 6)
      return (void *)1;
    if (!(sequence % 127) && fflush(NULL))
      return (void *)1;
    if (!(sequence % 23))
      sched_yield();
  }
  return NULL;
}

static void shared_stream(void) {
  char *text = NULL;
  size_t length = 0;
  FILE *stream = open_memstream(&text, &length);
  check(stream != NULL, "shared memory stream");
  if (!stream)
    return;
  struct stream_writer arguments[2] = {{stream, 0}, {stream, 1}};
  pthread_t threads[2] = {0};
  for (unsigned i = 0; i < 2; i++) {
    if (pthread_create(&threads[i], NULL, write_stream, &arguments[i])) {
      threads[i] = NULL;
      check(false, "create stream writer");
    }
  }
  for (unsigned i = 0; i < 2; i++) {
    void *result = (void *)1;
    if (threads[i])
      check(pthread_join(threads[i], &result) == 0 && result == NULL,
            "concurrent stream writer");
  }
  check(fclose(stream) == 0 && length == 12000, "shared stream size");
  bool valid = length == 12000;
  bool seen[2][1000] = {{false}};
  for (size_t offset = 0; valid && offset < length; offset += 6) {
    unsigned writer = (unsigned char)text[offset] - 'A';
    unsigned sequence = 0;
    valid = writer < 2 && text[offset + 5] == '\n';
    for (unsigned digit = 1; digit < 5; digit++) {
      unsigned value = (unsigned char)text[offset + digit] - '0';
      valid &= value < 10;
      sequence = sequence * 10 + value;
    }
    valid = valid && sequence < 1000 && !seen[writer][sequence];
    if (valid)
      seen[writer][sequence] = true;
  }
  check(valid, "atomic formatted writes and concurrent flush");
  free(text);
}

static void file_io(void) {
  FILE *file = tmpfile();
  check(file != NULL, "temporary file");
  if (!file)
    return;
  char buffer[37];
  check(setvbuf(file, buffer, _IOFBF, sizeof(buffer)) == 0,
        "caller-owned stream buffer");
  check(fputs("123abc 0x1.8p1 hello", file) == 0 && fflush(file) == 0,
        "buffered stream output");
  rewind(file);
  int number = 0;
  char word[16] = {0};
  char output[16] = {0};
  double real = 0;
  check(fscanf(file, "%d%3s %lf %15s", &number, word, &real, output) == 4 &&
            number == 123 && !strcmp(word, "abc") && real == 3 &&
            !strcmp(output, "hello"),
        "stream scanning");
  fpos_t position;
  check(fgetpos(file, &position) == 0 && fsetpos(file, &position) == 0,
        "stream position");
  check(ftruncate(fileno(file), 64) == 0, "grow file");
  struct stat status;
  check(fstat(fileno(file), &status) == 0 && status.st_size == 64,
        "resized status");
  fseek(file, 32, SEEK_SET);
  unsigned char zero[32];
  memset(zero, 1, sizeof(zero));
  bool cleared = fread(zero, 1, sizeof(zero), file) == sizeof(zero);
  for (unsigned i = 0; i < sizeof(zero); i++)
    cleared &= zero[i] == 0;
  check(cleared && ftruncate(fileno(file), 3) == 0,
        "zero-filled growth and shrink");
  check(fclose(file) == 0, "temporary file close");
  char *text = NULL;
  size_t length = 0;
  file = open_memstream(&text, &length);
  check(file != NULL, "memory output stream");
  if (file) {
    const char binary[] = {'a', 0, 'b'};
    check(fwrite(binary, 1, sizeof(binary), file) == sizeof(binary) &&
              fprintf(file, "%01024d", 7) == 1024 && fflush(file) == 0 &&
              length == 1027 && !memcmp(text, binary, sizeof(binary)) &&
              text[1026] == '7' && text[1027] == 0,
          "binary and large formatted stream output");
    check(fseek(file, 3, SEEK_END) == 0 && fputc('Q', file) == 'Q' &&
              fflush(file) == 0 && length == 1031 && !text[1027] &&
              !text[1028] && !text[1029] && text[1030] == 'Q',
          "memory stream seek and zero-filled gap");
    check(fclose(file) == 0 && text[1031] == 0, "memory stream ownership");
    free(text);
  }
  errno = 0;
  char *end;
  check(strtoull("18446744073709551616", &end, 10) == UINT64_MAX &&
            errno == ERANGE && !*end,
        "integer overflow");
  const char *invalid = " +word";
  check(strtol(invalid, &end, 10) == 0 && end == invalid,
        "integer no conversion");
  check(setenv("LIBCTEST", "first", 1) == 0 &&
            setenv("LIBCTEST", "second", 0) == 0 &&
            !strcmp(getenv("LIBCTEST"), "first") && unsetenv("LIBCTEST") == 0 &&
            getenv("LIBCTEST") == NULL,
        "environment updates");
}

static void directories_and_mappings(void) {
  check(mkdir("/libcdir", 0) == 0, "create test directory");
  DIR *directory = opendir("/libcdir");
  check(directory != NULL, "directory handle");
  if (!directory)
    return;
  struct stat first, second;
  check(fstat(dirfd(directory), &first) == 0 && S_ISDIR(first.st_mode) &&
            first.st_ino,
        "directory status identity");
  check(rmdir("/libcdir") == -1 && errno == EBUSY,
        "open directory protects lifetime");
  FILE *file = fopen("/libcdir/a", "w");
  check(file != NULL, "create directory entry");
  if (file) {
    check(fstat(fileno(file), &first) == 0 &&
              stat("/libcdir/a", &second) == 0 &&
              first.st_dev == second.st_dev && first.st_ino == second.st_ino,
          "stat and fstat share stable identities");
    char *path = realpath("/libcdir/../libcdir/a", NULL);
    check(path && path[1] == ':' && !strcasecmp(path + 2, "/libcdir/a") &&
              stat(path, &second) == 0 && first.st_dev == second.st_dev &&
              first.st_ino == second.st_ino,
          "VFS canonical path");
    free(path);
    fclose(file);
  }
  closedir(directory);
  directory = opendir("/libcdir");
  bool found = false;
  if (directory) {
    struct dirent *entry;
    while ((entry = readdir(directory)))
      found |= !strcasecmp(entry->d_name, "a") && entry->d_type == DT_REG;
    closedir(directory);
  }
  check(found, "directory enumeration");
  check(unlink("/libcdir/a") == 0 && rmdir("/libcdir") == 0,
        "release directory fixture");

  unsigned char *code =
      mmap(NULL, 6, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  check(code != MAP_FAILED, "anonymous mmap");
  if (code == MAP_FAILED)
    return;
  const unsigned char instructions[] = {0xb8, 42, 0, 0, 0, 0xc3};
  memcpy(code, instructions, sizeof(instructions));
  check(mmap(code, 6, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
             0) == MAP_FAILED &&
            errno == EEXIST,
        "mmap does not replace existing mappings");
  check(mprotect(code, 6, PROT_READ | PROT_WRITE | PROT_EXEC) == -1 &&
            errno == EACCES,
        "mmap W^X enforcement");
  check(mprotect(code, 6, PROT_READ | PROT_EXEC) == 0 &&
            ((int (*)(void))code)() == 42,
        "mmap JIT publication");
  check(munmap(code, 6) == 0, "mmap rounded release");

  void *library = dlopen(NULL, RTLD_NOW);
  size_t (*length)(const char *) =
      (size_t(*)(const char *))dlsym(library, "strlen");
  Dl_info information;
  check(library && length && length("native") == 6 && dlerror() == NULL &&
            dladdr((void *)length, &information) && information.dli_fname &&
            information.dli_fbase,
        "native dynamic symbol lookup");
  check(!dlsym(library, "missing_libctest_symbol") && dlerror() && !dlerror(),
        "dynamic lookup errors");
  check(dlclose(library) == 0, "close process lookup handle");
}

static void *math_thread(void *argument) {
  bool inherited = fegetround() == FE_UPWARD && rint(1.25) == 2;
  fesetround(FE_TOWARDZERO);
  return (void *)(uintptr_t)inherited;
}

static void math_environment(void) {
  fenv_t original, held;
  check(fegetenv(&original) == 0 && fesetround(FE_TONEAREST) == 0,
        "floating-point environment snapshot");
  check(
      fesetround(FE_DOWNWARD) == 0 && feclearexcept(FE_ALL_EXCEPT) == 0 &&
          lround(2.5) == 3 && lround(-2.5) == -3 && lroundf(0.49999997f) == 0 &&
          llround(0x1.fffffffffffffp62) == 9223372036854774784ll &&
          llround(-0x1p63) == LLONG_MIN && llroundf(-0.5f) == -1 &&
          lround((double)LONG_MIN) == LONG_MIN && !fetestexcept(FE_ALL_EXCEPT),
      "integer rounding ties, range and FP mode independence");
  check(lround(-(double)LONG_MIN) == LONG_MIN && fetestexcept(FE_INVALID) &&
            !fetestexcept(FE_INEXACT),
        "integer rounding overflow");
  feclearexcept(FE_ALL_EXCEPT);
  check(llround(NAN) == LLONG_MIN && fetestexcept(FE_INVALID) &&
            !fetestexcept(FE_INEXACT),
        "integer rounding NaN");
  check(feclearexcept(FE_ALL_EXCEPT) == 0 && fesetround(FE_TONEAREST) == 0,
        "restore integer rounding environment");
  check(fma(0x1.0000000000001p0, 0x1.ffffffffffffep-1, -1.0) == -0x1p-104 &&
            fmaf(0x1.000002p0f, 0x1.fffffcp-1f, -1.0f) == -0x1p-46f,
        "fused multiply-add single rounding");
  check(rint(2.5) == 2 && lrintf(2.5f) == 2 && round(-2.5) == -3 &&
            sin(0) == 0 && cos(0) == 1 && signbit(sin(-0.0)) &&
            log2f(16) == 4 && logb(8) == 3 &&
            fabs(log1p(1e-20) - 1e-20) < 1e-30 &&
            fabs(expm1(1e-20) - 1e-20) < 1e-30 &&
            fabs(erf(1) - 0.8427007929497149) < 1e-15,
        "shader compiler scalar math");
  check(fesetround(FE_DOWNWARD) == 0 && fegetround() == FE_DOWNWARD &&
            rint(1.75) == 1 && rint(-1.25) == -2 && lrintf(1.75f) == 1 &&
            fesetround(FE_UPWARD) == 0 && rint(1.25) == 2,
        "rounding modes");
  pthread_t thread;
  void *result = NULL;
  check(pthread_create(&thread, NULL, math_thread, NULL) == 0 &&
            pthread_join(thread, &result) == 0 && result == (void *)1 &&
            fegetround() == FE_UPWARD,
        "thread floating-point environment inheritance and isolation");
  check(feclearexcept(FE_ALL_EXCEPT) == 0 && feraiseexcept(FE_DIVBYZERO) == 0 &&
            feholdexcept(&held) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0 &&
            feraiseexcept(FE_INEXACT) == 0 && feupdateenv(&held) == 0 &&
            fetestexcept(FE_DIVBYZERO | FE_INEXACT) ==
                (FE_DIVBYZERO | FE_INEXACT),
        "floating-point status preservation");
  check(fesetenv(&original) == 0, "restore floating-point environment");
}

int main(void) {
  memory_copy();
  allocations();
  numbers();
  tokenization();
  times_and_locale();
  file_io();
  shared_stream();
  directories_and_mappings();
  math_environment();
  if (!failures) {
    puts("LIBCTEST PASS");
    logk("LIBCTEST PASS\n");
  }
  return failures != 0;
}
