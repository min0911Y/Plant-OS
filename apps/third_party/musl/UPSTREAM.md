# musl algorithms used by libp

Source: https://musl.libc.org/releases/musl-1.2.5.tar.gz

SHA256: `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`

This is a selection of algorithms, not a second libc or a Linux ABI layer.
Plant OS owns allocation, FILE objects, threading, TLS, syscalls and file access.
The release copyright file is retained as `COPYRIGHT`; individual math files
also retain their upstream MIT, Sun or BSD notices.

- `floatscan.c` and `intscan.c` come from `src/internal/`. Their scanner input is
  adapted to `scan_input_t`, with names `plant_floatscan`/`plant_intscan`.
- `scanf.c` comes from `src/stdio/vfscanf.c`. It uses the same native scanner
  adapter and opaque FILE API, without importing musl's private FILE layout.
- `scan.h` is the native string/FILE adapter. The public strto* and scanf-family
  wrappers live in `apps/libp/`.
- `error_strings.h` comes from `src/errno/__strerror.h`; the native wrapper
  indexes it with the project's errno constants.
- `math/*.c` and data declarations come from `src/math/`. The explicit source
  list in `apps/dynamic.mk` is the build authority. The routines cover the
  additional scalar math used by LLVM and Mesa, including software FMA and OpenAL float sinh/cube-root/expm1.
- `math/libm.h` is a native adapter to existing bit helpers. It selects x87
  evaluation precision on i386. `rint.c`, `rintf.c`, `logb.c`, and `fma.c` include
  this adapter; `fma.c` uses the compiler's CLZ intrinsic instead of musl's
  internal atomic header. Data headers use native visibility attributes instead
  of musl `features.h`. `exp2f` reuses libp's existing `__exp2f_data` definition.

Scalar algorithms otherwise retain their upstream bodies and formatting.
Native floating-point environment support is in `libp/arch/x86/fenv.c`, and
architecture-specific sqrt/long-double primitives stay in the architecture
backend. `libctest` exercises parser overflow, subnormals, binary/large stdio,
FMA cancellation, rounding, floating flags and thread environment inheritance.
