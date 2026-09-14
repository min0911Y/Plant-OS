/* Plant's C ABI uses an eight-byte long double, matching -mlong-double-64. */
#include "ffi.h"

ffi_type ffi_type_longdouble = {
    sizeof(long double), _Alignof(long double), FFI_TYPE_LONGDOUBLE, NULL};
