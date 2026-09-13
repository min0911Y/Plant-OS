#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <syscall.h>

static pthread_barrier_t gate;
static void *libraries[2];
static int failures;

static void check(int value, const char *message) {
  if (!value) {
    __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED);
    logkf("DYNLOAD FAIL: %s (%s)\n", message, dlerror());
  }
}

static void inspect(void) {
  for (unsigned i = 0; i < 2; i++) {
    int (*load)(void) = dlsym(libraries[i], "JNI_OnLoad");
    int *(*value)(void) = dlsym(libraries[i], "dl_fixture_value");
    int *symbol = dlsym(libraries[i], "dl_fixture_tls");
    check(load && load() == (i + 1) * 101, "per-library JNI entry");
    check(value && symbol && value() == symbol && *symbol == (int)(i + 1) * 101,
          "late TLS template, constructor and symbol address");
    if (symbol)
      *symbol = 900 + i;
  }
}

static void *worker(void *unused) {
  pthread_barrier_wait(&gate);
  inspect();
  for (unsigned i = 0; i < 32; i++) {
    void *handle = dlopen("/lib/liblatea.so", RTLD_NOW);
    check(handle == libraries[0], "concurrent repeated open");
    check(dlclose(handle) == 0, "concurrent close");
  }
  return NULL;
}

int runtime_loading(void) {
  failures = 0;
  pthread_t thread;
  if (pthread_barrier_init(&gate, NULL, 2) ||
      pthread_create(&thread, NULL, worker, NULL))
    return 1;
  check(dlopen("/lib/libbad.so", RTLD_NOW) == NULL && dlerror(),
        "undefined relocation is recoverable");
  check(dlopen("/lib/absent.so", RTLD_NOW) == NULL && dlerror(),
        "missing library is recoverable");
  libraries[0] = dlopen("/lib/liblatea.so", RTLD_NOW);
  libraries[1] = dlopen("/lib/liblateb.so", RTLD_NOW);
  check(libraries[0] && libraries[1] && libraries[0] != libraries[1],
        "distinct library handles");
  check(!dlsym(RTLD_DEFAULT, "JNI_OnLoad") && dlerror(),
        "local scope isolation");
  void *(*local)(void) = dlsym(libraries[1], "dl_fixture_default");
  void *(*next)(void) = dlsym(libraries[1], "dl_fixture_next");
  check(local && local() == dlsym(libraries[1], "JNI_OnLoad"),
        "local caller default scope");
  check(next && next() == dlsym(libraries[0], "JNI_OnLoad"),
        "NEXT follows dependency scope");
  inspect();
  pthread_barrier_wait(&gate);
  check(pthread_join(thread, NULL) == 0, "existing thread uses late TLS");
  int created = pthread_create(&thread, NULL, worker, NULL);
  check(created == 0, "new thread after TLS loading");
  if (!created) {
    pthread_barrier_wait(&gate);
    check(pthread_join(thread, NULL) == 0,
          "new thread receives independent late TLS");
  }
  pthread_barrier_destroy(&gate);
  for (unsigned i = 0; i < 2; i++) {
    int *symbol = dlsym(libraries[i], "dl_fixture_tls");
    check(symbol && *symbol == (int)(900 + i), "thread TLS isolation");
  }
  int child = fork();
  if (!child) {
    int *symbol = dlsym(libraries[0], "dl_fixture_tls");
    if (!symbol || *symbol != 900)
      _exit(1);
    *symbol = 42;
    _exit(0);
  }
  check(child > 0 && waittid(child) == 0,
        "dynamic TLS and loader lock after fork");
  void *promoted =
      dlopen("/lib/liblatea.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
  check(promoted == libraries[0] &&
            dlsym(RTLD_DEFAULT, "JNI_OnLoad") == dlsym(promoted, "JNI_OnLoad"),
        "global promotion");
  check(dlclose(promoted) == 0, "promotion reference");
  check(dlclose(libraries[0]) == 0 && dlclose(libraries[0]) == -1,
        "close validates references");
  check(dlclose(libraries[1]) == 0 && !dlsym((void *)123, "JNI_OnLoad"),
        "invalid handle is rejected");
  if (!failures)
    logk("DYNLOAD PASS\n");
  return failures != 0;
}
