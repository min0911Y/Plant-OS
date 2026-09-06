#include <stdlib.h>
#include <syscall.h>

extern "C" void __cxa_pure_virtual() { abort(); }

void *operator new(size_t size) { return malloc(size); }
void *operator new[](size_t size) { return malloc(size); }
void operator delete(void *pointer) { free(pointer); }
void operator delete[](void *pointer) { free(pointer); }
void operator delete(void *pointer, size_t) { free(pointer); }
void operator delete[](void *pointer, size_t) { free(pointer); }

extern "C" void __gxx_personality_v0() { exit(-1); }
