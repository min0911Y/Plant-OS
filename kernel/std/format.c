#include <ctypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  char *buffer;
  size_t capacity, length;
} format_output_t;

static void format_append(format_output_t *out, const char *text, size_t size) {
  size_t available =
      out->length < out->capacity ? out->capacity - out->length : 0;
  if (available)
    memcpy(out->buffer + out->length, text,
           size < available ? size : available);
  out->length += size;
}
static void format_padding(format_output_t *out, char fill, size_t size) {
  size_t available =
      out->length < out->capacity ? out->capacity - out->length : 0;
  if (available)
    memset(out->buffer + out->length, fill,
           size < available ? size : available);
  out->length += size;
}

int vsnprintf(char *buffer, size_t capacity, const char *format,
              va_list arguments) {
  format_output_t out = {buffer, capacity ? capacity - 1 : 0, 0};
  while (*format) {
    const char *literal = format;
    while (*format && *format != '%')
      format++;
    format_append(&out, literal, format - literal);
    if (!*format)
      break;
    format++;
    bool left = false, zero = false, alternate = false;
    char sign = 0;
    for (;; format++) {
      if (*format == '-')
        left = true;
      else if (*format == '0')
        zero = true;
      else if (*format == '#')
        alternate = true;
      else if (*format == '+')
        sign = '+';
      else if (*format == ' ') {
        if (!sign)
          sign = ' ';
      } else
        break;
    }
    size_t width = 0;
    if (*format == '*') {
      int value = va_arg(arguments, int);
      left |= value < 0;
      width = value < 0 ? -(int64_t)value : value;
      format++;
    } else {
      while (*format >= '0' && *format <= '9') {
        if (width > (INT_MAX - 9u) / 10)
          return -1;
        width = width * 10 + *format++ - '0';
      }
    }
    int precision = -1;
    if (*format == '.') {
      format++;
      precision = 0;
      if (*format == '*') {
        precision = va_arg(arguments, int);
        format++;
      } else
        while (*format >= '0' && *format <= '9') {
          if (precision > (INT_MAX - 9) / 10)
            return -1;
          precision = precision * 10 + *format++ - '0';
        }
    }
    enum {
      NORMAL,
      CHAR,
      SHORT,
      LONG,
      LONG_LONG,
      SIZE,
      POINTER
    } length = NORMAL;
    if (*format == 'l') {
      length = LONG;
      if (*++format == 'l') {
        length = LONG_LONG;
        format++;
      }
    } else if (*format == 'h') {
      length = SHORT;
      if (*++format == 'h') {
        length = CHAR;
        format++;
      }
    } else if (*format == 'z') {
      length = SIZE;
      format++;
    } else if (*format == 't') {
      length = POINTER;
      format++;
    }
    char conversion = *format;
    if (conversion)
      format++;
    char digits[65], prefix[3];
    char *text = digits + sizeof(digits);
    size_t count = 0, prefix_size = 0, zeros = 0;
    if (conversion == 's') {
      text = va_arg(arguments, char *);
      if (!text)
        text = "(null)";
      while (text[count] && (precision < 0 || count < (size_t)precision))
        count++;
      zero = false;
    } else if (conversion == 'c' || conversion == '%') {
      digits[0] = conversion == '%' ? '%' : (char)va_arg(arguments, int);
      text = digits;
      count = 1;
      zero = false;
    } else if (conversion == 'd' || conversion == 'i' || conversion == 'u' ||
               conversion == 'x' || conversion == 'X' || conversion == 'o' ||
               conversion == 'b' || conversion == 'p') {
      bool negative = false;
      uint64_t value;
      if (conversion == 'p')
        value = (uintptr_t)va_arg(arguments, void *);
      else if (conversion == 'd' || conversion == 'i') {
        int64_t number;
        switch (length) {
        case LONG:
          number = va_arg(arguments, long);
          break;
        case LONG_LONG:
          number = va_arg(arguments, long long);
          break;
        case SIZE:
        case POINTER:
          number = va_arg(arguments, intptr_t);
          break;
        default:
          number = va_arg(arguments, int);
          break;
        }
        if (length == CHAR)
          number = (int8_t)number;
        if (length == SHORT)
          number = (int16_t)number;
        negative = number < 0;
        value = negative ? 0ull - (uint64_t)number : (uint64_t)number;
      } else {
        switch (length) {
        case LONG:
          value = va_arg(arguments, unsigned long);
          break;
        case LONG_LONG:
          value = va_arg(arguments, unsigned long long);
          break;
        case SIZE:
        case POINTER:
          value = va_arg(arguments, uintptr_t);
          break;
        default:
          value = va_arg(arguments, unsigned);
          break;
        }
        if (length == CHAR)
          value = (uint8_t)value;
        if (length == SHORT)
          value = (uint16_t)value;
      }
      unsigned base =
          conversion == 'o'                                               ? 8
          : conversion == 'b'                                             ? 2
          : (conversion == 'x' || conversion == 'X' || conversion == 'p') ? 16
                                                                          : 10;
      const char *alphabet =
          conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
      if (negative)
        prefix[prefix_size++] = '-';
      else if (sign && (conversion == 'd' || conversion == 'i'))
        prefix[prefix_size++] = sign;
      if (conversion == 'p' || (alternate && base == 16 && value)) {
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = conversion == 'X' ? 'X' : 'x';
      }
      if (value || precision != 0)
        do {
          *--text = alphabet[value % base];
          value /= base;
          count++;
        } while (value);
      if (alternate && base == 8 && (!count || text[0] != '0'))
        zeros = 1;
      if (precision > 0 && (size_t)precision > count)
        zeros = precision - count;
      if (precision >= 0)
        zero = false;
    } else {
      digits[0] = '%';
      digits[1] = conversion;
      text = digits;
      count = conversion ? 2 : 1;
    }
    size_t body = prefix_size + zeros + count;
    size_t padding = width > body ? width - body : 0;
    if (!left && !zero)
      format_padding(&out, ' ', padding);
    format_append(&out, prefix, prefix_size);
    if (!left && zero)
      format_padding(&out, '0', padding);
    format_padding(&out, '0', zeros);
    format_append(&out, text, count);
    if (left)
      format_padding(&out, ' ', padding);
  }
  if (capacity)
    buffer[out.length < capacity ? out.length : capacity - 1] = 0;
  return out.length > INT_MAX ? -1 : (int)out.length;
}
int vsprintf(char *buffer, const char *format, va_list arguments) {
  return vsnprintf(buffer, (size_t)-1, format, arguments);
}
int snprintf(char *buffer, size_t capacity, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(buffer, capacity, format, arguments);
  va_end(arguments);
  return result;
}
int sprintf(char *buffer, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsprintf(buffer, format, arguments);
  va_end(arguments);
  return result;
}
