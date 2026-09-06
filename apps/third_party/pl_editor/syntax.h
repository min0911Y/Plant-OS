/**
 * syntax.h - Syntax highlighting for pleditor
 */
#ifndef SYNTAX_H
#define SYNTAX_H

#include <stdbool.h>

/* Highlight types */
enum pleditor_highlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MULTILINE_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_PUNCTUATION,
    HL_FUNC_CLASS_NAME
};

/* Data structure for highlighting in a row */
typedef struct pleditor_highlight_row {
    unsigned char *hl;          /* Highlighting for each character */
    bool hl_multiline_comment;  /* Is this row part of a multi-line comment */
} pleditor_highlight_row;

/* Syntax definition structure */
typedef struct pleditor_syntax {
    char *filetype;         /* Language/filetype name */
    char **filematch;       /* File patterns that match this syntax */
    char **keywords;        /* Keywords for the language */
    char *singleline_comment_start;  /* Single line comment start */
    char *multiline_comment_start;   /* Multi-line comment start */
    char *multiline_comment_end;     /* Multi-line comment end */
    bool flags;             /* Syntax flags */
} pleditor_syntax;

/* Forward declarations for structs defined in pleditor.h */
struct pleditor_state;
typedef struct pleditor_state pleditor_state;

/* Function prototypes */
bool pleditor_syntax_init(pleditor_state *state);
int pleditor_syntax_color_to_ansi(int hl);
void pleditor_syntax_by_fileext(pleditor_state *state, const char *filename);
void pleditor_syntax_update_row(pleditor_state *state, int row_idx);
void pleditor_syntax_update_all(pleditor_state *state);
void pleditor_syntax_update_multiline(pleditor_state *state, int start_row);

#endif /* SYNTAX_H */
