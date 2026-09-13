#ifndef PLANT_ICONV_H
#define PLANT_ICONV_H

#include <stddef.h>

typedef void *iconv_t;

#ifdef __cplusplus
extern "C" {
#endif

iconv_t iconv_open(const char *to_code, const char *from_code);
int iconv_close(iconv_t descriptor);
size_t iconv(iconv_t descriptor, char **input, size_t *input_left,
             char **output, size_t *output_left);

#ifdef __cplusplus
}
#endif

#endif
