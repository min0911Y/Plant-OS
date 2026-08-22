#ifndef __JCONFIG_H__
#define __JCONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#define NO_GETENV

#define HAVE_PROTOTYPES 1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1

#ifdef JPEG_INTERNALS
#define INLINE __inline__
#endif

#ifdef __cplusplus
}
#endif

#endif /* __JCONFIG_H__ */
