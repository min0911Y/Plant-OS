#include <rand.h>
static unsigned long next1 = 1;
int rand(void)
{
    next1 = next1 * 1103515245 + 12345;
    return ((unsigned)(next1 / 65536) % 32768);
}

void srand(unsigned seed)
{
    next1 = seed;
}
