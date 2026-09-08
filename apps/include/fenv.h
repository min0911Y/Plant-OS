#ifndef PLANT_FENV_H
#define PLANT_FENV_H
#include <stdint.h>

#define FE_INVALID 1
#define FE_DIVBYZERO 4
#define FE_OVERFLOW 8
#define FE_UNDERFLOW 16
#define FE_INEXACT 32
#define FE_ALL_EXCEPT                                                          \
  (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)
#define FE_TONEAREST 0
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00
typedef unsigned short fexcept_t;
typedef struct {
  uint16_t control, reserved_control;
  uint16_t status, reserved_status;
  uint16_t tags, reserved_tags;
  uint32_t instruction;
  uint16_t code_selector, opcode;
  uint32_t data;
  uint16_t data_selector, reserved_data;
  uint32_t mxcsr;
} fenv_t;
#define FE_DFL_ENV ((const fenv_t *)-1)

#ifdef __cplusplus
extern "C" {
#endif
int feclearexcept(int exceptions);
int fegetexceptflag(fexcept_t *flags, int exceptions);
int feraiseexcept(int exceptions);
int fesetexceptflag(const fexcept_t *flags, int exceptions);
int fetestexcept(int exceptions);
int fegetround(void);
int fesetround(int mode);
int fegetenv(fenv_t *environment);
int feholdexcept(fenv_t *environment);
int fesetenv(const fenv_t *environment);
int feupdateenv(const fenv_t *environment);
#ifdef __cplusplus
}
#endif
#endif
