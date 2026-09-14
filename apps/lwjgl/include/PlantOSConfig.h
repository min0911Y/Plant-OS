/* LWJGL native configuration for the freestanding Plant OS runtime. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define DISABLE_WARNINGS()                                                   \
  _Pragma("GCC diagnostic push")                                             \
  _Pragma("GCC diagnostic ignored \"-Wpedantic\"")

#define ENABLE_WARNINGS() _Pragma("GCC diagnostic pop")

#define JNIEXPORT_CRITICAL static
#define CRITICAL(function) _JavaCritical_##function
