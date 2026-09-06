/**
 * terminal.h - VT100 terminal control sequences
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdio.h>

/* VT100 Terminal Control Sequences */
#define VT100_CLEAR_SCREEN "\x1b[2J"
#define VT100_CLEAR_LINE "\x1b[K"
#define VT100_CURSOR_HOME "\x1b[H"
#define VT100_CURSOR_HIDE "\x1b[?25l"
#define VT100_CURSOR_SHOW "\x1b[?25h"

/* Position cursor at row,col (1-based) */
#define VT100_CURSOR_POSITION(row, col) "\x1b[" #row ";" #col "H"

/* ANSI Terminal Colors */
#define VT100_COLOR_RESET "\x1b[0m"
#define VT100_COLOR_BLACK "\x1b[30m"
#define VT100_COLOR_RED "\x1b[31m"
#define VT100_COLOR_GREEN "\x1b[32m"
#define VT100_COLOR_YELLOW "\x1b[33m"
#define VT100_COLOR_BLUE "\x1b[34m"
#define VT100_COLOR_MAGENTA "\x1b[35m"
#define VT100_COLOR_CYAN "\x1b[36m"
#define VT100_COLOR_WHITE "\x1b[37m"
#define VT100_COLOR_DARK_GRAY "\x1b[0;90m"  /* Bright black = dark gray */

/* Background colors */
#define VT100_BG_BLACK "\x1b[40m"
#define VT100_BG_RED "\x1b[41m"
#define VT100_BG_GREEN "\x1b[42m"
#define VT100_BG_YELLOW "\x1b[43m"
#define VT100_BG_BLUE "\x1b[44m"
#define VT100_BG_MAGENTA "\x1b[45m"
#define VT100_BG_CYAN "\x1b[46m"
#define VT100_BG_WHITE "\x1b[47m"

/* Text formatting */
#define VT100_BOLD "\x1b[1m"
#define VT100_UNDERLINE "\x1b[4m"
#define VT100_BLINK "\x1b[5m"
#define VT100_INVERSE "\x1b[7m"

/* Function to format cursor position */
static inline char* cursor_position(int row, int col, char* buffer) {
    sprintf(buffer, "\x1b[%d;%dH", row, col);
    return buffer;
}

#endif /* TERMINAL_H */
