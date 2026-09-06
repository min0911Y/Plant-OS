/**
 * pleditor.h - Platform-independent core for PL Editor
 */
#ifndef PLEDITOR_H
#define PLEDITOR_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "syntax.h"

/* Editor config */
#define PLEDITOR_VERSION "0.1.0"
#define PLEDITOR_TAB_STOP 4
#define PLEDITOR_QUIT_CONFIRM_TIMES 3

/* Key definitions */
#define PLEDITOR_CTRL_KEY(k) ((k) & 0x1f)
#define PLEDITOR_KEY_ESC '\x1b'
#define PLEDITOR_KEY_BACKSPACE 127

/* Special key codes */
enum pleditor_key {
    PLEDITOR_KEY_ERR = -1,
    PLEDITOR_ARROW_LEFT = 1000,
    PLEDITOR_ARROW_RIGHT,
    PLEDITOR_ARROW_UP,
    PLEDITOR_ARROW_DOWN,
    PLEDITOR_PAGE_UP,
    PLEDITOR_PAGE_DOWN,
    PLEDITOR_HOME_KEY,
    PLEDITOR_END_KEY,
    PLEDITOR_DEL_KEY,
};

/* Direction for search */
enum pleditor_search_direction {
    SEARCH_FORWARD,
    SEARCH_BACKWARD
};

/* Undo/Redo operation types */
enum pleditor_operation_type {
    OP_INSERT_CHAR,
    OP_DELETE_CHAR,
    OP_INSERT_LINE,
    OP_DELETE_LINE
};

/* Undo/Redo operation parameters */
typedef struct pleditor_operation_params {
    enum pleditor_operation_type type;
    int cx, cy;        /* Cursor position */
    int character;     /* Character for insert/delete operations */
    char *line;        /* Line content */
    int line_size;     /* Size of the line */
} pleditor_operation_params;

/* Undo/Redo operation structure */
typedef struct pleditor_operation {
    enum pleditor_operation_type type;
    int cx, cy;        /* Cursor position before the operation */
    int character;     /* Character for insert/delete operations */
    char *line;        /* Line content for line operations */
    int line_size;     /* Size of the line */
    struct pleditor_operation *next;
} pleditor_operation;

/* Row of text in the editor */
typedef struct pleditor_row {
    int size;          /* Size of the text */
    char *chars;       /* Raw text content */
    int render_size;   /* Size of the rendered text */
    char *render;      /* Rendered text (with tab expansion) */
    pleditor_highlight_row *hl; /* Syntax highlighting for this row */
} pleditor_row;

/* Editor state */
typedef struct pleditor_state {
    int cx, cy;              /* Cursor position */
    int rx;                  /* Render X position (for tabs) */
    int row_offset;          /* Row scroll offset */
    int col_offset;          /* Column scroll offset */
    int screen_rows;         /* Number of visible rows */
    int screen_cols;         /* Number of visible columns */
    int num_rows;            /* Number of rows in file */
    pleditor_row *rows;      /* File content */
    bool dirty;              /* File has unsaved changes */
    char *filename;          /* Currently open filename */
    char status_msg[80];     /* Status message */
    pleditor_syntax *syntax; /* Current syntax highlighting */
    bool show_line_numbers;  /* Whether to display line numbers */
    pleditor_operation *undo_stack; /* Stack of undo operations */
    pleditor_operation *redo_stack; /* Stack of redo operations */
    bool should_quit;        /* Flag to indicate editor should exit */
    bool is_unredoing;       /* Flag to prevent recursive undo/redo operations */
    bool is_searching;       /* Flag to indicate search mode */
    char *search_query;      /* Current search query */
    int last_match_row;      /* Row of the last match found */
    int last_match_col;      /* Column of the last match found */
    enum pleditor_search_direction search_direction; /* Direction for search */
} pleditor_state;

/* Function prototypes */
void pleditor_init(pleditor_state *state);
void pleditor_free(pleditor_state *state);
bool pleditor_open(pleditor_state *state, const char *filename);
void pleditor_save(pleditor_state *state);

void pleditor_insert_char(pleditor_state *state, int c);
void pleditor_delete_char(pleditor_state *state);
void pleditor_insert_newline(pleditor_state *state);

void pleditor_refresh_screen(pleditor_state *state);
void pleditor_set_status_message(pleditor_state *state, const char *fmt, ...);
char* pleditor_prompt(pleditor_state *state, const char *prompt);
int pleditor_get_line_number_width(pleditor_state *state);
void pleditor_move_cursor(pleditor_state *state, int key);
void pleditor_handle_keypress(pleditor_state *state, int c);

void pleditor_record_operation(pleditor_state *state, const pleditor_operation_params *params);
void pleditor_apply_undo(pleditor_state *state);
void pleditor_apply_redo(pleditor_state *state);
void pleditor_free_operation_stack(pleditor_operation **stack);

void pleditor_search_init(pleditor_state *state);
void pleditor_search_next(pleditor_state *state);
void pleditor_search_previous(pleditor_state *state);
void pleditor_search_exit(pleditor_state *state);

#endif /* PLEDITOR_H */
