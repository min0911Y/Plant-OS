#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <wchar.h>

int mbsinit(const mbstate_t *state) { return !state || !state->state; }

size_t mbrtowc(wchar_t *wide, const char *text, size_t size, mbstate_t *state) {
  static mbstate_t internal;
  if (!state)
    state = &internal;
  if (!text)
    return mbrtowc(NULL, "", 1, state);
  if (!size)
    return (size_t)-2;
  size_t used = 0;
  unsigned remaining = state->state & 7;
  uint32_t minimum = state->state >> 3;
  uint32_t value = state->value;
  if (!remaining) {
    unsigned first = (unsigned char)text[used++];
    if (first < 128) {
      if (wide)
        *wide = first;
      return first ? 1 : 0;
    }
    if (MB_CUR_MAX == 1)
      goto invalid;
    if (first >= 0xc2 && first <= 0xdf) {
      remaining = 1;
      minimum = 0x80;
      value = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
      remaining = 2;
      minimum = 0x800;
      value = first & 0xf;
    } else if (first >= 0xf0 && first <= 0xf4) {
      remaining = 3;
      minimum = 0x10000;
      value = first & 7;
    } else {
      goto invalid;
    }
  }
  while (remaining && used < size) {
    unsigned byte = (unsigned char)text[used++];
    if ((byte & 0xc0) != 0x80)
      goto invalid;
    value = value << 6 | (byte & 0x3f);
    remaining--;
    uint32_t lower = value << (6 * remaining);
    uint32_t upper = lower | ((1u << (6 * remaining)) - 1);
    if (upper < minimum || lower > 0x10ffff ||
        (lower >= 0xd800 && upper <= 0xdfff))
      goto invalid;
  }
  if (remaining) {
    state->value = value;
    state->state = minimum << 3 | remaining;
    return (size_t)-2;
  }
  *state = (mbstate_t){0};
  if (wide)
    *wide = value;
  return used;
invalid:
  *state = (mbstate_t){0};
  errno = EILSEQ;
  return (size_t)-1;
}
size_t mbrlen(const char *text, size_t size, mbstate_t *state) {
  return mbrtowc(NULL, text, size, state);
}
size_t wcrtomb(char *text, wchar_t wide, mbstate_t *state) {
  if (state)
    *state = (mbstate_t){0};
  if (!text)
    return 1;
  uint32_t value = (uint32_t)wide;
  if (value < 128) {
    text[0] = value;
    return 1;
  }
  if (MB_CUR_MAX == 1 || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) {
    errno = EILSEQ;
    return (size_t)-1;
  }
  unsigned count = value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
  for (unsigned i = count - 1; i; i--) {
    text[i] = 0x80 | (value & 0x3f);
    value >>= 6;
  }
  text[0] = (count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | value;
  return count;
}
int mbtowc(wchar_t *wide, const char *text, size_t size) {
  static mbstate_t state;
  if (!text) {
    state = (mbstate_t){0};
    return 0;
  }
  size_t result = mbrtowc(wide, text, size, &state);
  return result > size ? -1 : (int)result;
}
int mblen(const char *text, size_t size) { return mbtowc(NULL, text, size); }
int wctomb(char *text, wchar_t wide) {
  if (!text)
    return 0;
  size_t result = wcrtomb(text, wide, NULL);
  return result == (size_t)-1 ? -1 : (int)result;
}

size_t mbstowcs(wchar_t *wide, const char *text, size_t size) {
  mbstate_t state = {0};
  size_t count = 0;
  while (*text) {
    wchar_t value;
    size_t length = mbrtowc(&value, text, (size_t)-1, &state);
    if (length == (size_t)-1 || length == (size_t)-2)
      return (size_t)-1;
    if (wide) {
      if (count == size)
        return count;
      wide[count] = value;
    }
    count++;
    text += length;
  }
  if (wide && count < size)
    wide[count] = 0;
  return count;
}

size_t wcstombs(char *text, const wchar_t *wide, size_t size) {
  mbstate_t state = {0};
  size_t count = 0;
  while (*wide) {
    char encoded[MB_LEN_MAX];
    size_t length = wcrtomb(encoded, *wide++, &state);
    if (length == (size_t)-1)
      return (size_t)-1;
    if (text) {
      if (length > size - count)
        return count;
      for (size_t i = 0; i < length; i++)
        text[count + i] = encoded[i];
    }
    count += length;
  }
  if (text && count < size)
    text[count] = '\0';
  return count;
}

size_t wcslen(const wchar_t *text) {
  const wchar_t *end = text;
  while (*end)
    end++;
  return end - text;
}
