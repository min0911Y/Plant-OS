/* C requires assert to follow NDEBUG on every inclusion. */
#undef assert
#if defined(NDEBUG)
#define assert(expression) ((void)0)
#else
#ifdef __cplusplus
extern "C" {
#endif
void __assert_fail(const char *expression, const char *file, unsigned line,
                   const char *function) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#define assert(expression)                                                     \
  ((expression) ? (void)0                                                      \
                : __assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

#if !defined(__cplusplus) && __STDC_VERSION__ >= 201112L &&                    \
    __STDC_VERSION__ < 202311L
#define static_assert _Static_assert
#endif
