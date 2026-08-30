//
// This file is part of pl_readline.
// pl_readline is free software: you can redistribute it and/or modify
// it under the terms of MIT license.
// See file LICENSE or https://opensource.org/licenses/MIT for full license
// details.
//
// Copyright (c) 2024 min0911_ https://github.com/min0911Y
//

// plreadln_wordmk.c: pl_readline word maker

#include "pl_readline.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

pl_readline_words_t pl_readline_word_maker_init(void) {
    pl_readline_words_t words = malloc(sizeof(struct pl_readline_words));
    if (words == NULL) return NULL;
    words->len                = 0;  // initial length
    words->max_len            = 16; // initial max length
    words->words              = malloc(words->max_len * sizeof(pl_readline_word));
    if (words->words == NULL) {
        free(words);
        return NULL;
    }
    return words;
}

void pl_readline_word_maker_destroy(pl_readline_words_t words) {
    if (words == NULL) return;
    for (isize i = 0; i < words->len; i++) {
        char *p = words->words[i].word;
        free(p);
    }
    free(words->words);
    free(words);
}

int pl_readline_word_maker_add(const char *word, pl_readline_words_t words, bool is_first, int color,
                               char sep) {
    if (word == NULL || words == NULL) return PL_READLINE_FAILED;
    if (words->len >= words->max_len) {
        if (words->max_len > INT_MAX / 2) return PL_READLINE_FAILED;
        pl_readline_word *expanded =
            realloc(words->words, words->max_len * 2 * sizeof(pl_readline_word));
        if (expanded == NULL) return PL_READLINE_FAILED;
        words->words = expanded;
        words->max_len *= 2;
    }
    char *copy = strdup(word);
    if (copy == NULL) return PL_READLINE_FAILED;
    words->words[words->len].first = is_first;
    words->words[words->len].word  = copy;
    words->words[words->len].sep   = sep;
    words->words[words->len].color = color;
    words->len++;
    return 0;
}

void pl_readline_word_maker_clear(pl_readline_words_t words) {
    if (words == NULL) return;
    for (isize i = 0; i < words->len; i++) {
        free(words->words[i].word);
    }
    words->len = 0;
}
