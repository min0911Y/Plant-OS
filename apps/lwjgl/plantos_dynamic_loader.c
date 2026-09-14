#include "common_tools.h"
#include <dlfcn.h>

EXTERN_C_ENTER

JNIEXPORT jlong JNICALL
Java_org_lwjgl_system_plantos_PlantOSLibrary_ndlopen(JNIEnv *env, jclass clazz,
                                                     jlong filename, jint mode) {
  UNUSED_PARAMS(env, clazz)
  return (jlong)(uintptr_t)dlopen((const char *)(uintptr_t)filename, mode);
}

JNIEXPORT jlong JNICALL
Java_org_lwjgl_system_plantos_PlantOSLibrary_ndlerror(JNIEnv *env,
                                                      jclass clazz) {
  UNUSED_PARAMS(env, clazz)
  return (jlong)(uintptr_t)dlerror();
}

JNIEXPORT jlong JNICALL
Java_org_lwjgl_system_plantos_PlantOSLibrary_ndlsym(JNIEnv *env, jclass clazz,
                                                    jlong handle, jlong name) {
  UNUSED_PARAMS(env, clazz)
  return (jlong)(uintptr_t)dlsym((void *)(uintptr_t)handle,
                                 (const char *)(uintptr_t)name);
}

JNIEXPORT jint JNICALL
Java_org_lwjgl_system_plantos_PlantOSLibrary_ndlclose(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong handle) {
  UNUSED_PARAMS(env, clazz)
  return (jint)dlclose((void *)(uintptr_t)handle);
}

EXTERN_C_EXIT
