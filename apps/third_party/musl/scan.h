#ifndef PLANT_SCAN_INPUT_H
#define PLANT_SCAN_INPUT_H

#include <stdio.h>

typedef struct {
  FILE *file;
  const char *string;
  size_t position, count, limit;
  int last;
} scan_input_t;

static inline int scan_get(scan_input_t *input) {
  if (input->limit && input->count == input->limit)
    return input->last = EOF;
  int character;
  if (input->file) {
    character = fgetc(input->file);
  } else {
    character = (unsigned char)input->string[input->position];
    if (!character)
      character = EOF;
    else
      input->position++;
  }
  if (character >= 0)
    input->count++;
  return input->last = character;
}
static inline void scan_unget(scan_input_t *input) {
  if (input->last == EOF) {
    input->last = 0;
    return;
  }
  if (input->file)
    ungetc(input->last, input->file);
  else
    input->position--;
  input->count--;
}
#define shgetc(scan) scan_get(scan)
#define shunget(scan) scan_unget(scan)
#define shlim(scan, width) ((scan)->count = 0, (scan)->limit = (width))
#define shcnt(scan) ((scan)->count)
long double plant_floatscan(scan_input_t *scan, int precision, int partial);
unsigned long long plant_intscan(scan_input_t *scan, unsigned base, int partial, unsigned long long limit);
int plant_scan(scan_input_t *scan, const char *format, va_list arguments);

#endif
