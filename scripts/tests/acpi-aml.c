/* cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
 * scripts/tests/acpi-aml.c -o /tmp/acpi-aml && /tmp/acpi-aml */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/platform/pc/acpi_aml.h"

static bool parse(const uint8_t *bytes, size_t size, uint16_t types[2]) {
  /* Exact allocation lets ASan detect reads beyond truncated firmware. */
  uint8_t *copy = malloc(size ? size : 1);
  assert(copy);
  memcpy(copy, bytes, size);
  bool result = aml_s5((AcpiAml){copy, copy + size}, types);
  free(copy);
  return result;
}

int main(void) {
  uint16_t types[2];
  const uint8_t s5[] = {0x08, '\\', '_', 'S', '5', '_', 0x12,
                        6, 2, 0x0a, 5, 0x0a, 6};
  assert(parse(s5, sizeof(s5), types));
  assert(types[0] == 5 << 10 && types[1] == 6 << 10);
  for (size_t length = 0; length < sizeof(s5); length++)
    assert(!parse(s5, length, types));
  uint8_t invalid[sizeof(s5)];
  memcpy(invalid, s5, sizeof(s5));
  invalid[10] = 8;
  assert(!parse(invalid, sizeof(invalid), types));
  memcpy(invalid, s5, sizeof(s5));
  invalid[8] = 1;
  assert(!parse(invalid, sizeof(invalid), types));
  memcpy(invalid, s5, sizeof(s5));
  invalid[7] = 63;
  assert(!parse(invalid, sizeof(invalid), types));
  const uint8_t wide[] = {0x08, '_', 'S', '5', '_', 0x12,
                          0x48, 0, 2, 0x0b, 7, 0, 0x01};
  /* PkgLength counts its own two bytes: 2 + count + word + One. */
  uint8_t valid_wide[sizeof(wide)];
  memcpy(valid_wide, wide, sizeof(wide));
  valid_wide[6] = 0x47;
  assert(parse(valid_wide, sizeof(valid_wide), types));
  assert(types[0] == 7 << 10 && types[1] == 1 << 10);
  assert(!parse(wide, sizeof(wide), types));
  const uint8_t zero[] = {0x08, '_', 'S', '5', '_', 0x12, 4, 2, 0, 1};
  assert(parse(zero, sizeof(zero), types));
  assert(types[0] == 0 && types[1] == 1 << 10);

  /* A byte pattern in a method, scope, buffer or string is not a declaration. */
  uint8_t hidden[64];
  const uint8_t scope_prefix[] = {0x10, 0x22, '\\', 0,
                                  0x5b, 0x80, 'D', 'B', 'G', '_', 1,
                                  0x0b, 2, 4, 1,
                                  0x5b, 0x01, 'L', 'O', 'C', 'K', 0};
  memcpy(hidden, scope_prefix, sizeof(scope_prefix));
  memcpy(hidden + sizeof(scope_prefix), s5, sizeof(s5));
  assert(parse(hidden, sizeof(scope_prefix) + sizeof(s5), types));

  const uint8_t method_prefix[] = {0x14, 0x13, 'M', 'E', 'T', 'H', 0};
  const size_t method_end = sizeof(method_prefix) + sizeof(s5);
  memcpy(hidden, method_prefix, sizeof(method_prefix));
  memcpy(hidden + sizeof(method_prefix), s5, sizeof(s5));
  assert(!parse(hidden, method_end, types));
  memcpy(hidden + method_end, s5, sizeof(s5));
  assert(parse(hidden, method_end + sizeof(s5), types));

  const uint8_t buffer_prefix[] = {0x08, 'D', 'A', 'T', 'A',
                                    0x11, 4, 0x0a, 1, 0x08};
  memcpy(hidden, buffer_prefix, sizeof(buffer_prefix));
  memcpy(hidden + sizeof(buffer_prefix), s5, sizeof(s5));
  assert(parse(hidden, sizeof(buffer_prefix) + sizeof(s5), types));

  const uint8_t string_prefix[] = {0x08, 'D', 'A', 'T', 'A',
                                   0x0d, '_', 'S', '5', '_', 0};
  memcpy(hidden, string_prefix, sizeof(string_prefix));
  memcpy(hidden + sizeof(string_prefix), s5, sizeof(s5));
  assert(parse(hidden, sizeof(string_prefix) + sizeof(s5), types));

  puts("ACPI AML PASS: package lengths, integer encodings, truncation, range, opaque objects");
}
