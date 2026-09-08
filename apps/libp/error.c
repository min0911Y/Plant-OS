#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const messages[] = {
#define E(number, text) [number] = text,
#include "../third_party/musl/error_strings.h"
#undef E
};

char *strerror(int number) {
  return (char *)(number >= 0 &&
                          (size_t)number <
                              sizeof(messages) / sizeof(*messages) &&
                          messages[number]
                      ? messages[number]
                      : "Unknown error");
}
int strerror_r(int number, char *buffer, size_t size) {
  const char *text = strerror(number);
  size_t length = strlen(text);
  if (!buffer || size <= length)
    return ERANGE;
  memcpy(buffer, text, length + 1);
  return number >= 0 && (size_t)number < sizeof(messages) / sizeof(*messages) &&
                 messages[number]
             ? 0
             : EINVAL;
}
void __assert_fail(const char *expression, const char *file, unsigned line,
                   const char *function) {
  fprintf(stderr, "%s:%u: %s: assertion '%s' failed\n", file, line, function,
          expression);
  abort();
}
