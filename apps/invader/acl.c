#if (!defined(ASTATIC))
	#define ASTATIC static
#endif

#if (!defined(AINLINESTATIC))
	#define AINLINESTATIC inline static
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define AARCH_X86
#if (defined(AARCH_X86))
	#define AINTBIT		32
	typedef signed char AInt8;
	typedef int AInt16;
	typedef int AInt32;
	typedef long long AInt64;
	typedef unsigned char AUInt8;
	typedef int AUInt16;
	typedef unsigned int AUInt32;
	typedef unsigned long long AUInt64;
#elif (defined(AARCH_X64))
	#define AINTBIT		64
	typedef signed char AInt8;
	typedef int AInt16;
	typedef int AInt32;
	typedef long long AInt64;
	typedef unsigned char AUInt8;
	typedef int AUInt16;
	typedef unsigned int AUInt32;
	typedef unsigned long long AUInt64;
#elif (defined(AARCH_ARM32))
	#define AINTBIT		32
	typedef signed char AInt8;
	typedef int AInt16;
	typedef int AInt32;
	typedef long long AInt64;
	typedef unsigned char AUInt8;
	typedef int AUInt16;
	typedef unsigned int AUInt32;
	typedef unsigned long long AUInt64;
#elif (defined(AARCH_AARCH64))
	#define AINTBIT		64
	typedef signed char AInt8;
	typedef int AInt16;
	typedef int AInt32;
	typedef long long AInt64;
	typedef unsigned char AUInt8;
	typedef int AUInt16;
	typedef unsigned int AUInt32;
	typedef unsigned long long AUInt64;
#endif

typedef int8_t  AInt8a;
typedef int16_t AInt16a;
typedef int32_t AInt32a;
typedef int64_t AInt64a;
typedef uint8_t  AUInt8a;
typedef uint16_t AUInt16a;
typedef uint32_t AUInt32a;
typedef uint64_t AUInt64a;
typedef intptr_t AInt;
typedef uintptr_t AUInt;
typedef FILE AFile;

#define aStdin stdin
#define aStdout stdout
#define aStderr stderr

#define avfPrintf	vfprintf
#define afPrintf	fprintf
#define aExitInt	exit
#define aPrintf		printf
#define avsnPrintf	vsnprintf

#define aSizeof		(AInt) sizeof

void aMain();

static int aArgc;
static char **aArgv;

#if (!defined(ANOUSE_ATERRMSG))
	static char *aAtErrExitMsg;
#endif

ASTATIC void aErrExit(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
	#if (!defined(ANOUSE_ATERRMSG))
		if (aAtErrExitMsg != 0)
		    fprintf(aStderr, "\n%s\n", aAtErrExitMsg);
	#endif
    vfprintf(aStderr, f, ap);
    fprintf(aStderr, "\n");
    va_end(ap);
    aExitInt(1);
}

ASTATIC void *aErrExitP0(void *p, const char *f, ...)
{
	if (p != 0)
		return p;
    va_list ap;
    va_start(ap, f);
	#if (!defined(ANOUSE_ATERRMSG))
		if (aAtErrExitMsg != 0)
		    fprintf(aStderr, "\n%s\n", aAtErrExitMsg);
	#endif
    vfprintf(aStderr, f, ap);
    fprintf(aStderr, "\n");
    va_end(ap);
    aExitInt(1);
}

#if (!defined(ANOUSE_FASTMALLOC))
	#define ANOUSE_FASTMALLOC
#endif

ASTATIC void *aMalloc(AInt sz) { return aErrExitP0(malloc(sz), "aMalloc: error"); }

ASTATIC void aFree(void *p, AInt sz)
{
	(void) sz;
	if (p != 0)
		free(p);
}

#define aDbgErrExit(...)
//ASTATIC void *aMalloc(AInt s);

#define AMODE_SET		0
#define AMODE_OR		1
#define AMODE_AND		2
#define AMODE_XOR		3

#define AKEY_ENTER		10
#define AKEY_ESC		27
#define AKEY_BACKSPACE	8
#define AKEY_TAB		9
#define AKEY_PAGEUP		0x1020
#define AKEY_PAGEDWN		0x1021
#define	AKEY_END		0x1022
#define	AKEY_HOME		0x1023
#define AKEY_LEFT		0x1024
#define AKEY_RIGHT		0x1025
#define AKEY_UP		0x1026
#define AKEY_DOWN		0x1027
#define AKEY_INS		0x1028
#define AKEY_DEL		0x1029

#define AKEY_LOCKS		0x00f00000

#define AKEY_LV0		0x00
#define AKEY_LV1		0x01
#define AMOS_LV0		0x00
#define AMOS_LV1		0x10	// クリックのみ.
#define AMOS_LV2		0x20	// MOVEすべて.
#define AMOS_LV3		0x30	// ホイールも.

//#include "astring.c"
//#define aStrtoAInt	strtoll
//#include "amisc.c"
//#include "amalloc.c"

#if (!defined(AMAXWINDOWS))
	#define AMAXWINDOWS	16
#endif

#if (!defined(AKEYBUFSIZ))
	#define AKEYBUFSIZ		1024
#endif

#include "sdl2.c"
#include "graphics.c"

int main(int argc, char **argv)
{
	aArgc = argc;
	aArgv = argv;
	#if (!defined(ANOUSE_FASTMALLOC))
		aMalloc_indexTableInit();
	#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
		aErrExit("SDL2 init error");
	aMain();
	return 0;
}
