/**
 * syntax.c - Syntax highlighting implementation
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "syntax.h"
#include "platform.h"
#include "pleditor.h"

/* C-like language keywords */
char *C_HL_keywords[] = {
    /* C keywords */
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "case",
    "#include", "#define", "#ifdef", "#ifndef", "#endif", "#pragma",
    "volatile", "register", "sizeof", "const", "auto", "do", "goto",
    "default", "extern", "inline", "restrict",

    /* C++ keywords */
    "namespace", "public", "private", "protected", "virtual", "friend",
    "new", "delete", "try", "catch", "throw", "this", "constexpr",
    "final", "override", "explicit", "using",

    /* Types - keyword2 */
    "int|" , "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "bool|", "short|", "size_t|", "uint8_t|", "uint16_t|", "uint32_t|",
    "uint64_t|", "int8_t|", "int16_t|", "int32_t|", "int64_t|", "FILE|", "time_t|",
    "class|", "template|",

    /* Built-in values */
    "true|", "false|", "NULL|", "nullptr|",

    NULL
};

/* Lua language keywords */
char *LUA_HL_keywords[] = {
    /* Lua keywords */
    "function", "local", "if", "then", "else", "elseif", "end", "while",
    "do", "for", "repeat", "until", "break", "return", "in", "and", "or", "not",

    /* Lua built-in values */
    "true|", "false|", "nil|",

    /* Lua built-in functions */
    "print|", "pairs|", "ipairs|", "type|", "tonumber|", "tostring|", "require|",
    "table|", "string|", "math|", "os|", "io|", "coroutine|", "error|", "assert|",
    "pcall|", "xpcall|", "select|", "rawget|", "rawset|", "rawequal|", "rawlen|",
    "collectgarbage|", "dofile|", "load|", "loadfile|", "next|",

    NULL
};

/* Python language keywords */
char *PYTHON_HL_keywords[] = {
    /* Python keywords */
    "def", "class", "if", "elif", "else", "while", "for", "in", "try",
    "except", "finally", "with", "as", "import", "from", "pass", "return",
    "break", "continue", "lambda", "yield", "global", "nonlocal", "assert",
    "raise", "del", "not", "and", "or", "is", "async", "await", "match", "case",

    /* Python built-in values */
    "True|", "False|", "None|",

    /* Python special identifiers */
    "self|", "super|", "cls|",

    /* Python built-in types */
    "int|", "str|", "float|", "list|", "dict|", "tuple|", "set|", "bool|",
    "bytes|", "bytearray|", "complex|", "frozenset|", "object|", "type|",

    /* Python built-in functions */
    "print|", "len|", "range|", "enumerate|", "sorted|", "sum|", "min|", "max|",
    "abs|", "open|", "id|", "input|", "format|", "zip|", "map|", "filter|",
    "any|", "all|", "dir|", "vars|", "locals|", "globals|", "hasattr|",
    "getattr|", "setattr|", "delattr|", "isinstance|", "issubclass|",
    "callable|", "property|", "staticmethod|", "classmethod|", "iter|",
    "next|", "reversed|", "exec|", "eval|", "repr|", "round|", "pow|",

    NULL
};

/* Riddle language keywords */
char *RIDDLE_HL_keywords[] = {
    /* Riddle keywords */
    "var", "val", "for", "while", "continue", "break", "if", "else", "fun",
    "return", "import", "package", "class", "try", "catch", "override",
    "static", "const", "public", "protected", "private", "virtual", "operator",

    /* Types - keyword2 */
    "int|", "long|", "double|", "float|", "char|", "void|", "bool|", "short|",

    /* Riddle built-in values */
    "true|", "false|", "null|",

    NULL
};

/* Stamon language keywords */
char *STAMON_HL_keywords[] = {
    /* Stamon keywords */"class", "def", "extends", "func",
	"break", "continue",
	"if", "else", "while", "for", "in",
	"return", "sfn", "new", "null", "import",
	"true", "false",
    NULL
};

/* Syntax definitions */
pleditor_syntax HLDB[] = {
    /* C-like language */
    {
        "c",
        (char*[]){"c", "h", "cpp", "hpp", "cc", "cxx", "c++", NULL},
        C_HL_keywords,
        "//",     /* Single line comment start */
        "/*",     /* Multi-line comment start */
        "*/",     /* Multi-line comment end */
        0         /* Flags */
    },
    /* Lua language */
    {
        "lua",
        (char*[]){"lua", NULL},
        LUA_HL_keywords,
        "--",     /* Single line comment start */
        "--[[",   /* Multi-line comment start */
        "]]",     /* Multi-line comment end */
        0         /* Flags */
    },
    /* Python language */
    {
        "python",
        (char*[]){"py", "pyw", NULL},
        PYTHON_HL_keywords,
        "#",      /* Single line comment start */
        "\"\"\"", /* Multi-line comment/docstring start */
        "\"\"\"", /* Multi-line comment/docstring end */
        0         /* Flags */
    },
    /* Riddle language */
    {
        "riddle",
        (char*[]){"rid", NULL},
        RIDDLE_HL_keywords,
        "//",     /* Single line comment start */
        "/*",     /* Multi-line comment start */
        "*/",     /* Multi-line comment end */
        0
    },
	/* Stamon language */
    {
        "stamon",
        (char*[]){"st", "stm", NULL},
        STAMON_HL_keywords,
        "//",     /* Single line comment start */
        "/*",     /* Multi-line comment start */
        "*/",     /* Multi-line comment end */
        0
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* Is the character a separator */
static bool is_separator(int c) {
    return c == '\0' || isspace(c) ||
    strchr(",.()+-/*=~%<>[];\\{}:\"'", c) != NULL;
}

/* Is the character a punctuation mark to highlight */
static bool is_punctuation(int c) {
    return strchr(",.():;{}[]<>=%+-*/&|^~!", c) != NULL;
}

/* Is the character valid in an identifier */
static bool is_identifier_char(char c) {
    return isalnum(c) || c == '_';
}

/* Highlight function or class name in definitions or calls */
static void highlight_function_class(pleditor_state *state, pleditor_row *row, int *i) {
    char *line = row->render;
    int line_len = row->render_size;

    /* Skip if position is out of bounds */
    if (*i >= line_len) return;

    /* Check for function/class definition keywords */
    bool is_def = false;
    int kw_len = 0;
    int j, idx;

    /* Look ahead for patterns based on language */
    if (state->syntax) {
        if (strcmp(state->syntax->filetype, "python") == 0) {
            /* Python: Check for 'def ' or 'class ' */
            if (*i > 0 && is_separator(line[*i - 1])) {
                if (*i + 3 < line_len && strncmp(&line[*i], "def ", 4) == 0) {
                    is_def = true;
                    kw_len = 4;
                } else if (*i + 5 < line_len && strncmp(&line[*i], "class ", 6) == 0) {
                    is_def = true;
                    kw_len = 6;
                }
            }
        } else if (strcmp(state->syntax->filetype, "lua") == 0) {
            /* Lua: Check for 'function ' */
            if (*i > 0 && is_separator(line[*i - 1])) {
                if (*i + 8 < line_len && strncmp(&line[*i], "function ", 9) == 0) {
                    is_def = true;
                    kw_len = 9;
                }
            }
        } else if (strcmp(state->syntax->filetype, "c") == 0) {
            /* Class declaration: "class Name" */
            if (*i > 0 && is_separator(line[*i - 1])) {
                if (*i + 5 < line_len && strncmp(&line[*i], "class ", 6) == 0) {
                    is_def = true;
                    kw_len = 6;
                } else if (*i + 3 < line_len && strncmp(&line[*i], "struct ", 7) == 0) {
                    is_def = true;
                    kw_len = 7;
                }
            }

            /* C function definition check */
            if (!is_def && *i > 0) {
                /* Check for function pattern: Look for spaces, then identifier, then ( */
                for (j = *i; j < line_len && is_identifier_char(line[j]); j++);

                /* If it's followed by a ( after possible whitespace, it might be a function */
                idx = j;
                while (idx < line_len && isspace(line[idx])) idx++;

                if (idx < line_len && line[idx] == '(') {
                    /* Likely a function, check if declaration or call */
                    bool has_body = false;

                    /* Skip to the end of the parameter list */
                    int paren_level = 1;
                    idx++;
                    while (idx < line_len && paren_level > 0) {
                        if (line[idx] == '(') paren_level++;
                        else if (line[idx] == ')') paren_level--;
                        idx++;
                    }

                    /* Check for { after the closing ) - indicates a function definition */
                    while (idx < line_len && isspace(line[idx])) idx++;
                    if (idx < line_len && line[idx] == '{') has_body = true;

                    /* If it's a definition or we're not sure, highlight it */
                    if (has_body || (j > *i)) {
                        for (idx = *i; idx < j; idx++) {
                            row->hl->hl[idx] = HL_FUNC_CLASS_NAME;
                        }
                        *i = j - 1; /* position just before the end */
                        return;
                    }
                }
            }
        }
    }

    if (is_def) {
        /* Skip the keyword and spaces */
        *i += kw_len;
        while (*i < line_len && isspace(line[*i])) (*i)++;

        /* Highlight the function/class name */
        int name_start = *i;
        while (*i < line_len && is_identifier_char(line[*i])) (*i)++;

        if (*i > name_start) {
            for (j = name_start; j < *i; j++) {
                row->hl->hl[j] = HL_FUNC_CLASS_NAME;
            }
            /* Don't increment i again since the loop will do it */
            (*i)--;
        }
    } else {
        /* Check for function calls: name(... */
        int name_start = *i;
        while (*i < line_len && is_identifier_char(line[*i])) (*i)++;

        /* If we have an identifier followed by a parenthesis, it's likely a function call */
        if (*i > name_start) {
            idx = *i;
            while (idx < line_len && isspace(line[idx])) idx++;

            if (idx < line_len && line[idx] == '(') {
                for (j = name_start; j < *i; j++) {
                    row->hl->hl[j] = HL_FUNC_CLASS_NAME;
                }
            }
        }
        /* Don't increment i again since the loop will do it */
        (*i)--;
    }
}

/* Handle punctuation highlighting */
static void highlight_punctuation(pleditor_row *row, int i) {
    /* Check for single character punctuation */
    if (i < row->render_size && is_punctuation(row->render[i])) {
        row->hl->hl[i] = HL_PUNCTUATION;

        /* Check for compound operators */
        if (i + 1 < row->render_size) {
            char c1 = row->render[i];
            char c2 = row->render[i + 1];

            /* Check for common compound operators where second char is '=' */
            if (c2 == '=' && strchr("+-*/=!&|^<>%", c1) != NULL) {
                row->hl->hl[i + 1] = HL_PUNCTUATION;
            }

            /* Check for increment/decrement operators */
            else if ((c1 == '+' && c2 == '+') || (c1 == '-' && c2 == '-')) {
                row->hl->hl[i + 1] = HL_PUNCTUATION;
            }

            /* Check for shift operators */
            else if ((c1 == '<' && c2 == '<') || (c1 == '>' && c2 == '>')) {
                row->hl->hl[i + 1] = HL_PUNCTUATION;
            }

            /* Check for logical operators */
            else if ((c1 == '&' && c2 == '&') || (c1 == '|' && c2 == '|')) {
                row->hl->hl[i + 1] = HL_PUNCTUATION;
            }

            /* Check for structure dereference operator -> */
            else if (c1 == '-' && c2 == '>') {
                row->hl->hl[i + 1] = HL_PUNCTUATION;
            }
        }
    }
}

/* Handle hex, octal, or binary number formats */
static bool highlight_based_number(pleditor_row *row, int *i) {
    if (*i + 2 >= row->render_size || row->render[*i] != '0')
        return false;

    char next = row->render[*i + 1];
    if (!strchr("xXoObB", next))
        return false;

    /* Highlight the prefix (0x, 0o, 0b) */
    row->hl->hl[*i] = row->hl->hl[*i + 1] = HL_NUMBER;
    *i += 2;

    /* Continue highlighting based on the number format */
    while (*i < row->render_size) {
        char c = row->render[*i];
        bool valid;

        if (next == 'x' || next == 'X')
            valid = isxdigit(c);
        else if (next == 'o' || next == 'O')
            valid = c >= '0' && c <= '7';
        else
            valid = c == '0' || c == '1';

        if (!valid) break;

        row->hl->hl[*i] = HL_NUMBER;
        (*i)++;
    }

    return true;
}

/* Initialize syntax highlighting system */
bool pleditor_syntax_init(pleditor_state *state) {
    state->syntax = NULL;

    /* Select syntax by filename if there is one */
    if (state->filename) {
        pleditor_syntax_by_fileext(state, state->filename);

        /* Apply syntax highlighting to all rows */
        pleditor_syntax_update_all(state);
    }

    return true;
}

/* Apply syntax highlighting to all rows in the file */
void pleditor_syntax_update_all(pleditor_state *state) {
    if (!state->syntax) return;

    for (int i = 0; i < state->num_rows; i++) {
        pleditor_syntax_update_row(state, i);
    }
}

/* Map highlight values to ansi escape code */
int pleditor_syntax_color_to_ansi(int hl) {
    switch(hl) {
        case HL_COMMENT:
        case HL_MULTILINE_COMMENT:
            return 90;  /* Gray */
        case HL_KEYWORD1:
            return 34;  /* Blue */
        case HL_KEYWORD2:
            return 32;  /* Green */
        case HL_STRING:
            return 35;  /* Magenta */
        case HL_NUMBER:
            return 31;  /* Red */
        case HL_PUNCTUATION:
            return 33;  /* Yellow */
        case HL_FUNC_CLASS_NAME:
            return 36;  /* Cyan */
        default:
            return 37;  /* White (default) */
    }
}

/* Select syntax highlighting based on file extension */
void pleditor_syntax_by_fileext(pleditor_state *state, const char *filename) {
    state->syntax = NULL;

    /* No filename, so no highlighting */
    if (!filename) return;

    /* Get file extension */
    char *ext = strrchr(filename, '.');
    if (!ext) return;
    ext++; /* Skip the dot */

    /* Try to match file extension with a syntax */
    for (unsigned int i = 0; i < HLDB_ENTRIES; i++) {
        pleditor_syntax *syntax = &HLDB[i];
        char **pattern = syntax->filematch;

        while (*pattern) {
            /* Check if extension matches */
            if (strcmp(*pattern, ext) == 0) {
                state->syntax = syntax;
                return;
            }
            pattern++;
        }
    }
}

/* Update highlighting for a row */
void pleditor_syntax_update_row(pleditor_state *state, int row_idx) {
    pleditor_row *row = &state->rows[row_idx];

    /* Free existing highlighting memory */
    if (row->hl) {
        free(row->hl->hl);
        free(row->hl);
    }

    /* Allocate new highlight struct */
    row->hl = pleditor_platform_reallocate(NULL, sizeof(pleditor_highlight_row));
    row->hl->hl = pleditor_platform_reallocate(NULL, row->render_size);
    memset(row->hl->hl, HL_NORMAL, row->render_size);
    row->hl->hl_multiline_comment = false;

    /* If no syntax, leave everything as normal */
    if (!state->syntax) return;

    char **keywords = state->syntax->keywords;
    char *scs = state->syntax->singleline_comment_start;
    char *mcs = state->syntax->multiline_comment_start;
    char *mce = state->syntax->multiline_comment_end;

    bool prev_sep = true;
    int in_string = 0;
    bool in_comment = (row_idx > 0 && state->rows[row_idx-1].hl) ?
                    state->rows[row_idx-1].hl->hl_multiline_comment : false;

    /* Check for preprocessor directives in C/C++ at the beginning of the line */
    if (state->syntax && (strcmp(state->syntax->filetype, "c") == 0)) {
        if (row->render_size > 0 && row->render[0] == '#') {
            /* Highlight the # character */
            row->hl->hl[0] = HL_KEYWORD1;

            /* Find the directive word (e.g., define, ifndef) */
            int j = 1;
            while (j < row->render_size && isspace(row->render[j])) j++;

            int directive_start = j;
            while (j < row->render_size && isalpha(row->render[j])) j++;

            /* Highlight the directive */
            for (int k = directive_start; k < j; k++) {
                row->hl->hl[k] = HL_KEYWORD1;
            }

            /* Highlight what follows the directive for specific cases */
            if (directive_start < j) {
                int len = j - directive_start;
                if ((len == 6 && strncmp(&row->render[directive_start], "define", len) == 0) ||
                    (len == 6 && strncmp(&row->render[directive_start], "ifndef", len) == 0) ||
                    (len == 5 && strncmp(&row->render[directive_start], "ifdef", len) == 0) ||
                    (len == 7 && strncmp(&row->render[directive_start], "include", len) == 0) ||
                    (len == 5 && strncmp(&row->render[directive_start], "endif", len) == 0) ||
                    (len == 5 && strncmp(&row->render[directive_start], "undef", len) == 0) ||
                    (len == 6 && strncmp(&row->render[directive_start], "pragma", len) == 0)) {

                    /* Skip whitespace after directive */
                    while (j < row->render_size && isspace(row->render[j])) j++;

                    /* Highlight the identifier */
                    int ident_start = j;

                    /* For #include, handle both <...> and "..." forms */
                    if (len == 7 && j < row->render_size &&
                        (row->render[j] == '<' || row->render[j] == '"')) {
                        char end_char = (row->render[j] == '<') ? '>' : '"';
                        row->hl->hl[j++] = HL_KEYWORD2; /* Highlight the opening < or " */

                        /* Find the closing character */
                        while (j < row->render_size && row->render[j] != end_char) {
                            row->hl->hl[j++] = HL_KEYWORD2;
                        }
                        if (j < row->render_size) {
                            row->hl->hl[j++] = HL_KEYWORD2; /* Highlight the closing > or " */
                        }
                    } else {
                        /* For other directives, highlight the identifier */
                        while (j < row->render_size &&
                              (is_identifier_char(row->render[j]) || row->render[j] == '.')) {
                            j++;
                        }

                        for (int k = ident_start; k < j; k++) {
                            row->hl->hl[k] = HL_KEYWORD2;
                        }
                    }
                }
            }
        }
    }

    int i = 0;
    while (i < row->render_size) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl->hl[i-1] : HL_NORMAL;

        /* String handling */
        if (in_string) {
            row->hl->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->render_size) {
                row->hl->hl[i+1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == in_string) in_string = 0;
            i++;
            prev_sep = true;
            continue;
        }

        /* Comment handling */
        if (in_comment) {
            row->hl->hl[i] = HL_MULTILINE_COMMENT;
            if (mce && strncmp(&row->render[i], mce, strlen(mce)) == 0) {
                for (unsigned int j = 0; j < strlen(mce); j++)
                    row->hl->hl[i+j] = HL_MULTILINE_COMMENT;
                i += strlen(mce);
                in_comment = false;
                prev_sep = true;
                continue;
            } else {
                i++;
                continue;
            }
        }

        /* Start of multi-line comment */
        if (mcs && strncmp(&row->render[i], mcs, strlen(mcs)) == 0) {
            for (unsigned int j = 0; j < strlen(mcs); j++)
                row->hl->hl[i+j] = HL_MULTILINE_COMMENT;
            i += strlen(mcs);
            in_comment = true;
            continue;
        }

        /* Start of single-line comment */
        if (scs && strncmp(&row->render[i], scs, strlen(scs)) == 0) {
            for (int j = i; j < row->render_size; j++)
                row->hl->hl[j] = HL_COMMENT;
            break;
        }

        /* String start or include brackets <> */
        if (c == '"' || c == '\'' ||
            (c == '<' && prev_sep && strstr(row->render, "#include") != NULL)) {
            /* Set appropriate closing character */
            char closing = (c == '<') ? '>' : c;
            in_string = closing;
            row->hl->hl[i] = HL_STRING;
            i++;
            continue;
        }

        /* Number handling */
        if (isdigit(c)) {
            /* Check for special number formats (hex, octal, binary) */
            if (highlight_based_number(row, &i)) {
                prev_sep = false;
                continue;
            }

            /* Regular decimal number */
            if (prev_sep || prev_hl == HL_NUMBER) {
                row->hl->hl[i] = HL_NUMBER;
                i++;
                prev_sep = false;
                continue;
            }
        } else if (c == '.' && prev_hl == HL_NUMBER) {
            /* Decimal point in a number */
            row->hl->hl[i] = HL_NUMBER;
            i++;
            prev_sep = false;
            continue;
        }

        /* Handle special case for colon in array slices and Python statements */
        if (c == ':') {
            row->hl->hl[i] = HL_PUNCTUATION;
            i++;
            prev_sep = true; /* Treat colon as separator */
            continue;
        }

        /* Keyword handling */
        if (prev_sep) {
            bool found_keyword = false;
            for (int j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                bool is_kw2 = keywords[j][klen-1] == '|';

                if (is_kw2) klen--;

                /* Special handling for Python identifiers that need context checks */
                bool is_valid_match = strncmp(&row->render[i], keywords[j], klen) == 0 &&
                                      is_separator(row->render[i + klen]);

                if (is_kw2) {
                    for (int back = i - 1; back >= 0 && back >= i - 20; back--) {
                        if (row->render[back] == '(' || row->render[back] == ',') {
                            /* Found valid context */
                            break;
                        }
                        if (!isspace(row->render[back])) {
                            /* Found non-whitespace that's not a separator we expect */
                            if (back == i - 1) {
                                is_valid_match = false; /* directly adjacent */
                            }
                            break;
                        }
                    }
                }

                if (is_valid_match) {
                    /* Apply keyword highlighting */
                    for (int k = 0; k < klen; k++)
                        row->hl->hl[i+k] = is_kw2 ? HL_KEYWORD2 : HL_KEYWORD1;
                    i += klen;
                    found_keyword = true;
                    break;
                }
            }

            if (found_keyword) {
                prev_sep = false;
                continue;
            }
        }

        /* Highlight punctuation */
        highlight_punctuation(row, i);

        /* Check for function or class names */
        if ((isalpha(c) || c == '_') && prev_sep) {
            highlight_function_class(state, row, &i);
        }

        /* Empty - Moved preprocessor directive handling to earlier in the code */

        /* Special handling for Python indentation */
        if (state->syntax && strcmp(state->syntax->filetype, "python") == 0) {
            /* Mark beginning of line whitespace as special in Python */
            if (i == 0 && isspace(c)) {
                int indent_end = 0;
                while (indent_end < row->render_size && isspace(row->render[indent_end])) {
                    indent_end++;
                }
                if (indent_end > 0) {
                    for (int k = 0; k < indent_end; k++) {
                        row->hl->hl[k] = HL_NORMAL;
                    }
                }
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    /* Update multiline comment status for this row */
    row->hl->hl_multiline_comment = in_comment;
}

/* Update syntax highlighting for rows affected by multi-line comments */
void pleditor_syntax_update_multiline(pleditor_state *state, int start_row) {
    if (!state->syntax) return;

    /* Update the starting row and all subsequent rows that might be affected */
    for (int i = start_row; i < state->num_rows; i++) {
        pleditor_syntax_update_row(state, i);

        /* Stop updating when we reach a row not affected by multi-line comments */
        if (i > start_row && !state->rows[i].hl->hl_multiline_comment) {
            break;
        }
    }
}
