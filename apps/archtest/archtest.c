#include <framebuffer.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <time.h>

intptr_t libp_syscall3(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

static int reject_elf32(void) {
  /* A complete ELF32/i386 executable with one RX segment and an exit stub. */
  unsigned char image[91] = {0x7f, 'E', 'L', 'F', 1, 1, 1};
  uint16_t value16;
  uint32_t value32;
#define PUT16(offset, value)                                                   \
  do {                                                                         \
    value16 = (value);                                                         \
    memcpy(image + (offset), &value16, 2);                                     \
  } while (0)
#define PUT32(offset, value)                                                   \
  do {                                                                         \
    value32 = (value);                                                         \
    memcpy(image + (offset), &value32, 4);                                     \
  } while (0)
  PUT16(16, 2);
  PUT16(18, 3);
  PUT32(20, 1);
  PUT32(24, 0x70000000);
  PUT32(28, 52);
  PUT16(40, 52);
  PUT16(42, 32);
  PUT16(44, 1);
  PUT32(52, 1);
  PUT32(56, 84);
  PUT32(60, 0x70000000);
  PUT32(68, 7);
  PUT32(72, 7);
  PUT32(76, 5);
  PUT32(80, 1);
  memcpy(image + 84, "\xb8\x1e\x00\x00\x00\xcd\x36", 7);
  FILE *file = fopen("elf32.bin", "wb");
  if (!file)
    return 0;
  int wrote = fwrite(image, 1, sizeof(image), file) == sizeof(image);
  fclose(file);
  int rejected = wrote && exec("elf32.bin", "elf32.bin") == -1;
  remove("elf32.bin");
  return rejected;
}

int main(void) {
  _Static_assert(sizeof(void *) == 8 && sizeof(long) == 8, "native LP64 ABI");
  void *memory = malloc(128);
  if (!memory)
    return 1;
  int valid = (uintptr_t)memory >= 0x100000000ull && !((uintptr_t)memory & 15);
  memset(memory, 0xa5, 128);
  valid &= libp_syscall3(0xffff, 0, 0, 0) == -1;
  int child = fork();
  if (child == 0) {
    __asm__ volatile("int $0x36" : : : "memory");
    _exit(1);
  }
  valid &= child > 0 && waittid(child) == -1;
  valid &= monotonic_ns() != 0;
  volatile double nine = 9.0;
  valid &= sqrt(nine) == 3.0 && fabs(sin(0.5) - 0.479425538604203) < 1e-12;
  valid &= roundf(23.75f) == 24.0f && roundf(-0.5f) == -1.0f &&
           roundf(-2.5f) == -3.0f;
  valid &= reject_elf32();
  framebuffer_info_t before, after;
  valid &= framebuffer_info(&before) == 0;
  valid &= SwitchTo320X200X256() < 0 && SwitchToText8025() < 0;
  valid &= SwitchTo320X200X256_BIOS() < 0 && SwitchToText8025_BIOS() < 0;
  valid &= libp_syscall3(0x20, 1, 0x13, 0) < 0 &&
           libp_syscall3(0x20, 2, 0x13, 0) < 0;
  intptr_t address = set_mode(640, 480);
  valid &= address > 0 && framebuffer_info(&after) == 0;
  valid &= before.width == after.width && before.height == after.height &&
           before.pitch == after.pitch;
  if (address > 0) {
    volatile uint32_t *pixels = (void *)(uintptr_t)address;
    size_t index = after.pitch / 4 * 16 + 16;
    pixels[index] = 0x13579b;
    print("flanterm output while framebuffer is owned\n");
    valid &= pixels[index] == 0x13579b;
  }
  free(memory);
  logkf("ARCHTEST %s pointers=64 elf32=rejected legacy-video=error "
        "framebuffer=%ux%u pitch=%u\n",
        valid ? "PASS" : "FAIL", after.width, after.height, after.pitch);
  return !valid;
}
