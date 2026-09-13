#include <copyfile.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

struct copyfile_state {
  copyfile_callback_t callback;
  void *context;
};

copyfile_state_t copyfile_state_alloc(void) {
  return calloc(1, sizeof(struct copyfile_state));
}

int copyfile_state_free(copyfile_state_t state) {
  free(state);
  return 0;
}

int copyfile_state_set(copyfile_state_t state, unsigned int key,
                       const void *value) {
  if (state == NULL || value == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (key == COPYFILE_STATE_STATUS_CB) {
    state->callback = (copyfile_callback_t)(uintptr_t)value;
  } else if (key == COPYFILE_STATE_STATUS_CTX) {
    state->context = (void *)value;
  } else {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

int fcopyfile(int source, int destination, copyfile_state_t state,
              copyfile_flags_t flags) {
  if ((flags & COPYFILE_DATA) == 0) {
    errno = ENOTSUP;
    return -1;
  }
  unsigned char buffer[8192];
  for (;;) {
    ssize_t count = read(source, buffer, sizeof(buffer));
    if (count < 0) {
      return -1;
    }
    if (count == 0) {
      return 0;
    }
    size_t offset = 0;
    while (offset < (size_t)count) {
      ssize_t written = write(destination, buffer + offset,
                              (size_t)count - offset);
      if (written <= 0) {
        if (written == 0) {
          errno = EIO;
        }
        return -1;
      }
      offset += (size_t)written;
    }
    if (state != NULL && state->callback != NULL &&
        state->callback(COPYFILE_COPY_DATA, COPYFILE_PROGRESS, state, NULL,
                        NULL, state->context) == COPYFILE_QUIT) {
      errno = ECANCELED;
      return -1;
    }
  }
}
