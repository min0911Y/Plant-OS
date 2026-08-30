#ifndef PL_READLINE_H
#define PL_READLINE_H

#include "pl_list.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


// Color definitions for terminal output
#define PL_COLOR_RESET   0
#define PL_COLOR_BLACK   30
#define PL_COLOR_RED     31
#define PL_COLOR_GREEN   32
#define PL_COLOR_YELLOW  33
#define PL_COLOR_BLUE    34
#define PL_COLOR_MAGENTA 35
#define PL_COLOR_CYAN    36
#define PL_COLOR_WHITE   37

#define PL_READLINE_KEY_UP        0xff00
#define PL_READLINE_KEY_DOWN      0xff01
#define PL_READLINE_KEY_LEFT      0xff02
#define PL_READLINE_KEY_RIGHT     0xff03
#define PL_READLINE_KEY_HOME      0xff04
#define PL_READLINE_KEY_END       0xff05
#define PL_READLINE_KEY_PAGE_UP   0xff06
#define PL_READLINE_KEY_PAGE_DOWN 0xff07
#define PL_READLINE_KEY_ENTER     '\n'
#define PL_READLINE_KEY_TAB       '\t'
#define PL_READLINE_KEY_CTRL_A    0x01
#define PL_READLINE_KEY_CTRL_C    0x03
#define PL_READLINE_KEY_BACKSPACE '\b'

#define _self                          pl_readline_t self
#define PL_READLINE_SUCCESS            0
#define PL_READLINE_FAILED             -1
#define PL_READLINE_NOT_FINISHED       1
#define PL_READLINE_DEFAULT_BUFFER_LEN 32

#ifndef PL_ENABLE_HISTORY_FILE
#    define PL_ENABLE_HISTORY_FILE 1
#endif
#ifndef PL_ENABLE_COLOR_FIRST_WORD_ONLY
#    define PL_ENABLE_COLOR_FIRST_WORD_ONLY 1
#endif
#ifndef PL_ENABLE_INTELLISENSE
#    define PL_ENABLE_INTELLISENSE 1
#endif
typedef struct pl_readline_word {
    char *word; // 词组
    /**
      如果first为true，
      这个word必须在第一个参数的位置的时候才能得到补全
      如abc 则必须输入"ab"然后按tab，才会有可能有"abc"
      如果是"qwe ab"则不会补全"qwe abc"，除非first为false.
    */
    bool  first;
    char  sep;   // 分隔符
    int   color; // ANSI颜色（如 PL_COLOR_GREEN）
} pl_readline_word;

typedef struct pl_readline_words {
    isize            len;     // 词组数量
    isize            max_len; // 词组最大数量
    pl_readline_word *words;   // 词组列表
} *pl_readline_words_t;

typedef struct pl_readline {
    int (*pl_readline_hal_getch)(void);                       // 输入函数
    int (*pl_readline_hal_putch)(int ch);                     // 输出函数
    void (*pl_readline_hal_flush)(void);                      // 刷新函数
    void (*pl_readline_get_words)(char               *buf,
                                  pl_readline_words_t words); // 获取词组列表
    char  *buffer;                                            // 输入缓冲区
    char  *input_buf;                                         // 输入缓冲区（补全的前缀）
    isize ptr;                                                // 输入缓冲区指针
    isize input_ptr;                                          // 输入缓冲区（补全的前缀）指针
    isize maxlen;                                             // 缓冲区最大长度
    isize length;                                             // 输入缓冲区长度（已经输入的字符数）
    pl_list_t history;                                        // 历史记录列表
    int    history_idx;                                       // 历史记录指针
    const char *prompt;                                       // 提示符
    bool   intellisense_mode;                                 // 智能补全模式
    char  *intellisense_word;                                 // 智能补全词组


    // for color
    char **color_words;                                       // 词组列表（用于判断并着色）
    int color_max_words;
} *pl_readline_t;

void pl_readline_insert_char(char *str, char ch, int idx);
void pl_readline_delete_char(char *str, int idx);
int pl_readline_add_history(_self, char *line);
int pl_readline_modify_history(_self);
pl_readline_words_t pl_readline_word_maker_init(void);
pl_readline_t       pl_readline_init(int (*pl_readline_hal_getch)(void),
                                     int (*pl_readline_hal_putch)(int ch),
                                     void (*pl_readline_hal_flush)(void),
                                     void (*pl_readline_get_words)(char *buf, pl_readline_words_t words));
pl_readline_t       pl_readline_init_plant_os(
                         void (*get_words)(char *buf,
                                           pl_readline_words_t words));
const char         *pl_readline(_self, const char *prompt);
pl_readline_word    pl_readline_intellisense(_self, pl_readline_words_t words);
int                 pl_readline_insert_char_and_view(_self, char ch);
void                pl_readline_insert_char(char *str, char ch, int idx);
int  pl_readline_word_maker_add(const char *word, pl_readline_words_t words, bool is_first, int color,
                                char sep);
void pl_readline_print(_self, const char *str);
void pl_readline_intellisense_insert(_self, pl_readline_word words);
void pl_readline_word_maker_destroy(pl_readline_words_t words);
void pl_readline_next_line(_self);
int  pl_readline_handle_key(_self, int ch);
void pl_readline_uninit(_self);
int get_command_color(_self, const char *word, int is_first_word);
void pl_readline_redisplay(_self, int show_prompt);
#if PL_ENABLE_HISTORY_FILE
    void pl_readline_save_history(_self, const char *filename);
void pl_readline_load_history(_self, const char *filename);
#endif

#endif /* PL_READLINE_H */
