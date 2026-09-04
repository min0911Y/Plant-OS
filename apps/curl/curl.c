#include <socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_DEFAULT_PORT 80u
#define HTTP_HEADER_LIMIT (64u * 1024u)
#define HTTP_IO_BUFFER_SIZE 8192u
#define HTTP_REDIRECT_LIMIT 10u
#define HTTP_RECEIVE_TIMEOUT_SECONDS 30
#define HTTP_UINT32_MAX 0xffffffffu
#define HTTP_UINT64_MAX 0xffffffffffffffffull

typedef enum {
  HTTP_RESULT_OK = 0,
  HTTP_RESULT_USAGE = 2,
  HTTP_RESULT_URL = 3,
  HTTP_RESULT_RESOLVE = 6,
  HTTP_RESULT_CONNECT = 7,
  HTTP_RESULT_PROTOCOL = 8,
  HTTP_RESULT_TRANSFER = 18,
  HTTP_RESULT_WRITE = 23,
  HTTP_RESULT_MEMORY = 27,
  HTTP_RESULT_TIMEOUT = 28,
  HTTP_RESULT_REDIRECT = 47,
} http_result_t;

typedef enum {
  HTTP_URL_OK,
  HTTP_URL_INVALID,
  HTTP_URL_UNSUPPORTED,
  HTTP_URL_MEMORY,
} http_url_result_t;

typedef enum {
  HTTP_TRANSFER_NONE,
  HTTP_TRANSFER_CHUNKED,
  HTTP_TRANSFER_UNSUPPORTED,
} http_transfer_t;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} http_buffer_t;

typedef struct {
  char *host;
  char *target;
  uint16_t port;
} http_url_t;

typedef struct {
  const char *url;
  const char *method;
  const char *data;
  const char *output_path;
  const char **headers;
  size_t header_count;
  bool data_present;
  bool method_present;
  bool include_headers;
  bool head;
  bool follow_redirects;
  bool help;
} http_options_t;

typedef struct {
  socket_t socket;
  uint8_t buffer[HTTP_IO_BUFFER_SIZE];
  size_t begin;
  size_t end;
} http_stream_t;

typedef struct {
  int status;
  bool content_length_present;
  http_transfer_t transfer;
  uint64_t content_length;
  char *location;
  http_buffer_t raw_headers;
} http_response_t;

static bool http_buffer_reserve(http_buffer_t *buffer, size_t extra) {
  if (extra > HTTP_UINT32_MAX - buffer->length - 1u) {
    return false;
  }
  size_t required = buffer->length + extra + 1u;
  if (required <= buffer->capacity) {
    return true;
  }

  size_t capacity = buffer->capacity == 0 ? 256u : buffer->capacity;
  while (capacity < required) {
    if (capacity > HTTP_UINT32_MAX / 2u) {
      capacity = required;
      break;
    }
    capacity *= 2u;
  }
  char *data = realloc(buffer->data, capacity);
  if (data == NULL) {
    return false;
  }
  buffer->data = data;
  buffer->capacity = capacity;
  return true;
}

static bool http_buffer_append(http_buffer_t *buffer, const void *data,
                               size_t length) {
  if (!http_buffer_reserve(buffer, length)) {
    return false;
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return true;
}

static bool http_buffer_append_string(http_buffer_t *buffer,
                                      const char *text) {
  return http_buffer_append(buffer, text, strlen(text));
}

static void http_buffer_reset(http_buffer_t *buffer) {
  buffer->length = 0;
  if (buffer->data != NULL) {
    buffer->data[0] = '\0';
  }
}

static void http_buffer_destroy(http_buffer_t *buffer) {
  free(buffer->data);
  memset(buffer, 0, sizeof(*buffer));
}

static char *http_string_copy(const char *begin, size_t length) {
  char *copy = malloc(length + 1u);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, begin, length);
  copy[length] = '\0';
  return copy;
}

static void http_url_destroy(http_url_t *url) {
  free(url->host);
  free(url->target);
  memset(url, 0, sizeof(*url));
}

static bool http_url_character_valid(unsigned char character) {
  return character > 0x20u && character != 0x7fu;
}

static http_url_result_t http_url_parse(const char *text, http_url_t *url) {
  memset(url, 0, sizeof(*url));
  if (text == NULL || *text == '\0') {
    return HTTP_URL_INVALID;
  }

  const char *authority = text;
  if (strncasecmp(text, "http://", 7u) == 0) {
    authority += 7;
  } else if (strstr(text, "://") != NULL) {
    return HTTP_URL_UNSUPPORTED;
  }

  const char *authority_end = authority + strcspn(authority, "/?#");
  if (authority_end == authority) {
    return HTTP_URL_INVALID;
  }

  const char *colon = NULL;
  for (const char *cursor = authority; cursor < authority_end; cursor++) {
    unsigned char character = (unsigned char)*cursor;
    if (!http_url_character_valid(character) || character == '@' ||
        character == '[' || character == ']' || character == '\\') {
      return HTTP_URL_INVALID;
    }
    if (character == ':') {
      if (colon != NULL) {
        return HTTP_URL_INVALID;
      }
      colon = cursor;
    }
  }

  const char *host_end = colon == NULL ? authority_end : colon;
  size_t host_length = (size_t)(host_end - authority);
  if (host_length == 0 || host_length > 255u) {
    return HTTP_URL_INVALID;
  }

  uint16_t port = HTTP_DEFAULT_PORT;
  if (colon != NULL) {
    const char *cursor = colon + 1;
    uint32_t value = 0;
    if (cursor == authority_end) {
      return HTTP_URL_INVALID;
    }
    while (cursor < authority_end) {
      if (*cursor < '0' || *cursor > '9' || value > 6553u ||
          (value == 6553u && *cursor > '5')) {
        return HTTP_URL_INVALID;
      }
      value = value * 10u + (uint32_t)(*cursor++ - '0');
    }
    if (value == 0) {
      return HTTP_URL_INVALID;
    }
    port = (uint16_t)value;
  }

  const char *fragment = strchr(authority_end, '#');
  const char *target_end = fragment == NULL ? text + strlen(text) : fragment;
  size_t target_length = (size_t)(target_end - authority_end);
  size_t target_prefix = *authority_end == '?' ? 1u : 0u;
  if (*authority_end == '#') {
    target_length = 0;
  }
  for (const char *cursor = authority_end; cursor < target_end; cursor++) {
    if (!http_url_character_valid((unsigned char)*cursor)) {
      return HTTP_URL_INVALID;
    }
  }

  url->host = http_string_copy(authority, host_length);
  if (url->host == NULL) {
    return HTTP_URL_MEMORY;
  }
  url->target = malloc((target_length == 0 ? 1u : target_length + target_prefix) +
                       1u);
  if (url->target == NULL) {
    http_url_destroy(url);
    return HTTP_URL_MEMORY;
  }
  if (target_length == 0) {
    strcpy(url->target, "/");
  } else {
    if (target_prefix != 0) {
      url->target[0] = '/';
    }
    memcpy(url->target + target_prefix, authority_end, target_length);
    url->target[target_prefix + target_length] = '\0';
  }
  url->port = port;
  return HTTP_URL_OK;
}

static bool http_location_has_scheme(const char *location) {
  if ((*location < 'A' || *location > 'Z') &&
      (*location < 'a' || *location > 'z')) {
    return false;
  }
  for (location++; *location != '\0'; location++) {
    if (*location == ':') {
      return true;
    }
    if (*location == '/' || *location == '?' || *location == '#') {
      return false;
    }
    if ((*location < 'A' || *location > 'Z') &&
        (*location < 'a' || *location > 'z') &&
        (*location < '0' || *location > '9') && *location != '+' &&
        *location != '-' && *location != '.') {
      return false;
    }
  }
  return false;
}

static http_url_result_t http_url_resolve(const http_url_t *base,
                                          const char *location,
                                          http_url_t *resolved) {
  while (*location == ' ' || *location == '\t') {
    location++;
  }
  size_t location_length = strlen(location);
  while (location_length != 0 &&
         (location[location_length - 1u] == ' ' ||
          location[location_length - 1u] == '\t')) {
    location_length--;
  }
  if (location_length == 0) {
    return HTTP_URL_INVALID;
  }

  char *trimmed = http_string_copy(location, location_length);
  if (trimmed == NULL) {
    return HTTP_URL_MEMORY;
  }
  if (strncasecmp(trimmed, "http://", 7u) == 0) {
    http_url_result_t result = http_url_parse(trimmed, resolved);
    free(trimmed);
    return result;
  }
  if (http_location_has_scheme(trimmed)) {
    free(trimmed);
    return HTTP_URL_UNSUPPORTED;
  }

  http_buffer_t absolute = {0};
  bool success = http_buffer_append_string(&absolute, "http:");
  if (strncmp(trimmed, "//", 2u) == 0) {
    success = success && http_buffer_append_string(&absolute, trimmed);
  } else {
    char port[8];
    success = success && http_buffer_append_string(&absolute, "//") &&
              http_buffer_append_string(&absolute, base->host);
    if (base->port != HTTP_DEFAULT_PORT) {
      int length = snprintf(port, sizeof(port), ":%u", (unsigned)base->port);
      success = success && length > 0 && (size_t)length < sizeof(port) &&
                http_buffer_append(&absolute, port, (size_t)length);
    }

    if (trimmed[0] == '/') {
      success = success && http_buffer_append_string(&absolute, trimmed);
    } else if (trimmed[0] == '?') {
      size_t path_length = strcspn(base->target, "?");
      success = success &&
                http_buffer_append(&absolute, base->target, path_length) &&
                http_buffer_append_string(&absolute, trimmed);
    } else if (trimmed[0] == '#') {
      success = success && http_buffer_append_string(&absolute, base->target);
    } else {
      size_t path_length = strcspn(base->target, "?");
      const char *slash = NULL;
      for (size_t i = 0; i < path_length; i++) {
        if (base->target[i] == '/') {
          slash = base->target + i;
        }
      }
      size_t directory_length =
          slash == NULL ? 1u : (size_t)(slash - base->target) + 1u;
      success = success &&
                http_buffer_append(&absolute, base->target, directory_length) &&
                http_buffer_append_string(&absolute, trimmed);
    }
  }

  free(trimmed);
  if (!success) {
    http_buffer_destroy(&absolute);
    return HTTP_URL_MEMORY;
  }
  http_url_result_t result = http_url_parse(absolute.data, resolved);
  http_buffer_destroy(&absolute);
  return result;
}

static bool http_token_character(unsigned char character) {
  if ((character >= 'A' && character <= 'Z') ||
      (character >= 'a' && character <= 'z') ||
      (character >= '0' && character <= '9')) {
    return true;
  }
  return strchr("!#$%&'*+-.^_`|~", character) != NULL;
}

static bool http_method_valid(const char *method) {
  if (method == NULL || *method == '\0') {
    return false;
  }
  for (; *method != '\0'; method++) {
    if (!http_token_character((unsigned char)*method)) {
      return false;
    }
  }
  return true;
}

static const char *http_header_colon_n(const char *header, size_t length) {
  const char *colon = memchr(header, ':', length);
  if (colon == header || colon == NULL) {
    return NULL;
  }
  for (const char *cursor = header; cursor < colon; cursor++) {
    if (!http_token_character((unsigned char)*cursor)) {
      return NULL;
    }
  }
  for (const char *cursor = colon + 1; cursor < header + length; cursor++) {
    unsigned char character = (unsigned char)*cursor;
    if ((character < 0x20u && character != '\t') || character == 0x7fu) {
      return NULL;
    }
  }
  return colon;
}

static const char *http_header_colon(const char *header) {
  return http_header_colon_n(header, strlen(header));
}

static bool http_header_named(const char *header, const char *name) {
  const char *colon = strchr(header, ':');
  size_t name_length = strlen(name);
  return colon != NULL && (size_t)(colon - header) == name_length &&
         strncasecmp(header, name, name_length) == 0;
}

static bool http_options_have_header(const http_options_t *options,
                                     const char *name) {
  for (size_t i = 0; i < options->header_count; i++) {
    if (http_header_named(options->headers[i], name)) {
      return true;
    }
  }
  return false;
}

static void http_usage(FILE *stream) {
  fprintf(stream,
          "Usage: curl.bin [options] <http://host[:port]/path>\n"
          "Options:\n"
          "  -i, --include         Include response headers\n"
          "  -I, --head            Send HEAD and show headers\n"
          "  -L, --location        Follow redirects (up to 10)\n"
          "  -o, --output FILE     Write output to FILE\n"
          "  -X, --request METHOD  Use an HTTP method\n"
          "  -d, --data DATA       Send DATA (defaults to POST)\n"
          "  -H, --header HEADER   Add a request header\n"
          "  -h, --help            Show this help\n");
}

static bool http_option_value(int argc, char **argv, int *index,
                              const char *attached, const char **value) {
  if (*attached != '\0') {
    *value = attached;
    return true;
  }
  if (*index + 1 >= argc) {
    return false;
  }
  *value = argv[++*index];
  return true;
}

static http_result_t http_options_parse(int argc, char **argv,
                                        http_options_t *options) {
  memset(options, 0, sizeof(*options));
  options->headers = malloc((size_t)argc * sizeof(*options->headers));
  if (options->headers == NULL) {
    return HTTP_RESULT_MEMORY;
  }

  bool options_enabled = true;
  for (int i = 1; i < argc; i++) {
    const char *argument = argv[i];
    if (options_enabled && strcmp(argument, "--") == 0) {
      options_enabled = false;
      continue;
    }
    if (!options_enabled || argument[0] != '-' || argument[1] == '\0') {
      if (options->url != NULL) {
        fprintf(stderr, "curl: only one URL may be specified\n");
        return HTTP_RESULT_USAGE;
      }
      options->url = argument;
      continue;
    }

    if (argument[1] == '-') {
      const char *value = NULL;
      if (strcmp(argument, "--help") == 0) {
        http_usage(stdout);
        options->help = true;
        return HTTP_RESULT_OK;
      }
      if (strcmp(argument, "--include") == 0) {
        options->include_headers = true;
      } else if (strcmp(argument, "--head") == 0) {
        options->head = true;
      } else if (strcmp(argument, "--location") == 0) {
        options->follow_redirects = true;
      } else if (strcmp(argument, "--output") == 0) {
        if (!http_option_value(argc, argv, &i, "", &value)) {
          fprintf(stderr, "curl: --output requires a file\n");
          return HTTP_RESULT_USAGE;
        }
        options->output_path = value;
      } else if (strncmp(argument, "--output=", 9u) == 0) {
        options->output_path = argument + 9;
      } else if (strcmp(argument, "--request") == 0) {
        if (!http_option_value(argc, argv, &i, "", &value)) {
          fprintf(stderr, "curl: --request requires a method\n");
          return HTTP_RESULT_USAGE;
        }
        options->method = value;
        options->method_present = true;
      } else if (strncmp(argument, "--request=", 10u) == 0) {
        options->method = argument + 10;
        options->method_present = true;
      } else if (strcmp(argument, "--data") == 0) {
        if (!http_option_value(argc, argv, &i, "", &value)) {
          fprintf(stderr, "curl: --data requires a value\n");
          return HTTP_RESULT_USAGE;
        }
        options->data = value;
        options->data_present = true;
      } else if (strncmp(argument, "--data=", 7u) == 0) {
        options->data = argument + 7;
        options->data_present = true;
      } else if (strcmp(argument, "--header") == 0) {
        if (!http_option_value(argc, argv, &i, "", &value)) {
          fprintf(stderr, "curl: --header requires a value\n");
          return HTTP_RESULT_USAGE;
        }
        options->headers[options->header_count++] = value;
      } else if (strncmp(argument, "--header=", 9u) == 0) {
        options->headers[options->header_count++] = argument + 9;
      } else {
        fprintf(stderr, "curl: unknown option %s\n", argument);
        return HTTP_RESULT_USAGE;
      }
      continue;
    }

    for (const char *option = argument + 1; *option != '\0'; option++) {
      const char *value = NULL;
      if (*option == 'i') {
        options->include_headers = true;
      } else if (*option == 'I') {
        options->head = true;
      } else if (*option == 'L') {
        options->follow_redirects = true;
      } else if (*option == 'h') {
        http_usage(stdout);
        options->help = true;
        return HTTP_RESULT_OK;
      } else if (*option == 'o' || *option == 'X' || *option == 'd' ||
                 *option == 'H') {
        if (!http_option_value(argc, argv, &i, option + 1, &value)) {
          fprintf(stderr, "curl: -%c requires a value\n", *option);
          return HTTP_RESULT_USAGE;
        }
        if (*option == 'o') {
          options->output_path = value;
        } else if (*option == 'X') {
          options->method = value;
          options->method_present = true;
        } else if (*option == 'd') {
          options->data = value;
          options->data_present = true;
        } else {
          options->headers[options->header_count++] = value;
        }
        break;
      } else {
        fprintf(stderr, "curl: unknown option -%c\n", *option);
        return HTTP_RESULT_USAGE;
      }
    }
  }

  if (options->url == NULL) {
    http_usage(stderr);
    return HTTP_RESULT_USAGE;
  }
  if (options->head) {
    if (options->data_present ||
        (options->method_present && strcmp(options->method, "HEAD") != 0)) {
      fprintf(stderr, "curl: --head cannot be combined with a request body or "
                      "another method\n");
      return HTTP_RESULT_USAGE;
    }
    options->method = "HEAD";
    options->include_headers = true;
  } else if (!options->method_present) {
    options->method = options->data_present ? "POST" : "GET";
  }
  if (!http_method_valid(options->method) ||
      strcmp(options->method, "CONNECT") == 0) {
    fprintf(stderr, "curl: invalid or unsupported HTTP method\n");
    return HTTP_RESULT_USAGE;
  }

  unsigned host_headers = 0;
  for (size_t i = 0; i < options->header_count; i++) {
    const char *header = options->headers[i];
    if (http_header_colon(header) == NULL) {
      fprintf(stderr, "curl: invalid request header: %s\n", header);
      return HTTP_RESULT_USAGE;
    }
    if (http_header_named(header, "Connection") ||
        http_header_named(header, "Content-Length") ||
        http_header_named(header, "Transfer-Encoding")) {
      fprintf(stderr, "curl: framing header %s is managed by curl.bin\n",
              header);
      return HTTP_RESULT_USAGE;
    }
    if (http_header_named(header, "Host") && ++host_headers > 1u) {
      fprintf(stderr, "curl: multiple Host headers are not allowed\n");
      return HTTP_RESULT_USAGE;
    }
  }
  return HTTP_RESULT_OK;
}

static http_result_t http_request_build(const http_options_t *options,
                                        const http_url_t *url,
                                        const char *method, bool send_data,
                                        http_buffer_t *request) {
  bool success = http_buffer_append_string(request, method) &&
                 http_buffer_append_string(request, " ") &&
                 http_buffer_append_string(request, url->target) &&
                 http_buffer_append_string(request, " HTTP/1.1\r\n");
  if (!http_options_have_header(options, "Host")) {
    success = success && http_buffer_append_string(request, "Host: ") &&
              http_buffer_append_string(request, url->host);
    if (url->port != HTTP_DEFAULT_PORT) {
      char port[8];
      int length = snprintf(port, sizeof(port), ":%u", (unsigned)url->port);
      success = success && length > 0 && (size_t)length < sizeof(port) &&
                http_buffer_append(request, port, (size_t)length);
    }
    success = success && http_buffer_append_string(request, "\r\n");
  }
  if (!http_options_have_header(options, "User-Agent")) {
    success = success &&
              http_buffer_append_string(request, "User-Agent: PlantOS-curl/1.0\r\n");
  }
  if (!http_options_have_header(options, "Accept")) {
    success = success && http_buffer_append_string(request, "Accept: */*\r\n");
  }
  if (send_data && !http_options_have_header(options, "Content-Type")) {
    success = success && http_buffer_append_string(
                             request,
                             "Content-Type: application/x-www-form-urlencoded\r\n");
  }
  if (send_data) {
    char length_text[32];
    int length = snprintf(length_text, sizeof(length_text),
                          "Content-Length: %u\r\n",
                          (unsigned)strlen(options->data));
    success = success && length > 0 && (size_t)length < sizeof(length_text) &&
              http_buffer_append(request, length_text, (size_t)length);
  }
  for (size_t i = 0; success && i < options->header_count; i++) {
    success = http_buffer_append_string(request, options->headers[i]) &&
              http_buffer_append_string(request, "\r\n");
  }
  success = success &&
            http_buffer_append_string(request, "Connection: close\r\n\r\n");
  if (!success) {
    return HTTP_RESULT_MEMORY;
  }
  return request->length > HTTP_HEADER_LIMIT ? HTTP_RESULT_PROTOCOL
                                              : HTTP_RESULT_OK;
}

static http_result_t http_send_all(socket_t socket_fd, const void *data,
                                   size_t length) {
  const uint8_t *bytes = data;
  while (length != 0) {
    uint32_t part = length > 65535u ? 65535u : (uint32_t)length;
    int sent = send(socket_fd, bytes, part, 0);
    if (sent <= 0) {
      return sent == SOCKET_ERR_TIMEDOUT ? HTTP_RESULT_TIMEOUT
                                         : HTTP_RESULT_TRANSFER;
    }
    bytes += (uint32_t)sent;
    length -= (uint32_t)sent;
  }
  return HTTP_RESULT_OK;
}

static http_result_t http_connect(const http_url_t *url, socket_t *socket_fd) {
  char service[8];
  int service_length =
      snprintf(service, sizeof(service), "%u", (unsigned)url->port);
  if (service_length <= 0 || (size_t)service_length >= sizeof(service)) {
    return HTTP_RESULT_URL;
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  struct addrinfo *addresses = NULL;
  int resolved = getaddrinfo(url->host, service, &hints, &addresses);
  if (resolved != 0 || addresses == NULL) {
    fprintf(stderr, "curl: could not resolve host %s (DNS error %d)\n",
            url->host, resolved);
    freeaddrinfo(addresses);
    return HTTP_RESULT_RESOLVE;
  }

  socket_t connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (connection < 0) {
    fprintf(stderr, "curl: could not create socket (%d)\n", connection);
    freeaddrinfo(addresses);
    return HTTP_RESULT_CONNECT;
  }
  struct timeval timeout = {.tv_sec = HTTP_RECEIVE_TIMEOUT_SECONDS,
                            .tv_usec = 0};
  int result = setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                          sizeof(timeout));
  if (result == 0) {
    result = connect(connection, addresses->ai_addr, addresses->ai_addrlen);
  }
  freeaddrinfo(addresses);
  if (result != 0) {
    fprintf(stderr, "curl: could not connect to %s:%u (%d)\n", url->host,
            (unsigned)url->port, result);
    socket_close(connection);
    return result == SOCKET_ERR_TIMEDOUT ? HTTP_RESULT_TIMEOUT
                                         : HTTP_RESULT_CONNECT;
  }
  *socket_fd = connection;
  return HTTP_RESULT_OK;
}

static int http_stream_fill(http_stream_t *stream) {
  if (stream->begin != stream->end) {
    return (int)(stream->end - stream->begin);
  }
  int received = recv(stream->socket, stream->buffer, sizeof(stream->buffer), 0);
  if (received > 0) {
    stream->begin = 0;
    stream->end = (size_t)received;
  }
  return received;
}

static int http_stream_read(http_stream_t *stream, void *destination,
                            size_t length) {
  int available = http_stream_fill(stream);
  if (available <= 0) {
    return available;
  }
  size_t part = length < (size_t)available ? length : (size_t)available;
  memcpy(destination, stream->buffer + stream->begin, part);
  stream->begin += part;
  return (int)part;
}

static http_result_t http_stream_read_line(http_stream_t *stream,
                                           http_buffer_t *line) {
  http_buffer_reset(line);
  for (;;) {
    int available = http_stream_fill(stream);
    if (available <= 0) {
      if (available == SOCKET_ERR_TIMEDOUT) {
        return HTTP_RESULT_TIMEOUT;
      }
      return available == 0 ? HTTP_RESULT_PROTOCOL : HTTP_RESULT_TRANSFER;
    }

    const uint8_t *begin = stream->buffer + stream->begin;
    const uint8_t *newline = memchr(begin, '\n', (size_t)available);
    size_t part = newline == NULL ? (size_t)available
                                  : (size_t)(newline - begin);
    if (line->length + part > HTTP_HEADER_LIMIT ||
        !http_buffer_append(line, begin, part)) {
      return line->length + part > HTTP_HEADER_LIMIT ? HTTP_RESULT_PROTOCOL
                                                      : HTTP_RESULT_MEMORY;
    }
    stream->begin += part + (newline != NULL ? 1u : 0u);
    if (newline != NULL) {
      if (line->length != 0 && line->data[line->length - 1u] == '\r') {
        line->data[--line->length] = '\0';
      }
      return HTTP_RESULT_OK;
    }
  }
}

static bool http_response_status_parse(const http_buffer_t *line,
                                       int *status) {
  if (line->length < 12u || strncmp(line->data, "HTTP/1.", 7u) != 0 ||
      (line->data[7] < '0' || line->data[7] > '9') ||
      line->data[8] != ' ' || line->data[9] < '1' || line->data[9] > '5' ||
      line->data[10] < '0' || line->data[10] > '9' || line->data[11] < '0' ||
      line->data[11] > '9' ||
      (line->length > 12u && line->data[12] != ' ')) {
    return false;
  }
  for (size_t i = 12u; i < line->length; i++) {
    unsigned char character = (unsigned char)line->data[i];
    if ((character < 0x20u && character != '\t') || character == 0x7fu) {
      return false;
    }
  }
  *status = (line->data[9] - '0') * 100 + (line->data[10] - '0') * 10 +
            line->data[11] - '0';
  return true;
}

static bool http_content_length_parse(http_response_t *response,
                                      const char *value, size_t length) {
  size_t position = 0;
  bool any = false;
  uint64_t expected = response->content_length;
  while (position < length) {
    while (position < length &&
           (value[position] == ' ' || value[position] == '\t')) {
      position++;
    }
    if (position == length || value[position] < '0' || value[position] > '9') {
      return false;
    }
    uint64_t parsed = 0;
    while (position < length && value[position] >= '0' &&
           value[position] <= '9') {
      unsigned digit = (unsigned)(value[position++] - '0');
      if (parsed > HTTP_UINT64_MAX / 10ull ||
          (parsed == HTTP_UINT64_MAX / 10ull &&
           digit > HTTP_UINT64_MAX % 10ull)) {
        return false;
      }
      parsed = parsed * 10ull + digit;
    }
    while (position < length &&
           (value[position] == ' ' || value[position] == '\t')) {
      position++;
    }
    if ((response->content_length_present || any) && parsed != expected) {
      return false;
    }
    expected = parsed;
    any = true;
    if (position == length) {
      break;
    }
    if (value[position++] != ',') {
      return false;
    }
    if (position == length) {
      return false;
    }
  }
  if (!any) {
    return false;
  }
  response->content_length = expected;
  response->content_length_present = true;
  return true;
}

static http_result_t http_response_header_parse(http_response_t *response,
                                                const http_buffer_t *line) {
  const char *colon = http_header_colon_n(line->data, line->length);
  if (colon == NULL) {
    return HTTP_RESULT_PROTOCOL;
  }
  const char *value = colon + 1;
  const char *end = line->data + line->length;
  while (value < end && (*value == ' ' || *value == '\t')) {
    value++;
  }
  while (end > value && (end[-1] == ' ' || end[-1] == '\t')) {
    end--;
  }
  size_t name_length = (size_t)(colon - line->data);
  size_t value_length = (size_t)(end - value);
  if (name_length == 14u &&
      strncasecmp(line->data, "Content-Length", name_length) == 0) {
    return http_content_length_parse(response, value, value_length)
               ? HTTP_RESULT_OK
               : HTTP_RESULT_PROTOCOL;
  }
  if (name_length == 17u &&
      strncasecmp(line->data, "Transfer-Encoding", name_length) == 0) {
    if (response->transfer != HTTP_TRANSFER_NONE || value_length != 7u ||
        strncasecmp(value, "chunked", value_length) != 0) {
      response->transfer = HTTP_TRANSFER_UNSUPPORTED;
    } else {
      response->transfer = HTTP_TRANSFER_CHUNKED;
    }
    return HTTP_RESULT_OK;
  }
  if (name_length == 8u &&
      strncasecmp(line->data, "Location", name_length) == 0) {
    if (response->location != NULL || value_length == 0) {
      return HTTP_RESULT_PROTOCOL;
    }
    response->location = http_string_copy(value, value_length);
    return response->location != NULL ? HTTP_RESULT_OK : HTTP_RESULT_MEMORY;
  }
  return HTTP_RESULT_OK;
}

static void http_response_destroy(http_response_t *response) {
  free(response->location);
  http_buffer_destroy(&response->raw_headers);
  memset(response, 0, sizeof(*response));
}

static http_result_t http_response_read(http_stream_t *stream,
                                        http_response_t *response) {
  memset(response, 0, sizeof(*response));
  http_buffer_t line = {0};
  http_result_t result = http_stream_read_line(stream, &line);
  if (result != HTTP_RESULT_OK || !http_response_status_parse(&line,
                                                               &response->status)) {
    http_buffer_destroy(&line);
    return result == HTTP_RESULT_OK ? HTTP_RESULT_PROTOCOL : result;
  }

  for (;;) {
    if (line.length > HTTP_HEADER_LIMIT - 2u ||
        response->raw_headers.length >
            HTTP_HEADER_LIMIT - line.length - 2u) {
      result = HTTP_RESULT_PROTOCOL;
      break;
    }
    if (!http_buffer_append(&response->raw_headers, line.data, line.length) ||
        !http_buffer_append_string(&response->raw_headers, "\r\n")) {
      result = HTTP_RESULT_MEMORY;
      break;
    }
    result = http_stream_read_line(stream, &line);
    if (result != HTTP_RESULT_OK) {
      break;
    }
    if (line.length == 0) {
      if (response->raw_headers.length > HTTP_HEADER_LIMIT - 2u) {
        result = HTTP_RESULT_PROTOCOL;
      } else if (!http_buffer_append_string(&response->raw_headers, "\r\n")) {
        result = HTTP_RESULT_MEMORY;
      }
      break;
    }
    result = http_response_header_parse(response, &line);
    if (result != HTTP_RESULT_OK) {
      break;
    }
  }
  http_buffer_destroy(&line);
  if (result != HTTP_RESULT_OK) {
    http_response_destroy(response);
  }
  return result;
}

static http_result_t http_output_write(FILE *output, const void *data,
                                       size_t length) {
  return length == 0 || fwrite(data, 1u, length, output) == length
             ? HTTP_RESULT_OK
             : HTTP_RESULT_WRITE;
}

static http_result_t http_stream_copy(http_stream_t *stream, FILE *output,
                                      uint64_t length, bool until_close) {
  while (until_close || length != 0) {
    int available = http_stream_fill(stream);
    if (available <= 0) {
      if (available == 0) {
        return until_close ? HTTP_RESULT_OK : HTTP_RESULT_PROTOCOL;
      }
      return available == SOCKET_ERR_TIMEDOUT ? HTTP_RESULT_TIMEOUT
                                               : HTTP_RESULT_TRANSFER;
    }
    size_t part = (size_t)available;
    if (!until_close && (uint64_t)part > length) {
      part = (size_t)length;
    }
    http_result_t result =
        http_output_write(output, stream->buffer + stream->begin, part);
    if (result != HTTP_RESULT_OK) {
      return result;
    }
    stream->begin += part;
    if (!until_close) {
      length -= part;
    }
  }
  return HTTP_RESULT_OK;
}

static bool http_chunk_size_parse(const http_buffer_t *line, uint64_t *size) {
  size_t position = 0;
  uint64_t value = 0;
  bool any = false;
  while (position < line->length) {
    unsigned digit;
    char character = line->data[position];
    if (character >= '0' && character <= '9') {
      digit = (unsigned)(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = (unsigned)(character - 'a') + 10u;
    } else if (character >= 'A' && character <= 'F') {
      digit = (unsigned)(character - 'A') + 10u;
    } else {
      break;
    }
    if (value > (HTTP_UINT64_MAX - digit) / 16ull) {
      return false;
    }
    value = value * 16ull + digit;
    position++;
    any = true;
  }
  while (position < line->length &&
         (line->data[position] == ' ' || line->data[position] == '\t')) {
    position++;
  }
  if (!any || (position < line->length && line->data[position] != ';')) {
    return false;
  }
  for (; position < line->length; position++) {
    unsigned char character = (unsigned char)line->data[position];
    if ((character < 0x20u && character != '\t') || character == 0x7fu) {
      return false;
    }
  }
  *size = value;
  return true;
}

static http_result_t http_stream_expect_crlf(http_stream_t *stream) {
  uint8_t ending[2];
  size_t received = 0;
  while (received < sizeof(ending)) {
    int part =
        http_stream_read(stream, ending + received, sizeof(ending) - received);
    if (part <= 0) {
      if (part == SOCKET_ERR_TIMEDOUT) {
        return HTTP_RESULT_TIMEOUT;
      }
      return part == 0 ? HTTP_RESULT_PROTOCOL : HTTP_RESULT_TRANSFER;
    }
    received += (size_t)part;
  }
  return ending[0] == '\r' && ending[1] == '\n' ? HTTP_RESULT_OK
                                                  : HTTP_RESULT_PROTOCOL;
}

static http_result_t http_stream_copy_chunked(http_stream_t *stream,
                                              FILE *output) {
  http_buffer_t line = {0};
  http_result_t result;
  for (;;) {
    result = http_stream_read_line(stream, &line);
    uint64_t chunk_size;
    if (result != HTTP_RESULT_OK || !http_chunk_size_parse(&line, &chunk_size)) {
      if (result == HTTP_RESULT_OK) {
        result = HTTP_RESULT_PROTOCOL;
      }
      break;
    }
    if (chunk_size == 0) {
      size_t trailer_size = 0;
      do {
        result = http_stream_read_line(stream, &line);
        if (result != HTTP_RESULT_OK) {
          break;
        }
        if (line.length != 0 &&
            http_header_colon_n(line.data, line.length) == NULL) {
          result = HTTP_RESULT_PROTOCOL;
          break;
        }
        if (line.length > HTTP_HEADER_LIMIT - 2u ||
            trailer_size > HTTP_HEADER_LIMIT - line.length - 2u) {
          result = HTTP_RESULT_PROTOCOL;
          break;
        }
        trailer_size += line.length + 2u;
      } while (line.length != 0);
      break;
    }
    result = http_stream_copy(stream, output, chunk_size, false);
    if (result != HTTP_RESULT_OK) {
      break;
    }
    result = http_stream_expect_crlf(stream);
    if (result != HTTP_RESULT_OK) {
      break;
    }
  }
  http_buffer_destroy(&line);
  return result;
}

static bool http_response_has_body(const char *method, int status) {
  return strcmp(method, "HEAD") != 0 && status >= 200 && status != 204 &&
         status != 205 && status != 304;
}

static http_result_t http_response_body(http_stream_t *stream,
                                        const http_response_t *response,
                                        FILE *output) {
  if (response->transfer != HTTP_TRANSFER_NONE) {
    if (response->transfer != HTTP_TRANSFER_CHUNKED) {
      return HTTP_RESULT_PROTOCOL;
    }
    return http_stream_copy_chunked(stream, output);
  }
  if (response->content_length_present) {
    return http_stream_copy(stream, output, response->content_length, false);
  }
  return http_stream_copy(stream, output, 0, true);
}

static bool http_redirect_status(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

static http_result_t http_perform(const http_options_t *options,
                                  FILE *output) {
  http_url_t url;
  http_url_result_t parsed = http_url_parse(options->url, &url);
  if (parsed != HTTP_URL_OK) {
    if (parsed == HTTP_URL_UNSUPPORTED) {
      fprintf(stderr, "curl: only HTTP URLs are supported\n");
    } else if (parsed != HTTP_URL_MEMORY) {
      fprintf(stderr, "curl: invalid URL: %s\n", options->url);
    }
    return parsed == HTTP_URL_MEMORY ? HTTP_RESULT_MEMORY : HTTP_RESULT_URL;
  }

  const char *method = options->method;
  bool send_data = options->data_present;
  http_result_t result = HTTP_RESULT_OK;
  for (unsigned redirects = 0;; redirects++) {
    socket_t connection = -1;
    result = http_connect(&url, &connection);
    if (result != HTTP_RESULT_OK) {
      break;
    }

    http_buffer_t request = {0};
    result = http_request_build(options, &url, method, send_data, &request);
    if (result == HTTP_RESULT_OK) {
      result = http_send_all(connection, request.data, request.length);
    }
    if (result == HTTP_RESULT_OK && send_data) {
      result = http_send_all(connection, options->data, strlen(options->data));
    }
    http_buffer_destroy(&request);
    if (result != HTTP_RESULT_OK) {
      socket_close(connection);
      break;
    }

    http_stream_t stream = {.socket = connection};
    http_response_t response;
    for (;;) {
      result = http_response_read(&stream, &response);
      if (result != HTTP_RESULT_OK) {
        break;
      }
      if (options->include_headers) {
        result = http_output_write(output, response.raw_headers.data,
                                   response.raw_headers.length);
        if (result != HTTP_RESULT_OK) {
          break;
        }
      }
      if (response.status < 100 || response.status >= 200) {
        break;
      }
      if (response.status == 101) {
        result = HTTP_RESULT_PROTOCOL;
        break;
      }
      http_response_destroy(&response);
    }
    if (result != HTTP_RESULT_OK) {
      http_response_destroy(&response);
      socket_close(connection);
      break;
    }

    bool redirect = options->follow_redirects &&
                    http_redirect_status(response.status) &&
                    response.location != NULL;
    if (!redirect) {
      if (http_response_has_body(method, response.status)) {
        result = http_response_body(&stream, &response, output);
      }
      http_response_destroy(&response);
      socket_close(connection);
      break;
    }
    if (redirects >= HTTP_REDIRECT_LIMIT) {
      result = HTTP_RESULT_REDIRECT;
      http_response_destroy(&response);
      socket_close(connection);
      break;
    }

    int redirect_status = response.status;
    http_url_t next;
    parsed = http_url_resolve(&url, response.location, &next);
    http_response_destroy(&response);
    socket_close(connection);
    if (parsed != HTTP_URL_OK) {
      result = parsed == HTTP_URL_MEMORY ? HTTP_RESULT_MEMORY
                                        : HTTP_RESULT_REDIRECT;
      if (parsed == HTTP_URL_UNSUPPORTED) {
        fprintf(stderr, "curl: redirect uses a non-HTTP URL\n");
      }
      break;
    }
    http_url_destroy(&url);
    url = next;
    if (strcmp(method, "HEAD") != 0 &&
        (redirect_status == 303 ||
         ((redirect_status == 301 || redirect_status == 302) &&
          strcmp(method, "POST") == 0))) {
      method = "GET";
      send_data = false;
    }
  }
  http_url_destroy(&url);
  return result;
}

static const char *http_result_message(http_result_t result) {
  switch (result) {
  case HTTP_RESULT_PROTOCOL:
    return "malformed or unsupported HTTP response";
  case HTTP_RESULT_TRANSFER:
    return "network transfer failed";
  case HTTP_RESULT_WRITE:
    return "failed to write output";
  case HTTP_RESULT_MEMORY:
    return "out of memory";
  case HTTP_RESULT_TIMEOUT:
    return "operation timed out";
  case HTTP_RESULT_REDIRECT:
    return "invalid redirect or redirect limit reached";
  default:
    return NULL;
  }
}

int main(int argc, char **argv) {
  http_options_t options;
  http_result_t result = http_options_parse(argc, argv, &options);
  if (result != HTTP_RESULT_OK || options.help || options.url == NULL) {
    free(options.headers);
    const char *message = http_result_message(result);
    if (message != NULL) {
      fprintf(stderr, "curl: %s\n", message);
    }
    return result;
  }

  FILE *output = stdout;
  bool close_output = options.output_path != NULL &&
                      strcmp(options.output_path, "-") != 0;
  if (close_output) {
    output = fopen(options.output_path, "wb");
    if (output == NULL) {
      fprintf(stderr, "curl: cannot open output file %s\n",
              options.output_path);
      free(options.headers);
      return HTTP_RESULT_WRITE;
    }
  }

  result = http_perform(&options, output);
  if ((close_output && fclose(output) != 0) ||
      (!close_output && fflush(output) != 0)) {
    result = HTTP_RESULT_WRITE;
  }
  free(options.headers);

  const char *message = http_result_message(result);
  if (message != NULL) {
    fprintf(stderr, "curl: %s\n", message);
  }
  return result;
}
