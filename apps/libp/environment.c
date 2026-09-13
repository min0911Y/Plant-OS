#include "runtime_lifecycle.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct environment_entry {
  struct environment_entry *next;
  char *value;
  char name[];
} environment_entry_t;
static environment_entry_t *environment;
pthread_mutex_t runtime_environment_lock = PTHREAD_MUTEX_INITIALIZER;
intptr_t environment_query(const char *name, size_t name_size, char *buffer,
                           size_t capacity);

static environment_entry_t *environment_find(const char *name) {
  for (environment_entry_t *entry = environment; entry; entry = entry->next)
    if (!strcmp(entry->name, name))
      return entry;
  return NULL;
}

/* Called with the environment lock; all allocations precede publication. */
static environment_entry_t *environment_store(environment_entry_t *entry,
                                              const char *name, char *value) {
  if (!entry) {
    size_t size = strlen(name) + 1;
    if (size > SIZE_MAX - sizeof(*entry)) {
      free(value);
      errno = ENOMEM;
      return NULL;
    }
    entry = malloc(sizeof(*entry) + size);
    if (!entry) {
      free(value);
      return NULL;
    }
    entry->value = value;
    memcpy(entry->name, name, size);
    entry->next = environment;
    environment = entry;
    return entry;
  }
  free(entry->value);
  entry->value = value;
  return entry;
}

char *getenv(const char *name) {
  if (!name || !*name || strchr(name, '='))
    return NULL;
  pthread_mutex_lock(&runtime_environment_lock);
  environment_entry_t *entry = environment_find(name);
  while (!entry) {
    intptr_t length = environment_query(name, strlen(name) + 1, NULL, 0);
    if (length < 0 || (uintptr_t)length == SIZE_MAX)
      break;
    char *value = malloc((size_t)length + 1);
    if (!value)
      break;
    intptr_t result =
        environment_query(name, strlen(name) + 1, value, (size_t)length + 1);
    if (result == -EOVERFLOW) {
      free(value);
      continue;
    }
    if (result >= 0)
      entry = environment_store(NULL, name, value);
    else
      free(value);
    break;
  }
  char *value = entry ? entry->value : NULL;
  pthread_mutex_unlock(&runtime_environment_lock);
  return value;
}

int setenv(const char *name, const char *value, int overwrite) {
  if (!name || !*name || strchr(name, '=') || !value) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&runtime_environment_lock);
  environment_entry_t *entry = environment_find(name);
  bool exists = entry ? entry->value != NULL
                      : environment_query(name, strlen(name) + 1, NULL, 0) >= 0;
  int result = 0;
  if (overwrite || !exists) {
    char *copy = strdup(value);
    if (!copy || !environment_store(entry, name, copy))
      result = -1;
  }
  pthread_mutex_unlock(&runtime_environment_lock);
  return result;
}
int unsetenv(const char *name) {
  if (!name || !*name || strchr(name, '=')) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&runtime_environment_lock);
  environment_entry_t *entry = environment_find(name);
  bool exists =
      entry || environment_query(name, strlen(name) + 1, NULL, 0) >= 0;
  int result = exists && !environment_store(entry, name, NULL) ? -1 : 0;
  pthread_mutex_unlock(&runtime_environment_lock);
  return result;
}

int putenv(char *entry) {
  if (!entry) {
    errno = EINVAL;
    return -1;
  }
  char *separator = strchr(entry, '=');
  if (separator == entry) {
    errno = EINVAL;
    return -1;
  }
  size_t name_size = (size_t)(separator - entry);
  char *name = malloc(name_size + 1);
  if (!name)
    return -1;
  memcpy(name, entry, name_size);
  name[name_size] = '\0';
  int result = setenv(name, separator + 1, 1);
  free(name);
  return result;
}
