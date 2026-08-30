//
// This file is part of pl_readline.
// pl_readline is free software: you can redistribute it and/or modify
// it under the terms of MIT license.
// See file LICENSE or https://opensource.org/licenses/MIT for full license
// details.
//
// Copyright (c) 2024 min0911_ https://github.com/min0911Y
//

// pl_readline_color: 着色

#include "pl_readline.h"
#include <stdio.h>
#include <string.h>


// Helper function to find the color of a command
int get_command_color(_self, const char *word, int is_first_word) {

    if (self->pl_readline_get_words == NULL) return PL_COLOR_RESET;

    // printf("get_command_color: word='%s', is_first_word=%d\n", word, is_first_word);
    // Initialize a temporary word list to hold commands
    pl_readline_words_t word_list = pl_readline_word_maker_init();
    if (word_list == NULL) return PL_COLOR_RESET;

    // Get all defined commands
    self->pl_readline_get_words((char *)word, word_list);

    // Check if the word exactly matches any of our defined commands
    for (isize i = 0; i < word_list->len; i++) {
        // Skip commands that require first position but aren't first
        if (word_list->words[i].first && !is_first_word) { continue; }

        // Check exact match
        if (strcmp(word_list->words[i].word, word) == 0) {
            int color = word_list->words[i].color;
            pl_readline_word_maker_destroy(word_list);
            return color;
        }
    }

    // Clean up
    pl_readline_word_maker_destroy(word_list);
    return PL_COLOR_RESET; // Default color
}

// Function to redisplay the buffer with colorized commands
void pl_readline_redisplay(_self, int show_prompt) {
#define EXPAND_BUFFER                                                                                                    \
    do {                                                                                                                 \
        if (word_count == self->color_max_words) {                                                                             \
            self->color_max_words *= 2;                                                                                  \
            self->color_words = realloc(self->color_words, self->color_max_words * sizeof(char *));                      \
        }                                                                                                                \
    } while(0)
    // Save original cursor position
    size_t original_ptr = self->ptr;
    size_t prompt_len   = 0;

    // Calculate prompt length (only if shown)
    if (self->prompt) {
        // Count visible characters in the prompt (ignoring ANSI escape sequences)
        const char *p = self->prompt;
        while (*p) {
            if (*p == '\033') {
                // Skip ANSI escape sequence
                while (*p && *p != 'm')
                    p++;
                if (*p) p++; // Skip the 'm'
                continue;
            }
            prompt_len++;
            p++;
        }
    }

    // Move cursor back to start of line using absolute positioning
    pl_readline_print(self, "\r");

    // Display prompt only if requested
    if (show_prompt) {
        pl_readline_print(self, self->prompt);
    } else if (prompt_len > 0) {
        // If not showing prompt but there is a prompt, move cursor forward to skip prompt area
        char forward_buf[32];
        sprintf(forward_buf, "\033[%zuC", prompt_len);
        pl_readline_print(self, forward_buf);
    }

    // Track position considering the prompt if shown
    size_t display_position = prompt_len; // Start after prompt

    // Clear the line after cursor
    pl_readline_print(self, "\033[K");

    // Parse and colorize each word in the buffer
    char  *buffer_copy = strdup(self->buffer);
    if (buffer_copy == NULL) return;
    size_t buffer_len  = strlen(buffer_copy);

    // Handle empty buffer
    if (buffer_len == 0) {
        free(buffer_copy);
        return;
    }

    // Get all tokens and preserve spaces

    if(!self->color_words) {
        self->color_words = malloc(self->color_max_words * sizeof(char *));
        if (self->color_words == NULL) {
            free(buffer_copy);
            return;
        }
    }
    int   word_count = 0;
    char *p          = buffer_copy;
    char *word_start = p;

    // Parse words and spaces
    while (*p) {
        if (*p == ' ') {
            // Found a space - terminate the current word
            *p = '\0';
            EXPAND_BUFFER;
            self->color_words[word_count++] = word_start;
            if (word_start != buffer_copy ||
                *buffer_copy != '\0') { // if the word is not empty
                EXPAND_BUFFER;
                self->color_words[word_count++] = p; // build a space word
            }
            // let the point p start from the next character which is not a
            // space
            p++; // we've set *p to '\0' so we need to move to the next character
            while (*p == ' ' && *p != '\0') {
              p++;
            }
            if(*p == '\0') {
                // If we reached the end, break
                word_start = p;
                break;
            }
            word_start = p;

        }
        p++;
    }

    // Don't forget the last word if it's not empty
    if (*word_start && word_start[0] != '\0') { EXPAND_BUFFER; self->color_words[word_count++] = word_start; }
    for (int i = 0; i < word_count; i++) {
      if (self->color_words[i][0] == '\0') {
            // This is a space (or spaces)
            int p;
            // calculate the length of the space
            if (i < word_count - 1) { // there is a next word
                p = self->color_words[i + 1] - self->color_words[i];
            } else { // or it's the last word in the buffer
                // calculate the length with the end position of the buffer
                p = (buffer_copy + buffer_len) - self->color_words[i];
            }
            for(int j = 0; j < p; j++) {
                pl_readline_print(self, " ");
                display_position++;
            }
        } else {
            // This is a word
            // Check if this is the first word in the line
#if PL_ENABLE_COLOR_FIRST_WORD_ONLY
            int is_first = (i == 0);
#else
            int is_first =
            (i == 0 || (i > 0 && self->color_words[i - 1][0] == '\0' && i == 1));
#endif
            int color = get_command_color(self, self->color_words[i], is_first);
            if (color != PL_COLOR_RESET) {
                // Apply color
                char color_str[16];
                sprintf(color_str, "\033[%dm", color);
                pl_readline_print(self, color_str);
            }

            // Print the word
            pl_readline_print(self, self->color_words[i]);
            display_position += strlen(self->color_words[i]);

            if (color != PL_COLOR_RESET) {
                // Reset color
                pl_readline_print(self, "\033[0m");
            }
        }
    }

    free(buffer_copy);

    // If original buffer ends with space, print it
    if (self->length > 0 && self->buffer[self->length - 1] == ' ') {
        pl_readline_print(self, " ");
        display_position++;
    }

    // Calculate cursor position (prompt + original_ptr)
    size_t target_position = prompt_len + original_ptr;

    // Position cursor correctly
    if (display_position != target_position) {
        // Use absolute positioning from the left edge
        pl_readline_print(self, "\r");

        if (target_position > 0) {
            char move_buf[32];
            sprintf(move_buf, "\033[%zuC", target_position);
            pl_readline_print(self, move_buf);
        }
    }
}
