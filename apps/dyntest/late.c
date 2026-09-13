#include <dlfcn.h>
_Thread_local int dl_fixture_tls = FIXTURE_VALUE;
static _Thread_local unsigned char zero[257] __attribute__((aligned(256)));
static int constructed;
static void __attribute__((constructor)) initialize(void) { constructed = 1; }
int JNI_OnLoad(void) { return constructed ? FIXTURE_VALUE : -1; }
int *dl_fixture_value(void) {
  if ((unsigned long)zero & 255)
    return 0;
  for (unsigned i = 0; i < sizeof(zero); i++)
    if (zero[i])
      return 0;
  zero[256] = 1;
  return &dl_fixture_tls;
}

void *dl_fixture_default(void) {
  /* Keep this DSO's return address visible to the caller-relative lookup. */
  void *volatile result = dlsym(RTLD_DEFAULT, "JNI_OnLoad");
  return result;
}
void *dl_fixture_next(void) {
  void *volatile result = dlsym(RTLD_NEXT, "JNI_OnLoad");
  return result;
}
