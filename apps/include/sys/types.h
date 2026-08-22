#ifndef	_SYS_TYPES_H
#define	_SYS_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif
#define TYPEDEF typedef
#include <features.h>

#define __NEED_ino_t
#define __NEED_dev_t
#define __NEED_uid_t
#define __NEED_gid_t
#define __NEED_mode_t
#define __NEED_nlink_t
#define __NEED_off_t
#define __NEED_pid_t
#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_time_t
#define __NEED_timer_t
#define __NEED_clockid_t

#define __NEED_blkcnt_t
#define __NEED_fsblkcnt_t
#define __NEED_fsfilcnt_t

#define __NEED_id_t
#define __NEED_key_t
#define __NEED_clock_t
#define __NEED_suseconds_t
#define __NEED_blksize_t

#define __NEED_pthread_t
#define __NEED_pthread_attr_t
#define __NEED_pthread_mutexattr_t
#define __NEED_pthread_condattr_t
#define __NEED_pthread_rwlockattr_t
#define __NEED_pthread_barrierattr_t
#define __NEED_pthread_mutex_t
#define __NEED_pthread_cond_t
#define __NEED_pthread_rwlock_t
#define __NEED_pthread_barrier_t
#define __NEED_pthread_spinlock_t
#define __NEED_pthread_key_t
#define __NEED_pthread_once_t
#define __NEED_useconds_t

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define __NEED_int8_t
#define __NEED_int16_t
#define __NEED_int32_t
#define __NEED_int64_t
#define __NEED_u_int64_t
#define __NEED_register_t
#endif

#define _REDIR_TIME64 1
#define _Addr int
#define _Reg int

#define __BYTE_ORDER 1234
#define __LONG_MAX 0x7fffffffL
#ifndef __cplusplus
#ifdef __WCHAR_TYPE__
TYPEDEF __WCHAR_TYPE__ wchar_t;
#else
TYPEDEF long wchar_t;
#endif
#endif
#ifndef MATH_H
TYPEDEF float float_t;
TYPEDEF double double_t;
#endif
#if !defined(__cplusplus)
TYPEDEF struct { _Alignas(8) long long __ll; long double __ld; } max_align_t;
#elif defined(__GNUC__)
TYPEDEF struct { __attribute__((__aligned__(8))) long long __ll; long double __ld; } max_align_t;
#else
TYPEDEF struct { alignas(8) long long __ll; long double __ld; } max_align_t;
#endif


#if defined(_LARGEFILE64_SOURCE)
#define blkcnt64_t blkcnt_t
#define fsblkcnt64_t fsblkcnt_t
#define fsfilcnt64_t fsfilcnt_t
#define ino64_t ino_t
#define off64_t off_t
#endif

#ifdef __cplusplus
}
#endif
#endif
