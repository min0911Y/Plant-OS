#ifndef PLANT_MACHINE_ENDIAN_H
#define PLANT_MACHINE_ENDIAN_H

#include <endian.h>

/* BSD headers expose the byte-order constants with a leading underscore. */
#ifdef _LITTLE_ENDIAN
#undef _LITTLE_ENDIAN
#endif
#ifdef _BIG_ENDIAN
#undef _BIG_ENDIAN
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define _LITTLE_ENDIAN LITTLE_ENDIAN
#else
#define _BIG_ENDIAN BIG_ENDIAN
#endif

#endif
