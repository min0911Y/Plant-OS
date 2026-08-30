//
// This file is part of pl_readline.
// pl_readline is free software: you can redistribute it and/or modify
// it under the terms of MIT license. See file LICENSE for details.
//

#include "pl_readline.h"

#include <stdio.h>
#include <string.h>

void pl_readline_redisplay(_self, int show_prompt) {
    size_t prompt_len = self->prompt == NULL ? 0 : strlen(self->prompt);
    char cursor[16];

    pl_readline_print(self, "\r");
    if (show_prompt) {
        if (self->prompt != NULL) pl_readline_print(self, self->prompt);
    } else if (prompt_len != 0) {
        sprintf(cursor, "\033[%uC", (unsigned)prompt_len);
        pl_readline_print(self, cursor);
    }
    pl_readline_print(self, "\033[K");
    pl_readline_print(self, self->buffer);

    if (self->ptr != self->length) {
        pl_readline_print(self, "\r");
        sprintf(cursor, "\033[%uC", (unsigned)(prompt_len + self->ptr));
        pl_readline_print(self, cursor);
    }
}
