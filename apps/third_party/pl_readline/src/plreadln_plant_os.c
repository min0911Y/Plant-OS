//
// This file is part of pl_readline.
// pl_readline is free software: you can redistribute it and/or modify
// it under the terms of MIT license. See file LICENSE for details.
//

#include "pl_readline.h"

#include <limits.h>
#include <syscall.h>

static int plant_os_getch(void) {
    for (;;) {
        int input = getch();
        switch (input) {
        case KEY_INPUT_UP:
            return PL_READLINE_KEY_UP;
        case KEY_INPUT_DOWN:
            return PL_READLINE_KEY_DOWN;
        case KEY_INPUT_LEFT:
            return PL_READLINE_KEY_LEFT;
        case KEY_INPUT_RIGHT:
            return PL_READLINE_KEY_RIGHT;
        case '\n':
            return PL_READLINE_KEY_ENTER;
        case '\b':
            return PL_READLINE_KEY_BACKSPACE;
        case '\t':
            return PL_READLINE_KEY_TAB;
        default:
            break;
        }
        if (input >= ' ' && input <= UCHAR_MAX && input != 0x7f) {
            return input;
        }
    }
}

static int plant_os_putch(int input) {
    putch((char)input);
    return input;
}

static void plant_os_flush(void) {}

pl_readline_t pl_readline_init_plant_os(
    void (*get_words)(char *buf, pl_readline_words_t words)) {
    return pl_readline_init(plant_os_getch, plant_os_putch, plant_os_flush,
                            get_words);
}
