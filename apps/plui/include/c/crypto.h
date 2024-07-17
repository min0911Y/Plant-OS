#pragma once
#include <define.h>
#include <type.h>

dlimport int base64_enc(char *_rest dst, size_t dstlen, const void *_rest src, size_t srclen);
dlimport int base64_dec(void *_rest dst, size_t dstlen, cstr _rest src, size_t srclen);

dlimport int caesar_enc(char *dst, cstr src, char passwd);
dlimport int caesar_dec(char *dst, cstr src, char passwd);

dlimport u32 strnhash(const void *_rest src, size_t len);
dlimport u32 strhash(cstr _rest src);

dlimport int hex_enc(char *_rest dst, size_t dstlen, const void *_rest src, size_t srclen);
dlimport int HEX_enc(char *_rest dst, size_t dstlen, const void *_rest src, size_t srclen);
dlimport int hex_dec(void *dst, cstr src);
dlimport int hex_sndec(void *dst, cstr src, size_t srclen);
dlimport int hex_dndec(void *dst, size_t dstlen, cstr src);
dlimport int hex_ndec(void *dst, size_t dstlen, cstr src, size_t srclen);
