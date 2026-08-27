//
// This file is part of pl_readline.
// pl_readline is free software: you can redistribute it and/or modify
// it under the terms of MIT license.
// See file LICENSE or https://opensource.org/licenses/MIT for full license
// details.
//
// Copyright (c) 2024 min0911_ https://github.com/min0911Y
//

// pl_readline_util.c : 实用函数实现

#include "pl_readline.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>


void pl_readline_insert_char(char *str, char ch, int idx) {
    int len = strlen(str) + 1; // 还要复制字符串结束符
    if (len) memmove(str + idx + 1, str + idx, len - idx);
    str[idx] = ch;
}

void pl_readline_delete_char(char *str, int idx) {
    int len = strlen(str);
    if (len) memmove(str + idx, str + idx + 1, len - idx);
    str[len] = '\0';
}

void pl_readline_print(_self, char *str) {
    while (*str) {
        self->pl_readline_hal_putch(*str++);
    }
}
