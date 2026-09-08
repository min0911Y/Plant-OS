#include <fenv.h>
#include <stddef.h>

_Static_assert(offsetof(fenv_t, mxcsr) == 28 && sizeof(fenv_t) == 32,
               "x87 environment layout");

int fegetenv(fenv_t *environment) {
  if (!environment)
    return -1;
  __asm__ volatile("fnstenv %0\n\tfldenv %0" : "=m"(*environment));
#ifdef PLANT_ARCH_X86_64
  __asm__ volatile("stmxcsr %0" : "=m"(environment->mxcsr));
#else
  environment->mxcsr = 0;
#endif
  return 0;
}

int fesetenv(const fenv_t *environment) {
  const fenv_t initial = {.control = 0x37f, .tags = 0xffff, .mxcsr = 0x1f80};
  if (environment == FE_DFL_ENV)
    environment = &initial;
  if (!environment || (environment->mxcsr & ~0xffffu))
    return -1;
  __asm__ volatile("fldenv %0" : : "m"(*environment));
#ifdef PLANT_ARCH_X86_64
  __asm__ volatile("ldmxcsr %0" : : "m"(environment->mxcsr));
#endif
  return 0;
}

int fegetround(void) {
#ifdef PLANT_ARCH_X86_64
  uint32_t control;
  __asm__ volatile("stmxcsr %0" : "=m"(control));
  return (control >> 3) & FE_TOWARDZERO;
#else
  uint16_t control;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  return control & FE_TOWARDZERO;
#endif
}

int fesetround(int mode) {
  if (mode & ~FE_TOWARDZERO)
    return -1;
  uint16_t control;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  control = (control & ~FE_TOWARDZERO) | mode;
  __asm__ volatile("fldcw %0" : : "m"(control));
#ifdef PLANT_ARCH_X86_64
  uint32_t mxcsr;
  __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
  mxcsr = (mxcsr & ~(FE_TOWARDZERO << 3)) | (mode << 3);
  __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
#endif
  return 0;
}

int fetestexcept(int exceptions) {
  uint16_t status;
  __asm__ volatile("fnstsw %0" : "=am"(status));
#ifdef PLANT_ARCH_X86_64
  uint32_t mxcsr;
  __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
  status |= mxcsr;
#endif
  return status & exceptions & FE_ALL_EXCEPT;
}

int fegetexceptflag(fexcept_t *flags, int exceptions) {
  if (!flags)
    return -1;
  *flags = fetestexcept(exceptions);
  return 0;
}

int fesetexceptflag(const fexcept_t *flags, int exceptions) {
  if (!flags)
    return -1;
  fenv_t environment;
  fegetenv(&environment);
  unsigned mask = exceptions & FE_ALL_EXCEPT;
  environment.status &= ~mask;
  environment.mxcsr &= ~mask;
#ifdef PLANT_ARCH_X86_64
  environment.mxcsr |= *flags & mask;
#else
  environment.status |= *flags & mask;
#endif
  return fesetenv(&environment);
}

int feclearexcept(int exceptions) {
  const fexcept_t flags = 0;
  return fesetexceptflag(&flags, exceptions);
}

int feraiseexcept(int exceptions) {
  fexcept_t flags = exceptions & FE_ALL_EXCEPT;
  return fesetexceptflag(&flags, exceptions);
}

int feholdexcept(fenv_t *environment) {
  if (fegetenv(environment))
    return -1;
  fenv_t held = *environment;
  held.control |= 0x3f;
  held.status &= ~FE_ALL_EXCEPT;
  held.mxcsr = (held.mxcsr | 0x1f80) & ~FE_ALL_EXCEPT;
  return fesetenv(&held);
}

int feupdateenv(const fenv_t *environment) {
  int raised = fetestexcept(FE_ALL_EXCEPT);
  if (fesetenv(environment))
    return -1;
  return feraiseexcept(raised);
}
