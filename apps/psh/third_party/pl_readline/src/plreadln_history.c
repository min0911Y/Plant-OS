//
// This file is part of pl_readline.
// pl_readline is free software: you can redistribute it and/or modify
// it under the terms of MIT license.
// See file LICENSE or https://opensource.org/licenses/MIT for full license
// details.
//
// Copyright (c) 2024 min0911_ https://github.com/min0911Y
//

// pl_readline_history.c : 历史记录功能

#include "pl_readline.h"
#include <stdio.h>
#include <string.h>


int pl_readline_add_history(_self, char *line) {
    char *copy = strdup(line);
    if (copy == NULL) return PL_READLINE_FAILED;
    pl_list_t node = pl_list_alloc(copy);
    if (node == NULL) {
        free(copy);
        return PL_READLINE_FAILED;
    }
    node->next = self->history;
    if (self->history != NULL) self->history->prev = node;
    self->history = node;
    return PL_READLINE_SUCCESS;
}

int pl_readline_modify_history(_self) {
    pl_list_t node = pl_list_nth(self->history, self->history_idx);
    if (node == NULL) return PL_READLINE_FAILED;
    char *copy = strdup(self->buffer);
    if (copy == NULL) return PL_READLINE_FAILED;
    free(node->data);
    node->data = copy;
    return PL_READLINE_SUCCESS;
}
#if PL_ENABLE_HISTORY_FILE
void pl_readline_save_history(_self, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;
    // Write history in reverse (oldest first)
    pl_list_t node = self->history;
    // Find the last node
    while (node && node->next)
        node = node->next;
    // Write from last to first, skipping the empty last entry
    while (node) {
        if (node->data && ((char *)node->data)[0] != '\0') fprintf(fp, "%s\n", (char *)node->data);
        node = node->prev;
    }
    fclose(fp);
}

void pl_readline_load_history(_self, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return;
    // Read each line and prepend to history (so oldest ends up at tail)
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buffer = malloc(file_size + 1);
    int n = fread(buffer, 1, file_size, fp);
    if(n != file_size) {
        free(buffer);
        fclose(fp);
        return; // Read error
    }
    buffer[file_size] = '\0';
    // 按行分割
    char *line = strtok(buffer, "\n");
    while (line) {
        self->history_idx = 0;
        pl_list_t node       = pl_list_nth(self->history, self->history_idx);
        free(node->data);
        node->data = strdup(line);
        line       = strtok(NULL, "\n");
        pl_readline_add_history(self, "");
    }
    free(buffer);
    fclose(fp);
}
#endif
