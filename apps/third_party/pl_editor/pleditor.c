/**
 * pleditor.c - Core editor implementation
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>

#include "pleditor.h"
#include "terminal.h"
#include "platform.h"
#include "syntax.h"

/* For calculating the render index from a chars index */
int pleditor_cx_to_rx(pleditor_row *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (PLEDITOR_TAB_STOP - 1) - (rx % PLEDITOR_TAB_STOP);
        rx++;
    }
    return rx;
}

static void pleditor_row_reserve(pleditor_row *row, size_t length) {
  if (length > INT_MAX)
    pleditor_platform_error("line too large");
  row->chars = pleditor_platform_reallocate(row->chars, length + 1);
}

/* Update the render string for a row (for handling tabs, etc.) */
void pleditor_update_row(pleditor_state *state, pleditor_row *row) {
    size_t tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    if (tabs > ((size_t)INT_MAX - row->size) / (PLEDITOR_TAB_STOP - 1))
        pleditor_platform_error("line too large to display");
    size_t length = (size_t)row->size + tabs * (PLEDITOR_TAB_STOP - 1);
    row->render = pleditor_platform_reallocate(row->render, length + 1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % PLEDITOR_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->render_size = idx;
}

/* Insert a row at the specified position */
void pleditor_insert_row(pleditor_state *state, int at, char *s, size_t len) {
    if (at < 0 || at > state->num_rows) return;

    if (len > INT_MAX || state->num_rows == INT_MAX ||
        (size_t)state->num_rows + 1 > SIZE_MAX / sizeof(pleditor_row))
        pleditor_platform_error("document too large");
    state->rows = pleditor_platform_reallocate(state->rows, sizeof(pleditor_row) * ((size_t)state->num_rows + 1));
    memmove(&state->rows[at + 1], &state->rows[at], sizeof(pleditor_row) * (state->num_rows - at));

    state->rows[at].size = len;
    state->rows[at].chars = pleditor_platform_reallocate(NULL, len + 1);
    memcpy(state->rows[at].chars, s, len);
    state->rows[at].chars[len] = '\0';

    state->rows[at].render_size = 0;
    state->rows[at].render = NULL;
    state->rows[at].hl = NULL;
    pleditor_update_row(state, &state->rows[at]);

    state->num_rows++;
    if (state->syntax)
        pleditor_syntax_update_multiline(state, at);
    state->dirty = true;
}

/* Free a row's memory */
void pleditor_free_row(pleditor_row *row) {
    free(row->render);
    free(row->chars);
    /* Free highlighting memory if allocated */
    if (row->hl) {
        free(row->hl->hl);
        free(row->hl);
    }
}

/* Delete a row at the specified position */
void pleditor_delete_row(pleditor_state *state, int at) {
    if (at < 0 || at >= state->num_rows) return;
    pleditor_free_row(&state->rows[at]);
    memmove(&state->rows[at], &state->rows[at + 1], sizeof(pleditor_row) * (state->num_rows - at - 1));
    state->num_rows--;
    state->dirty = true;
}

/* Insert a character at the current cursor position */
void pleditor_insert_char(pleditor_state *state, int c) {
    /* Don't insert control characters in the text (except TAB) */
    if (iscntrl(c) && c != '\t') {
        return;
    }

    /* Record the operation for undo - store the character being inserted */
    pleditor_operation_params params = {
        .type = OP_INSERT_CHAR,
        .cx = state->cx,
        .cy = state->cy,
        .character = c,
        .line = NULL,
        .line_size = 0
    };
    pleditor_record_operation(state, &params);

    if (state->cy == state->num_rows) {
        pleditor_insert_row(state, state->num_rows, "", 0);
    }

    pleditor_row *row = &state->rows[state->cy];
    pleditor_row_reserve(row, (size_t)row->size + 1);
    memmove(&row->chars[state->cx + 1], &row->chars[state->cx], row->size - state->cx + 1);
    row->size++;
    row->chars[state->cx] = c;
    pleditor_update_row(state, row);

    /* Update syntax highlighting for affected rows */
    if (state->syntax) {
        pleditor_syntax_update_multiline(state, state->cy);
    }

    state->cx++;
    state->dirty = true;
}

/* Insert a newline (Enter key) */
void pleditor_insert_newline(pleditor_state *state) {
    /* Before inserting a newline, we save information about the current row for undo */
    if (state->cy < state->num_rows) {
        pleditor_row *row = &state->rows[state->cy];

        /* Store a copy of the entire row for undoing properly */
        pleditor_operation_params params = {
            .type = OP_INSERT_LINE,
            .cx = state->cx,
            .cy = state->cy,
            .character = 0,
            .line = row->chars,
            .line_size = row->size
        };
        pleditor_record_operation(state, &params);
    } else {
        /* If we're at the end of the file, just record the position */
        pleditor_operation_params params = {
            .type = OP_INSERT_LINE,
            .cx = state->cx,
            .cy = state->cy,
            .character = 0,
            .line = NULL,
            .line_size = 0
        };
        pleditor_record_operation(state, &params);
    }

    if (state->cx == 0) {
        pleditor_insert_row(state, state->cy, "", 0);
    } else {
        pleditor_row *row = &state->rows[state->cy];
        pleditor_insert_row(state, state->cy + 1, &row->chars[state->cx], row->size - state->cx);
        row = &state->rows[state->cy]; /* Row pointer may have changed due to realloc */
        row->size = state->cx;
        row->chars[row->size] = '\0';
        pleditor_update_row(state, row);

        /* Update syntax highlighting for the modified current row and all affected rows */
        if (state->syntax) {
            pleditor_syntax_update_multiline(state, state->cy);
        }
    }
    state->cy++;
    state->cx = 0;
}

/* Delete the character at the current cursor position */
void pleditor_delete_char(pleditor_state *state) {
    if (state->cy == state->num_rows) return;
    if (state->cx == 0 && state->cy == 0) return;

    pleditor_row *row = &state->rows[state->cy];
    if (state->cx > 0) {
        /* Record the operation for undo - save the character that will be deleted */
        int del_char = row->chars[state->cx - 1];
        pleditor_operation_params params = {
            .type = OP_DELETE_CHAR,
            .cx = state->cx - 1,
            .cy = state->cy,
            .character = del_char,
            .line = NULL,
            .line_size = 0
        };
        pleditor_record_operation(state, &params);

        memmove(&row->chars[state->cx - 1], &row->chars[state->cx], row->size - state->cx + 1);
        row->size--;
        state->cx--;
        pleditor_update_row(state, row);

        /* Update syntax highlighting for affected rows */
        if (state->syntax) {
            pleditor_syntax_update_multiline(state, state->cy);
        }

        state->dirty = true;
    } else {
        /* At start of line or DEL at end of previous line */
        pleditor_row *prev_row = &state->rows[state->cy - 1];

        /* Save the line for undo */
        pleditor_operation_params params = {
            .type = OP_DELETE_LINE,
            .cx = prev_row->size,  /* Store previous row's end position */
            .cy = state->cy,
            .character = 0,
            .line = row->chars,
            .line_size = row->size
        };
        pleditor_record_operation(state, &params);

        /* Adjust cursor position for undo to beginning of previous row */
        state->cx = prev_row->size;

        /* Now merge the lines */
        pleditor_row_reserve(prev_row, (size_t)prev_row->size + row->size);
        memcpy(&prev_row->chars[prev_row->size], row->chars, row->size);
        prev_row->size += row->size;
        prev_row->chars[prev_row->size] = '\0';
        pleditor_update_row(state, prev_row);

        /* Update syntax highlighting for affected rows */
        if (state->syntax) {
            pleditor_syntax_update_multiline(state, state->cy - 1);
        }

        pleditor_delete_row(state, state->cy);
        state->cy--;
    }
}

/* Calculate the number of digits in a given number */
static int digit_count(int number) {
    if (number <= 0) return 1; /* Handle 0 and negative cases */

    int count = 0;
    while (number > 0) {
        number /= 10;
        count++;
    }
    return count;
}

/* Calculate the width needed for line numbers (digits + space) */
int pleditor_get_line_number_width(pleditor_state *state) {
    if (!state->show_line_numbers) {
        return 0;
    }

    /* Calculate the maximum visible line number on screen */
    int max_visible_line = state->num_rows;
    if (state->num_rows - state->row_offset > state->screen_rows)
        max_visible_line = state->row_offset + state->screen_rows;

    /* At least show single digit + space */
    int line_number_width = digit_count(max_visible_line) + 1;

    /* Ensure minimum of 2 characters wide + space */
    if (line_number_width < 3) {
        line_number_width = 3;
    }

    return line_number_width < state->screen_cols ? line_number_width : 0;
}

/* Scroll the editor if cursor is outside visible area */
void pleditor_scroll(pleditor_state *state) {
    state->rx = 0;
    if (state->cy < state->num_rows) {
        state->rx = pleditor_cx_to_rx(&state->rows[state->cy], state->cx);
    }

    /* Vertical scrolling */
    if (state->cy < state->row_offset) {
        state->row_offset = state->cy;
    }
    if (state->cy >= state->row_offset + state->screen_rows) {
        state->row_offset = state->cy - state->screen_rows + 1;
    }

    /* Horizontal scrolling */
    /* Calculate the effective screen width available for text,
     * accounting for line number display width if enabled */
    int effective_screen_width = state->screen_cols;
    if (state->show_line_numbers) {
        effective_screen_width -= pleditor_get_line_number_width(state);
    }

    if (state->rx < state->col_offset) {
        state->col_offset = state->rx;
    }
    if (state->rx >= state->col_offset + effective_screen_width) {
        state->col_offset = state->rx - effective_screen_width + 1;
    }
}

/* Build each repaint with checked growth rather than a guessed bytes-per-cell
 * allocation. Syntax changes can emit several control bytes for one glyph. */
typedef struct {
  char *data;
  size_t length, capacity;
} pleditor_buffer;

static void buffer_reserve(pleditor_buffer *buffer, size_t length) {
  if (length > SIZE_MAX - buffer->length - 1)
    pleditor_platform_error("screen update too large");
  size_t required = buffer->length + length + 1;
  if (required <= buffer->capacity)
    return;
  size_t capacity = buffer->capacity ? buffer->capacity : 1024;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2) {
      capacity = required;
      break;
    }
    capacity *= 2;
  }
  buffer->data = pleditor_platform_reallocate(buffer->data, capacity);
  buffer->capacity = capacity;
}

static void buffer_append(pleditor_buffer *buffer, const char *text,
                          size_t length) {
  buffer_reserve(buffer, length);
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = 0;
}

static void buffer_printf(pleditor_buffer *buffer, const char *format, ...) {
  va_list arguments, copy;
  va_start(arguments, format);
  va_copy(copy, arguments);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0)
    pleditor_platform_error("cannot format screen update");
  buffer_reserve(buffer, (size_t)length);
  vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length,
            format, arguments);
  va_end(arguments);
  buffer->length += (size_t)length;
}

static void pleditor_draw_rows(pleditor_state *state, pleditor_buffer *buffer) {
  int gutter = pleditor_get_line_number_width(state);
  int width = state->screen_cols - gutter;
  for (int y = 0; y < state->screen_rows; y++) {
    int row_index = y + state->row_offset;
    if (gutter) {
      if (row_index < state->num_rows)
        buffer_printf(buffer, "\033[%dm%*d \033[0m",
                      row_index == state->cy ? 37 : 90, gutter - 1,
                      row_index + 1);
      else
        buffer_printf(buffer, "%*s", gutter, "");
    }
    if (row_index >= state->num_rows) {
      if (state->num_rows == 0 && y == state->screen_rows / 3) {
        static const char welcome[] = "pleditor -- version " PLEDITOR_VERSION;
        int length = sizeof(welcome) - 1;
        if (length > width)
          length = width;
        int padding = (width - length) / 2;
        buffer_printf(buffer, "%*s%.*s", padding, "", length, welcome);
      } else {
        buffer_append(buffer, "~", 1);
      }
    } else {
      pleditor_row *row = &state->rows[row_index];
      int length = row->render_size - state->col_offset;
      if (length > width)
        length = width;
      int color = -1;
      for (int column = 0; column < length; column++) {
        int offset = state->col_offset + column;
        if (row->hl) {
          int next = pleditor_syntax_color_to_ansi(row->hl->hl[offset]);
          if (color != next) {
            buffer_printf(buffer, "\033[%dm", next);
            color = next;
          }
        }
        unsigned char byte = row->render[offset];
        char character = byte < 32 || byte == 127 ? '?' : (char)byte;
        buffer_append(buffer, &character, 1);
      }
    }
    static const char end[] = VT100_COLOR_RESET VT100_CLEAR_LINE "\r\n";
    buffer_append(buffer, end, sizeof(end) - 1);
  }
}

static void pleditor_draw_status_bar(pleditor_state *state,
                                     pleditor_buffer *buffer) {
  const char *filename = state->filename ? state->filename : "[No Name]";
  size_t path_length = strlen(filename);
  const char *prefix = "";
  if (path_length > 30) {
    prefix = "...";
    filename += path_length - 27;
  }
  char status[80], position[80];
  int left =
      snprintf(status, sizeof(status), "%s%s - %d lines %s", prefix, filename,
               state->num_rows, state->dirty ? "(modified)" : "");
  int right = snprintf(position, sizeof(position), "%s | %d/%d ",
                       state->syntax ? state->syntax->filetype : "no ft",
                       state->cy + 1, state->num_rows);
  if (right > state->screen_cols)
    right = state->screen_cols;
  if (left > state->screen_cols - right)
    left = state->screen_cols - right;
  buffer_printf(buffer, "\033[7m%.*s%*s%.*s\033[0m\r\n", left, status,
                state->screen_cols - left - right, "", right, position);
  buffer_printf(buffer, "\033[K%.*s", state->screen_cols, state->status_msg);
}

void pleditor_refresh_screen(pleditor_state *state) {
  pleditor_scroll(state);
  pleditor_buffer buffer = {0};
  static const char begin[] = VT100_CURSOR_HIDE VT100_CURSOR_HOME;
  buffer_append(&buffer, begin, sizeof(begin) - 1);
  pleditor_draw_rows(state, &buffer);
  pleditor_draw_status_bar(state, &buffer);
  int column =
      state->rx - state->col_offset + 1 + pleditor_get_line_number_width(state);
  buffer_printf(&buffer, "\033[%d;%dH\033[?25h",
                state->cy - state->row_offset + 1, column);
  pleditor_platform_write(buffer.data, buffer.length);
  free(buffer.data);
}

/* Set a status message to display in the message bar */
void pleditor_set_status_message(pleditor_state *state, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(state->status_msg, sizeof(state->status_msg), fmt, ap);
    va_end(ap);
}

/* Display a prompt in the status bar and get a input */
char* pleditor_prompt(pleditor_state *state, const char *prompt) {
    size_t bufsize = 128;
    char *buf = pleditor_platform_reallocate(NULL, bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        /* Display the prompt and current input */
        pleditor_set_status_message(state, "%s: %s", prompt, buf);
        pleditor_refresh_screen(state);

        int c = pleditor_platform_read_key();
        if (c == PLEDITOR_KEY_ERR)
            pleditor_platform_error("cannot read terminal");

        if (c == PLEDITOR_DEL_KEY || c == PLEDITOR_KEY_BACKSPACE) {
            /* Handle backspace/delete */
            if (buflen > 0) {
                buflen--;
                buf[buflen] = '\0';
            }
        } else if (c == '\r' || c == '\n') {
            /* Handle Enter/Return */
            if (buflen != 0) {
                pleditor_set_status_message(state, "");
                return buf;
            }
        } else if (c == PLEDITOR_KEY_ESC || c == PLEDITOR_CTRL_KEY('q')) {
            /* Handle escape or quit */
            pleditor_set_status_message(state, "");
            free(buf);
            return NULL;
        } else if (!iscntrl(c) && c < 128) {
            /* Append character to buffer */
            if (buflen == bufsize - 1) {
                if (bufsize > SIZE_MAX / 2)
                    pleditor_platform_error("input too long");
                bufsize *= 2;
                char *newbuf = pleditor_platform_reallocate(buf, bufsize);
                buf = newbuf;
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
}

/* Move the cursor based on key press */
void pleditor_move_cursor(pleditor_state *state, int key) {
    pleditor_row *row = (state->cy >= state->num_rows) ? NULL : &state->rows[state->cy];

    switch (key) {
        case PLEDITOR_ARROW_LEFT:
            if (state->cx > 0) {
                state->cx--;
            } else if (state->cy > 0) {
                /* Move to end of previous line */
                state->cy--;
                state->cx = state->rows[state->cy].size;
            }
            break;

        case PLEDITOR_ARROW_RIGHT:
            if (row && state->cx < row->size) {
                state->cx++;
            } else if (row && state->cx == row->size) {
                /* Move to beginning of next line */
                state->cy++;
                state->cx = 0;
            }
            break;

        case PLEDITOR_ARROW_UP:
            if (state->cy > 0) state->cy--;
            break;

        case PLEDITOR_ARROW_DOWN:
            if (state->cy < state->num_rows - 1) state->cy++;
            break;
    }

    /* Snap cursor to end of line if it's beyond line end */
    row = (state->cy >= state->num_rows) ? NULL : &state->rows[state->cy];
    int rowlen = row ? row->size : 0;
    if (state->cx > rowlen) {
        state->cx = rowlen;
    }
}

/* Save the current file */
void pleditor_save(pleditor_state *state) {
  bool new_name = state->filename == NULL;
  char *filename =
      new_name ? pleditor_prompt(state, "Save as") : state->filename;
  if (!filename) {
    pleditor_set_status_message(state, "Save aborted");
    return;
  }

  size_t length = 0;
  for (int row = 0; row < state->num_rows; row++) {
    size_t bytes = (size_t)state->rows[row].size + 1;
    if (bytes > SIZE_MAX - length - 1) {
      if (new_name)
        free(filename);
      pleditor_set_status_message(state, "File too large to save");
      return;
    }
    length += bytes;
  }
  char *buffer = pleditor_platform_reallocate(NULL, length + 1);
  char *cursor = buffer;
  for (int row = 0; row < state->num_rows; row++) {
    memcpy(cursor, state->rows[row].chars, state->rows[row].size);
    cursor += state->rows[row].size;
    *cursor++ = '\n';
  }

  if (pleditor_platform_write_file(filename, buffer, length)) {
    if (new_name) {
      state->filename = filename;
      pleditor_syntax_by_fileext(state, filename);
      pleditor_syntax_update_all(state);
    }
    state->dirty = false;
    pleditor_set_status_message(state, "%zu bytes written to disk", length);
  } else {
    if (new_name)
      free(filename);
    pleditor_set_status_message(state, "Can't save file (I/O error)");
  }
  free(buffer);
}

/* Process a keypress */
void pleditor_handle_keypress(pleditor_state *state, int c) {
    static int quit_times = PLEDITOR_QUIT_CONFIRM_TIMES;

    /* If in search mode, handle search-specific keys */
    if (state->is_searching) {
        switch (c) {
            case PLEDITOR_CTRL_KEY('n'):
                pleditor_search_next(state);
                return;
            case PLEDITOR_CTRL_KEY('p'):
                pleditor_search_previous(state);
                return;
            case '\r':
            case '\n':
            case PLEDITOR_KEY_ESC:
            case PLEDITOR_CTRL_KEY('q'):
                pleditor_search_exit(state);
                return;
        }
    }

    /* Clear status message on any keypress unless we're confirming quit */
    if (!(c == PLEDITOR_CTRL_KEY('q') && state->dirty && quit_times > 0)) {
        pleditor_set_status_message(state, "");
    }

    switch (c) {
        case PLEDITOR_CTRL_KEY('q'):
            if (state->dirty && quit_times > 0) {
                pleditor_set_status_message(state,
                    "WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            /* Clear screen and reposition cursor before exit */
            pleditor_platform_write(VT100_CLEAR_SCREEN VT100_CURSOR_HOME,
                                    sizeof(VT100_CLEAR_SCREEN VT100_CURSOR_HOME) - 1);
            state->should_quit = true;
            break;

        case PLEDITOR_CTRL_KEY('s'):
            pleditor_save(state);
            break;

        case PLEDITOR_CTRL_KEY('f'):
            pleditor_search_init(state);
            break;

        case PLEDITOR_CTRL_KEY('r'):
            state->show_line_numbers = !state->show_line_numbers;
            /* Update status message to show current line number state */
            pleditor_set_status_message(state, "Line numbers: %s",
                                     state->show_line_numbers ? "ON" : "OFF");
            break;

        case PLEDITOR_CTRL_KEY('z'):
            pleditor_apply_undo(state);
            break;

        case PLEDITOR_CTRL_KEY('y'):
            pleditor_apply_redo(state);
            break;

        case PLEDITOR_KEY_BACKSPACE:
        case PLEDITOR_CTRL_KEY('h'):
            pleditor_delete_char(state);
            break;

        case PLEDITOR_DEL_KEY:
            /* If we're at the end of the document, do nothing */
            if (state->num_rows == 0 ||
                (state->cy == state->num_rows - 1 &&
                 state->cx == state->rows[state->cy].size)) {
                break;
            }
            /* Store original cursor position before moving right */
            int orig_cx = state->cx;
            int orig_cy = state->cy;

            pleditor_move_cursor(state, PLEDITOR_ARROW_RIGHT);
            pleditor_delete_char(state);

            /* Update the undo operation with the original cursor position
             * and mark the position by making cx negative to signal it was a DEL operation */
            if (state->undo_stack && state->undo_stack->type == OP_DELETE_CHAR) {
                state->undo_stack->cx = -orig_cx - 1; /* Store as negative to mark DEL op */
                state->undo_stack->cy = orig_cy;
            }
            break;

        case '\r':
        case '\n':
            pleditor_insert_newline(state);
            break;

        case PLEDITOR_CTRL_KEY('l'):
        case PLEDITOR_KEY_ESC:
            /* Just refresh screen */
            break;

        case PLEDITOR_ARROW_UP:
        case PLEDITOR_ARROW_DOWN:
        case PLEDITOR_ARROW_LEFT:
        case PLEDITOR_ARROW_RIGHT:
            pleditor_move_cursor(state, c);
            break;

        case PLEDITOR_HOME_KEY:
            state->cx = 0;
            break;

        case PLEDITOR_END_KEY:
            if (state->cy < state->num_rows)
                state->cx = state->rows[state->cy].size;
            break;

        case PLEDITOR_PAGE_UP:
        case PLEDITOR_PAGE_DOWN:
            {
                if (state->num_rows == 0) break;
                if (c == PLEDITOR_PAGE_UP) {
                    state->cy = state->row_offset;
                } else if (c == PLEDITOR_PAGE_DOWN) {
                    state->cy = state->row_offset + state->screen_rows - 1;
                    if (state->cy > state->num_rows - 1) state->cy = state->num_rows - 1;
                }

                int times = state->screen_rows;
                while (times--)
                    pleditor_move_cursor(state,
                                      (c == PLEDITOR_PAGE_UP) ?
                                      PLEDITOR_ARROW_UP :
                                      PLEDITOR_ARROW_DOWN);
            }
            break;

        default:
            pleditor_insert_char(state, c);
            break;
    }

    quit_times = PLEDITOR_QUIT_CONFIRM_TIMES;
}

/* Initialize the editor state */
void pleditor_init(pleditor_state *state) {
    state->cx = 0;
    state->cy = 0;
    state->rx = 0;
    state->row_offset = 0;
    state->col_offset = 0;
    state->num_rows = 0;
    state->rows = NULL;
    state->dirty = false;
    state->filename = NULL;
    state->status_msg[0] = '\0';
    state->syntax = NULL;  /* No syntax highlighting by default */
    state->show_line_numbers = true; /* Line numbers enabled by default */
    state->undo_stack = NULL; /* Initialize the undo stack */
    state->redo_stack = NULL; /* Initialize the redo stack */
    state->is_unredoing = false; /* Initialize unredoing flag */
    state->should_quit = false; /* Initialize quit flag */

    /* Initialize search fields */
    state->is_searching = false;
    state->search_query = NULL;
    state->last_match_row = -1;
    state->last_match_col = -1;
    state->search_direction = SEARCH_FORWARD;

    if (!pleditor_platform_get_size(&state->screen_rows, &state->screen_cols)) {
        pleditor_platform_error("terminal size unavailable");
    }

    /* Leave room for status line and message bar */
    state->screen_rows -= 2;
}

/* Open a file in the editor */
bool pleditor_open(pleditor_state *state, const char *filename) {
    free(state->filename);

    /* Copy the filename to the state */
    state->filename = pleditor_platform_reallocate(NULL, strlen(filename) + 1);
    strcpy(state->filename, filename);

    char *buffer;
    size_t len;

    if (!pleditor_platform_read_file(filename, &buffer, &len)) {
        return false;
    }

    /* Parse the file contents into rows */
    char *line = buffer;
    char *end = buffer + len;
    char *eol;

    while (line < end) {
        /* Find the end of the current line */
        eol = memchr(line, '\n', (size_t)(end - line));

        size_t line_length;
        if (eol) {
            line_length = eol - line;
            eol++; /* Skip newline */
        } else {
            line_length = end - line;
            eol = end;
        }

        /* Add the line to our rows */
        pleditor_insert_row(state, state->num_rows, line, line_length);

        line = eol;
    }

    free(buffer);
    state->dirty = false;

    /* Select syntax highlighting based on filename */
    pleditor_syntax_by_fileext(state, filename);

    /* Apply syntax highlighting to all rows */
    pleditor_syntax_update_all(state);

    return true;
}

/* Free editor resources */
void pleditor_free(pleditor_state *state) {
    /* Free each row */
    for (int i = 0; i < state->num_rows; i++) {
        pleditor_free_row(&state->rows[i]);
    }
    free(state->rows);
    free(state->filename);
    free(state->search_query);
    pleditor_free_operation_stack(&state->undo_stack);
    pleditor_free_operation_stack(&state->redo_stack);
}

void pleditor_record_operation(pleditor_state *state, const pleditor_operation_params *params) {
    /* Don't record undo operations when undoing or redoing */
    if (state->is_unredoing) return;

    /* Clear redo stack when a new edit is made */
    pleditor_free_operation_stack(&state->redo_stack);

    pleditor_operation *op = pleditor_platform_reallocate(NULL, sizeof(pleditor_operation));

    op->type = params->type;
    op->cx = params->cx;
    op->cy = params->cy;
    op->character = params->character;
    op->line = NULL;
    op->line_size = params->line_size;

    if (params->line) {
        op->line = pleditor_platform_reallocate(NULL, (size_t)params->line_size + 1);
        if (op->line) {
            if (params->line_size > 0) {
                memcpy(op->line, params->line, params->line_size);
            }
            op->line[params->line_size] = '\0';
        }
    }

    op->next = state->undo_stack;
    state->undo_stack = op;
}

void pleditor_free_operation_stack(pleditor_operation **stack) {
    pleditor_operation *op = *stack;
    while (op) {
        pleditor_operation *next = op->next;
        if (op->line) free(op->line);
        free(op);
        op = next;
    }
    *stack = NULL;
}

void pleditor_apply_undo(pleditor_state *state) {
    if (!state->undo_stack) {
        pleditor_set_status_message(state, "Nothing to undo");
        return;
    }

    /* Set flag to prevent recording operations while undoing */
    state->is_unredoing = true;

    pleditor_operation *op = state->undo_stack;
    state->undo_stack = op->next;

    /* Save this operation to the redo stack */
    pleditor_operation *redo_op = pleditor_platform_reallocate(NULL, sizeof(pleditor_operation));

    redo_op->type = op->type;
    redo_op->cx = op->cx;
    redo_op->cy = op->cy;
    redo_op->character = op->character;
    redo_op->line = NULL;
    redo_op->line_size = op->line_size;

    if (op->line && op->line_size > 0) {
        redo_op->line = pleditor_platform_reallocate(NULL, (size_t)op->line_size + 1);
        if (redo_op->line) {
            memcpy(redo_op->line, op->line, op->line_size);
            redo_op->line[op->line_size] = '\0';
        }
    }

    redo_op->next = state->redo_stack;
    state->redo_stack = redo_op;

    switch (op->type) {
        case OP_INSERT_CHAR:
            /* For insert char, we need to delete the character that was inserted */
            state->cx = op->cx;
            state->cy = op->cy;
            if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];
                if (state->cx < row->size) {
                    /* Get the character for redo before deleting it */
                    redo_op->character = row->chars[state->cx];

                    /* Delete the character at the cursor position without pushing to undo stack again */
                    memmove(&row->chars[state->cx], &row->chars[state->cx + 1], row->size - state->cx);
                    row->size--;
                    pleditor_update_row(state, row);
                    state->dirty = true;
                    if (state->syntax) {
                        pleditor_syntax_update_multiline(state, state->cy);
                    }
                }
            }
            break;

        case OP_DELETE_CHAR:
            /* For delete char, we need to re-insert the character */
            bool is_del_operation = false;

            /* Check if this was a DEL operation (marked by negative cx) */
            if (op->cx < 0) {
                is_del_operation = true;
                state->cx = -op->cx - 1; /* Extract the original position */
            } else {
                state->cx = op->cx;
            }
            state->cy = op->cy;

            if (op->character != 0) {
                /* Insert character without pushing to undo stack again */
                if (state->cy == state->num_rows) {
                    pleditor_insert_row(state, state->num_rows, "", 0);
                }

                pleditor_row *row = &state->rows[state->cy];
                pleditor_row_reserve(row, (size_t)row->size + 1);
                memmove(&row->chars[state->cx + 1], &row->chars[state->cx], row->size - state->cx + 1);
                row->size++;
                row->chars[state->cx] = op->character;
                pleditor_update_row(state, row);

                if (state->syntax) {
                    pleditor_syntax_update_multiline(state, state->cy);
                }

                /* Only increment cursor for backspace, not for DEL */
                if (!is_del_operation) {
                    state->cx++;
                }
                state->dirty = true;
            }
            break;

        case OP_INSERT_LINE:
            /* For insert line, we need to properly merge the split lines back */
            state->cy = op->cy;
            if (state->cy < state->num_rows) {
                /* When we have the original line data from before the split */
                if (op->line) {
                    /* First handle the case of a proper newline (not at the beginning of a line) */
                    if (op->cx > 0) {
                        /* Delete the current row that was split */
                        pleditor_delete_row(state, state->cy);

                        /* Insert the original line content that was saved before splitting */
                        pleditor_insert_row(state, state->cy, op->line, op->line_size);

                        /* Delete any content from the next line that would be duplicate */
                        if ((state->cy + 1) < state->num_rows) {
                            /* Delete the next line (which was created by the Enter key) */
                            pleditor_delete_row(state, state->cy + 1);
                        }
                    } else {
                        /* For newline at the beginning of a line, just remove the empty line */
                        pleditor_delete_row(state, state->cy);
                    }

                    /* Update the dirty flag since we've modified the content */
                    state->dirty = true;
                } else {
                    /* For simple empty line insertion, just delete the row */
                    pleditor_delete_row(state, state->cy);
                    state->dirty = true;
                }

                /* Set cursor to the position before the newline was inserted */
                state->cx = op->cx;
            }
            break;

        case OP_DELETE_LINE:
            /* Only break if the line pointer is NULL */
            if (!op->line) break;

            /* Handle special case for Delete at end of line */
            if (op->cy > 0 && op->cy <= state->num_rows) {
                pleditor_row *prev_row = &state->rows[op->cy - 1];

                /* Check if this is a line joining operation (DEL at line end) */
                if (op->cx > 0) {
                    /* Truncate previous line to remove second line content */
                    prev_row->chars[op->cx] = '\0';
                    prev_row->size = op->cx;
                    pleditor_update_row(state, prev_row);

                    if (state->syntax) {
                        pleditor_syntax_update_multiline(state, op->cy - 1);
                    }
                } else {
                    /* Original backspace at line start case */
                    int match_start = prev_row->size - op->line_size;

                    /* Check if previous line ends with deleted line content */
                    if (prev_row->size >= op->line_size &&
                        memcmp(&prev_row->chars[match_start], op->line, op->line_size) == 0) {
                            /* Truncate previous line */
                            prev_row->chars[match_start] = '\0';
                            prev_row->size = match_start;
                            pleditor_update_row(state, prev_row);

                            if (state->syntax) {
                                pleditor_syntax_update_multiline(state, op->cy - 1);
                            }
                        }
                }
            }

            /* Re-insert the deleted line */
            pleditor_insert_row(state, op->cy, op->line, op->line_size);

            /* Position cursor at end of previous line for DEL at end of line case */
            if (op->cy > 0) {
                state->cy = op->cy - 1;
                state->cx = op->cx;
            } else {
                state->cy = op->cy;
                state->cx = 0;
            }

            state->dirty = true;
            break;
        }

    /* Clear the unredoing flag */
    state->is_unredoing = false;

    /* Free the undo operation */
    if (op->line) free(op->line);
    free(op);

    pleditor_set_status_message(state, "Undo successful");
}

void pleditor_apply_redo(pleditor_state *state) {
    if (!state->redo_stack) {
        pleditor_set_status_message(state, "Nothing to redo");
        return;
    }

    /* Set flag to prevent recording operations while redoing */
    state->is_unredoing = true;

    pleditor_operation *op = state->redo_stack;
    state->redo_stack = op->next;

    /* Save this operation to the undo stack */
    pleditor_operation *undo_op = pleditor_platform_reallocate(NULL, sizeof(pleditor_operation));

    undo_op->type = op->type;
    undo_op->cx = op->cx;
    undo_op->cy = op->cy;
    undo_op->character = op->character;
    undo_op->line = NULL;
    undo_op->line_size = op->line_size;

    if (op->line) {
        undo_op->line = pleditor_platform_reallocate(NULL, (size_t)op->line_size + 1);
        if (undo_op->line) {
            if (op->line_size > 0) {
                memcpy(undo_op->line, op->line, op->line_size);
            }
            undo_op->line[op->line_size] = '\0';
        }
    }

    undo_op->next = state->undo_stack;
    state->undo_stack = undo_op;

    switch (op->type) {
        case OP_INSERT_CHAR:
            /* For redo of insert, we need to re-insert the character */
            state->cx = op->cx;
            state->cy = op->cy;

            /* Insert character without pushing to undo stack again */
            if (state->cy == state->num_rows) {
                pleditor_insert_row(state, state->num_rows, "", 0);
            }

            if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];
                pleditor_row_reserve(row, (size_t)row->size + 1);
                memmove(&row->chars[state->cx + 1], &row->chars[state->cx], row->size - state->cx + 1);
                row->size++;
                row->chars[state->cx] = op->character;
                pleditor_update_row(state, row);

                if (state->syntax) {
                    pleditor_syntax_update_multiline(state, state->cy);
                }

                state->cx++;
                state->dirty = true;
            }
            break;

        case OP_DELETE_CHAR:
            /* For redo of delete, we need to delete the character again */
            bool is_del_operation = false;

            /* Check if this was a DEL operation (marked by negative cx) */
            if (op->cx < 0) {
                is_del_operation = true;
                state->cx = -op->cx - 1; /* Extract the original position */
            } else {
                state->cx = op->cx;
            }
            state->cy = op->cy;

            if (is_del_operation) {
                /* For DEL operation, simulate DEL key press */
                /* Move cursor right, then delete */
                pleditor_move_cursor(state, PLEDITOR_ARROW_RIGHT);
                pleditor_delete_char(state);
            } else if (state->cy < state->num_rows) {
                pleditor_row *row = &state->rows[state->cy];
                if (state->cx == 0 && state->cy > 0) {
                    /* This is a line join operation (deletion at beginning of line) */
                    /* Move cursor to the end of previous line where characters will be joined */
                    pleditor_row *prev_row = &state->rows[state->cy - 1];
                    int prev_row_size = prev_row->size;

                    /* Perform the delete character operation which will join the lines */
                    pleditor_delete_char(state);

                    /* Ensure cursor is at the join point */
                    state->cy = op->cy - 1;
                    state->cx = prev_row_size;
                } else if (state->cx < row->size) {
                    /* Delete the character at the cursor position */
                    memmove(&row->chars[state->cx], &row->chars[state->cx + 1], row->size - state->cx);
                    row->size--;
                    pleditor_update_row(state, row);
                    state->dirty = true;
                    if (state->syntax) {
                        pleditor_syntax_update_multiline(state, state->cy);
                    }
                }
            }
            break;

        case OP_INSERT_LINE:
            state->cx = op->cx;
            state->cy = op->cy;
            pleditor_insert_newline(state);
            break;

        case OP_DELETE_LINE:
            /* For redo of line delete, we need to delete the line again */
            state->cx = op->cx;
            state->cy = op->cy;

            if (state->cy < state->num_rows) {
                /* Update the undo operation to save the current line content */
                pleditor_row *current_row = &state->rows[state->cy];
                undo_op->line_size = current_row->size;

                /* Allocate and save the line content for proper undo */
                if (undo_op->line) {
                    free(undo_op->line);
                    undo_op->line = NULL;
                }

                undo_op->line = pleditor_platform_reallocate(NULL, (size_t)current_row->size + 1);
                if (undo_op->line) {
                    if (current_row->size > 0) {
                        memcpy(undo_op->line, current_row->chars, current_row->size);
                    }
                    undo_op->line[current_row->size] = '\0';
                }

                /* Check if this is a DEL key operation at the end of the previous line */
                if (state->cy > 0 && op->line) {
                    /* This is a DEL key at end of line case */
                    pleditor_row *prev_row = &state->rows[state->cy - 1];
                    int join_point = prev_row->size;

                    /* Merge the content with the previous line */
                    pleditor_row_reserve(prev_row, (size_t)prev_row->size + op->line_size);
                    if (op->line_size > 0) {
                        memcpy(&prev_row->chars[prev_row->size], op->line, op->line_size);
                    }
                    prev_row->size += op->line_size;
                    prev_row->chars[prev_row->size] = '\0';
                    pleditor_update_row(state, prev_row);

                    /* Update syntax highlighting for the next row and all affected rows */
                    if (state->syntax) {
                        pleditor_syntax_update_multiline(state, state->cy - 1);
                    }

                    /* Delete the line */
                    pleditor_delete_row(state, state->cy);

                    /* Position cursor at the join point */
                    state->cy--;
                    state->cx = join_point;
                } else {
                    /* Regular line delete */
                    pleditor_delete_row(state, state->cy);

                    /* If this was a DEL at end of line, position cursor at end of previous line */
                    if (state->cy > 0 && op->cx > 0) {
                        state->cy--;
                        state->cx = op->cx;
                    }
                }
                state->dirty = true;
            }
            break;
    }

    /* Clear the redoing flag */
    state->is_unredoing = false;

    /* Free the redo operation */
    if (op->line) free(op->line);
    free(op);

    pleditor_set_status_message(state, "Redo successful");
}

/**
 * Initialize search mode with a prompt for the query
 */
void pleditor_search_init(pleditor_state *state) {
    char *query = pleditor_prompt(state, "Searching");
    if (query == NULL) {
        return;
    }

    /* Free any existing search query */
    if (state->search_query) {
        free(state->search_query);
    }

    /* Save the search query and initialize search state */
    state->search_query = query;
    state->is_searching = true;
    state->last_match_row = -1;
    state->last_match_col = -1;
    state->search_direction = SEARCH_FORWARD;

    /* Perform initial search */
    pleditor_search_next(state);
}

/**
 * Find the next occurrence of the search query
 */
void pleditor_search_next(pleditor_state *state) {
    if (!state->search_query || state->num_rows == 0) {
        return;
    }

    state->search_direction = SEARCH_FORWARD;

    /* Start from the current position or the last match + 1 */
    int start_row = (state->last_match_row == -1) ? state->cy : state->last_match_row;
    int start_col = (state->last_match_col == -1) ? state->cx + 1 : state->last_match_col + 1;

    /* Loop through rows starting from the current position */
    for (int i = 0; i < state->num_rows; i++) {
        int current_row = (start_row + i) % state->num_rows;
        pleditor_row *row = &state->rows[current_row];

        /* If we've wrapped around to the first row, make sure we start from beginning */
        int col_offset = (i == 0) ? start_col : 0;

        if (col_offset > row->size) {
            /* If we're beyond the end of this row, move to the next one */
            continue;
        }

        /* Look for the search term in this row */
        char *match = strstr(row->chars + col_offset, state->search_query);
        if (match) {
            /* Found a match! */
            int match_col = match - row->chars;

            /* Update cursor position to the match */
            state->cy = current_row;
            state->cx = match_col;

            /* Save the match position */
            state->last_match_row = current_row;
            state->last_match_col = match_col;

            /* Ensure the match is visible on screen */
            state->row_offset = state->cy - (state->screen_rows / 2);
            if (state->row_offset < 0) state->row_offset = 0;

            pleditor_set_status_message(state, "Match found ('%s'). Ctrl-N for next, Ctrl-P for previous.",
                                     state->search_query);
            return;
        }
    }

    /* No match found */
    pleditor_set_status_message(state, "No match found for '%s'", state->search_query);

    /* Reset last match position */
    state->last_match_row = -1;
    state->last_match_col = -1;
}

/**
 * Find the previous occurrence of the search query
 */
void pleditor_search_previous(pleditor_state *state) {
    if (!state->search_query || state->num_rows == 0) {
        return;
    }

    state->search_direction = SEARCH_BACKWARD;

    /* Store current state to restore if no match is found */
    int original_cy = state->cy;
    int original_cx = state->cx;

    /* Start searching from one character before current position */
    int start_row = (state->last_match_row == -1) ? state->cy : state->last_match_row;
    int start_col = (state->last_match_col == -1 || state->last_match_col == 0) ?
                    ((start_row > 0) ? state->rows[start_row-1].size : 0) :
                    state->last_match_col - 1;

    /* If we're at the beginning of the file, wrap to the end */
    if (start_row == 0 && start_col == 0) {
        start_row = state->num_rows - 1;
        start_col = state->rows[start_row].size;
    }

    /* Loop through rows in reverse */
    for (int i = 0; i < state->num_rows; i++) {
        int current_row = (start_row - i + state->num_rows) % state->num_rows;
        pleditor_row *row = &state->rows[current_row];

        /* For the first row, start from the specified column */
        int search_limit = (i == 0) ? start_col : row->size;

        /* Search backward in this row */
        int match_col = -1;
        if ((size_t)search_limit >= strlen(state->search_query)) {
            for (size_t j = 0; j <= (size_t)(search_limit - strlen(state->search_query)); j++) {
                if (strncmp(row->chars + j, state->search_query, strlen(state->search_query)) == 0) {
                    match_col = (int)j;
                }
            }
        }

        if (match_col != -1) {
            /* Found a match! */
            state->cy = current_row;
            state->cx = match_col;

            /* Save the match position */
            state->last_match_row = current_row;
            state->last_match_col = match_col;

            /* Ensure the match is visible on screen */
            state->row_offset = state->cy - (state->screen_rows / 2);
            if (state->row_offset < 0) state->row_offset = 0;

            pleditor_set_status_message(state, "Match found ('%s'). Ctrl-N for next, Ctrl-P for previous.",
                                     state->search_query);
            return;
        }
    }

    /* No match found, restore original position */
    state->cy = original_cy;
    state->cx = original_cx;

    pleditor_set_status_message(state, "No match found for '%s'", state->search_query);

    /* Reset last match position */
    state->last_match_row = -1;
    state->last_match_col = -1;
}

/**
 * Exit search mode
 */
void pleditor_search_exit(pleditor_state *state) {
    state->is_searching = false;
    pleditor_set_status_message(state, "Search exited");
}
