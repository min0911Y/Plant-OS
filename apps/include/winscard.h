#ifndef PLANT_WINSCARD_H
#define PLANT_WINSCARD_H

#include <ctypes.h>

typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint8_t BYTE;
typedef uintptr_t SCARDCONTEXT;
typedef uintptr_t SCARDHANDLE;
typedef const void *LPCVOID;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef const BYTE *LPCBYTE;
typedef BYTE *LPBYTE;
typedef DWORD *LPDWORD;
typedef SCARDCONTEXT *LPSCARDCONTEXT;
typedef SCARDHANDLE *LPSCARDHANDLE;

typedef struct {
  DWORD dwProtocol;
  DWORD cbPciLength;
} SCARD_IO_REQUEST;

typedef struct {
  LPCSTR szReader;
  LPVOID pvUserData;
  DWORD dwCurrentState;
  DWORD dwEventState;
  DWORD cbAtr;
  BYTE rgbAtr[36];
} SCARD_READERSTATE;

#define SCARD_S_SUCCESS ((LONG)0)
#define SCARD_STATE_UNAWARE 0x0000

#endif
