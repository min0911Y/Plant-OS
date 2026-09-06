#include <loader.h>
void Main(const runtime_linker_t *linker);

void _start(const runtime_linker_t *linker) { Main(linker); }
