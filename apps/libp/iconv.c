#include <errno.h>
#include <iconv.h>

iconv_t iconv_open(const char *to_code, const char *from_code) {
  (void)to_code;
  (void)from_code;
  errno = EINVAL;
  return (iconv_t)-1;
}

int iconv_close(iconv_t descriptor) {
  if (descriptor == (iconv_t)-1) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

size_t iconv(iconv_t descriptor, char **input, size_t *input_left,
             char **output, size_t *output_left) {
  (void)descriptor;
  (void)input;
  (void)input_left;
  (void)output;
  (void)output_left;
  errno = EINVAL;
  return (size_t)-1;
}
