#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>

int ta_init(const void *base, const void *limit, const size_t heap_blocks, const size_t split_thresh, const size_t alignment);
void *ta_alloc(size_t num);
void *ta_calloc(size_t num, size_t size);
int ta_free(void *ptr);

size_t ta_num_free();
size_t ta_num_used();
size_t ta_num_fresh();
int ta_check();

#ifdef __cplusplus
}
#endif
