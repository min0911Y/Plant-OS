// #include <define.h>
// #include <type.h>

// #pragma GCC diagnostic   ignored "-Wall"
// #pragma clang diagnostic ignored "-Weverything"

// //? 已完成
// //+ 将 len 字节的数据从 src 复制到 dst
// //@ memcpy 函数是在 C标准库 中定义的函数，它的作用是从源缓冲区复制 n
// //@ 个字节到目标缓冲区。它返回目标缓冲区的首地址。
// void *memcpy(void *_rest _d, const void *_rest _s, size_t _n) {
//   byte       *d = _d;
//   const byte *s = _s;
//   const byte *e = _s + _n;
//   __std_safe__({
//     if (!d || !s) return null;
//     if (d + _n < d || e < s) return null;
//   });
//   if (d == s) return d;
//   while (s != e)
//     *d++ = *s++;
//   return d;
// }
// //? 已完成
// //- Copy N bytes of SRC to DEST, guaranteeing
// //- correct behavior for overlapping strings.
// //+ 将 len 字节的数据从 src 复制到 dst
// //+ 保证在重叠字符串上的正确行为
// void *memmove(void *_d, const void *_s, size_t _n) {
//   byte       *d = _d;
//   const byte *s = _s;
//   const byte *e = _s + _n;
//   __std_safe__({
//     if (!d || !s) return null;
//     if (d + _n < d || e < s) return null;
//   });
//   if (d == s) return d;
//   if (d > s && d < e) { // 重叠字符串，反向遍历
//     d += _n;
//     while (s != e)
//       *d-- = *e--;
//   } else { // 非重叠，正常遍历
//     while (s != e)
//       *d++ = *s++;
//   }
//   return d;
// }
// //? 已完成
// //- Set N bytes of S to C.
// //@ memset 函数是在 C标准库 中定义的函数，它的作用是将指定的字符 c
// //@ (转换为unsigned char类型) 赋值给缓冲区的前 n 个字节。它返回缓冲区的首地址。
// void *memset(void *_s, int _c, size_t _n) {
//   byte      *s = _s;
//   byte      *e = _s + _n;
//   const byte c = _c;
//   __std_safe__({
//     if (!s) return null;
//     if (e < s) return null;
//   });
//   for (; s < e; s++)
//     *s = c;
//   return _s;
// }
// //? 已完成
// //- Compare N bytes of S1 and S2.
// //@ memcmp 函数是在 C标准库 中定义的函数，它的作用是比较两个缓冲区的前 n
// //@ 个字节。它返回一个整数值，表示两个缓冲区的关系。
// //@ 如果 s1 和 s2 所指向的缓冲区相同，则函数返回 0。
// //@ 如果 s1 所指向的缓冲区小于 s2 所指向的缓冲区，则函数返回小于 0 的值。
// //@ 如果 s1 所指向的缓冲区大于 s2 所指向的缓冲区，则函数返回大于 0 的值。
// int memcmp(const void *_s1, const void *_s2, size_t _n) {
//   const byte *s1 = _s1, *s2 = _s2;
//   const byte *e1 = _s1 + _n;
//   for (; s1 < e1; s1++, s2++) {
//     if (*s1 < *s2) return -1;
//     if (*s1 > *s2) return 1;
//   }
//   return 0;
// }
// //? 已完成
// //- Search N bytes of S for C.
// void *memchr(const void *_s, int _c, size_t _n) {
//   const byte *s = _s;
//   const byte *e = _s + _n;
//   const byte  c = _c;
//   for (; s < e; s++)
//     if (*s == c) return (void *)s;
//   return null;
// }
// //? 已完成
// /* Copy SRC to DEST.  */
// char *strcpy(char *_rest d, cstr _rest s) {
//   char *_d = d;
//   while (*d++ = *s++) {}
//   return _d;
// }
// //? 已完成
// /* Copy no more than N characters of SRC to DEST.  */
// char *strncpy(char *_rest d, cstr _rest s, size_t n) {
//   char *_d = d;
//   cstr  e  = s + n;
//   while (s < e && (*d++ = *s++)) {}
//   return _d;
// }
// //! 未完成
// /* Append SRC onto DEST.  */
// char *strcat(char *_rest _d, cstr _rest _s) {}
// //! 未完成
// /* Append no more than N characters from SRC onto DEST.  */
// char *strncat(char *_rest __dest, cstr _rest __src, size_t __n) {}

// /* Compare S1 and S2.  */
// //? 已完成
// int strcmp(cstr _s1, cstr _s2) {
// #ifdef __std_safe__
//   if (!_s1 && !_s2) return 0;
//   if (!_s1) return -1;
//   if (!_s2) return 1;
// #endif
//   const byte *s1 = (const void *)_s1;
//   const byte *s2 = (const void *)_s2;
//   byte        c1, c2;
//   do {
//     c1 = *s1++;
//     c2 = *s2++;
//     if (!c1) return c1 - c2;
//   } while (c1 == c2);
//   return c1 - c2;
// }

// //^ __USE_MISC __USE_XOPEN __GLIBC_USE(ISOC2X)
// //- Copy no more than N bytes of SRC to DEST, stopping when C is found.
// //- Return the position in DEST one byte past where C was copied,
// //- or null if C was not found in the first N bytes of SRC.
// //+ 将不超过 N 个字节的 SRC 复制到 DEST，找到 C 时停止。 返回 DEST 中 C
// //+ 被复制后一个字节的位置，如果在 SRC 的前 N 个字节中找不到 C，则返回 NULL。
// //@ memccpy 函数是在 C标准库 中定义的函数，它的作用是从 src 缓冲区复制 n
// //@ 个字节到 des t 缓冲区，并在遇到 c 字符时停止复制。memccpy 函数返回一个
// //@ 指针，指向 dest 缓冲区中第一次出现 c 字符的位置。如果在复制过程中没有遇
// //@ 到 c 字符，则返回 NULL。
// void *memccpy(void *_rest _d, const void *_rest _s, int _c, size_t _n) {
//   byte       *d = _d;
//   const byte *s = _s;
//   const byte *e = _s + _n;
//   const byte  c = _c;
//   __std_safe__({
//     if (!d || !s) return null;
//     if (d + _n < d || e < s) return null;
//   });
//   // if (d == s) return d;
//   // while (s != e) *d++ = *s++;
//   return null;
// }
// //^ __USE_GNU
// //- Search in S for C.  This is similar to `memchr' but there is no length
// //- limit.
// void *rawmemchr(const void *_s, int _c) {
//   const byte *s = _s;
//   const byte  c = _c;
// #ifdef __std_safe__
//   if (!s) return null;
// #endif
//   for (; *s; s++)
//     if (*s == c) return (void *)s;
//   return null;
// }
// //^ __USE_GNU
// //- Search N bytes of S for the final occurrence of C.
// void *memrchr(const void *_s, int _c, size_t _n) {
//   const byte *s = _s;
//   const byte *e = _s + _n;
//   const byte  c = _c;
// #ifdef __std_safe__
//   if (!s) return null;
// #endif
//   for (; s < e; s++)
//     if (*s == c) return (void *)s;
// }

// char tolower(char c) {
//   return ('A' <= c && c <= 'Z') ? c - 'A' + 'a' : c;
// }
// char toupper(char c) {
//   return ('a' <= c && c <= 'a') ? c - 'a' + 'A' : c;
// }

// // case-insensitive
// // 不区分大小写的字符串比较函数
// int strcmp_ci(cstr _s1, cstr _s2) {
//   __std_safe__({
//     if (!_s1 && !_s2) return 0;
//     if (!_s1) return -1;
//     if (!_s2) return 1;
//   });
//   const byte *s1 = (const void *)_s1;
//   const byte *s2 = (const void *)_s2;
//   byte        c1, c2;
//   do {
//     c1 = *s1++;
//     c2 = *s2++;
//     if (!c1) return -c2;
//   } while (tolower(c1) == tolower(c2));
//   return c1 - c2;
// }

// //- Compare N characters of S1 and S2.
// int strncmp(cstr _s1, cstr _s2, size_t n) {
//   const byte *s1 = _s1;
//   const byte *e1 = _s1 + n;
//   const byte *s2 = _s2;
//   byte        c1, c2;
//   while (s1 != e1) {
//     c1 = *s1++;
//     c2 = *s2++;
//     if (!c1) return c1 - c2;
//     if (c1 != c2) return c1 - c2;
//   }
//   return 0;
// }

// #if 0

// /* Duplicate S, returning an identical malloc'd string.  */
// extern char *strdup(cstr __s) __attr(nothrow, leaf) __attribute_malloc__ __nonnull((1));

// /* Return a malloc'd copy of at most N bytes of STRING.  The
//    resultant string is terminated even if no null terminator
//    appears before STRING[N].  */
// extern char *strndup(cstr __string, size_t __n) __attr(nothrow, leaf)
//     __attribute_malloc__ __nonnull((1));

// /* Find the first occurrence of C in S.  */
// extern char *strchr(cstr __s, int __c) __attr_NTHLF(pure) __nonnull((1));
// /* Find the last occurrence of C in S.  */
// extern char *strrchr(cstr __s, int __c) __attr_NTHLF(pure) __nonnull((1));

// /* This function is similar to `strchr'.  But it returns a pointer to
//    the closing NUL byte in case C is not found in S.  */
// extern char *strchrnul(cstr __s, int __c) __attr_NTHLF(pure) __nonnull((1));

// /* Return the length of the initial segment of S which
//    consists entirely of characters not in REJECT.  */
// extern size_t strcspn(cstr __s, cstr __reject) __attr_NTHLF(pure) __nonnull((1, 2));
// /* Return the length of the initial segment of S which
//    consists entirely of characters in ACCEPT.  */
// size_t        strspn(cstr s, cstr accept) {
//   size_t cnt = 0;
//   for (; *s; ++s) {
//     cstr a = accept;
//     for (; *a; ++a) {
//       if (*s == *a) break;
//     }
//     if (!*a) return cnt;
//     ++cnt;
//   }
//   return cnt;
// }
// /* Find the first occurrence in S of any character in ACCEPT.  */
// extern char *strpbrk(cstr __s, cstr __accept) __attr_NTHLF(pure) __nonnull((1, 2));
// /* Find the first occurrence of NEEDLE in HAYSTACK.  */
// extern char *strstr(cstr __haystack, cstr __needle) __attr_NTHLF(pure)
//     __nonnull((1, 2));

// /* Divide S into tokens separated by characters in DELIM.  */
// extern char *strtok(char *_rest __s, cstr _rest __delim) __attr(nothrow, leaf)
//     __nonnull((2));

// /* Divide S into tokens separated by characters in DELIM.  Information
//    passed between calls are stored in SAVE_PTR.  */
// extern char *strtok_r(char *_rest __s, cstr _rest __delim, char **_rest __save_ptr)
//     __attr(nothrow, leaf) __nonnull((2, 3));

// /* Similar to `strstr' but this function ignores the case of both strings.  */
// extern char *strcasestr(cstr __haystack, cstr __needle) __attr_NTHLF(pure)
//     __nonnull((1, 2));

// /* Find the first occurrence of NEEDLE in HAYSTACK.
//    NEEDLE is NEEDLELEN bytes long;
//    HAYSTACK is HAYSTACKLEN bytes long.  */
// extern void *memmem(const void *__haystack, size_t __haystacklen, const void *__needle,
//                     size_t __needlelen) __attr_NTHLF(pure) __nonnull((1, 3))
//     __attr_access((__read_only__, 1, 2)) __attr_access((__read_only__, 3, 4));

// /* Copy N bytes of SRC to DEST, return pointer to bytes after the
//    last written byte.  */
// extern void *mempcpy(void *_rest __dest, const void *_rest __src, size_t __n)
//     __attr_NTHLF(nonnull(1, 2));

// /* Return the length of S.  */
// extern size_t strlen(cstr __s) __attr_NTHLF(pure) __nonnull((1));

// /* Find the length of STRING, but scan at most MAXLEN characters.
//    If no '\0' terminator is found in that many characters, return MAXLEN.  */
// extern size_t strnlen(cstr __string, size_t __maxlen) __attr_NTHLF(pure) __nonnull((1));

// #  include "LUT.h"
// #  include "std/errno.h"

// static char strerror_buf[32];

// //- Return a string describing the meaning of the `errno' code in ERRNUM.
// char *strerror(int e) {
//   if (e >= 0 && e <= ERRNO_MAX) {
//     return (void *)__LUT(strerror)[e];
//   } else {
//     sprintf(strerror_buf, "未知错误 %d", e);
//     return strerror_buf;
//   }
// }
// //- Reentrant version of `strerror'.
// //- There are 2 flavors of `strerror_r', GNU which returns the string
// //- and may or may not use the supplied temporary buffer and POSIX one
// //- which fills the string into the buffer.
// //- To use the POSIX version, -D_XOPEN_SOURCE=600 or -D_POSIX_C_SOURCE=200112L
// //- without -D_GNU_SOURCE is needed, otherwise the GNU version is
// //- preferred.
// //- Fill BUF with a string describing the meaning of the `errno' code in
// //- ERRNUM.
// //- If a temporary buffer is required, at most BUFLEN bytes of BUF will be
// //- used.
// char *strerror_r(int e, char *buf, size_t n) {
//   if (e >= 0 && e <= ERRNO_MAX) {
//     return strncpy(buf, __LUT(strerror)[e], n);
//   } else {
//     snprintf(buf, n, "未知错误 %d", e);
//     return buf;
//   }
// }

// /* Return a string describing the meaning of tthe error in ERR.  */
// extern cstr strerrordesc_np(int __err) __attr(nothrow, leaf);
// /* Return a string with the error name in ERR.  */
// extern cstr strerrorname_np(int __err) __attr(nothrow, leaf);

// /* Set N bytes of S to 0.  The compiler will not delete a call to this
//    function, even if S is dead after the call.  */
// extern void explicit_bzero(void *__s, size_t __n) __attr_NTHLF_nnul(1)
//     __fortified_attr_access(__write_only__, 1, 2);

// /* Return the next DELIM-delimited token from *STRINGP,
//    terminating it with a '\0', and update *STRINGP to point past it.  */
// extern char *strsep(char **_rest __stringp, cstr _rest __delim) __attr_NTHLF(nonnull(1, 2));

// /* Return a string describing the meaning of the signal number in SIG.  */
// extern char *strsignal(int __sig) __attr(nothrow, leaf);

// /* Return an abbreviation string for the signal number SIG.  */
// extern cstr sigabbrev_np(int __sig) __attr(nothrow, leaf);
// /* Return a string describing the meaning of the signal number in SIG,
//    the result is not translated.  */
// extern cstr sigdescr_np(int __sig) __attr(nothrow, leaf);

// /* Copy SRC to DEST, returning the address of the terminating '\0' in DEST.  */
// extern char *__stpcpy(char *_rest __dest, cstr _rest __src) __attr_NTHLF(nonnull(1, 2));
// extern char *stpcpy(char *_rest __dest, cstr _rest __src) __attr_NTHLF(nonnull(1, 2));

// /* Copy no more than N characters of SRC to DEST, returning the address of
//    the last character written into DEST.  */
// extern char *__stpncpy(char *_rest __dest, cstr _rest __src, size_t __n)
//     __attr_NTHLF(nonnull(1, 2));
// extern char *stpncpy(char *_rest __dest, cstr _rest __src, size_t __n)
//     __attr_NTHLF(nonnull(1, 2));

// /* Compare S1 and S2 as strings holding name & indices/version numbers.  */
// extern int strverscmp(cstr __s1, cstr __s2) __attr_NTHLF(pure) __nonnull((1, 2));

// /* Sautee STRING briskly.  */
// extern char *strfry(char *__string) __attr_NTHLF_nnul(1);

// /* Frobnicate N bytes of S.  */
// extern void *memfrob(void *__s, size_t __n) __attr_NTHLF_nnul(1)
//     __attr_access((__read_write__, 1, 2));

// /* Return the file name within directory of FILENAME.  We don't
//    declare the function if the `basename' macro is available (defined
//    in <libgen.h>) which makes the XPG version of this function
//    available.  */
// extern char *basename(cstr __filename) __attr_NTHLF_nnul(1);

// #endif
