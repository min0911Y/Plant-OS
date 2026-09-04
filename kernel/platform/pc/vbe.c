#include <arch/x86/i386/bios.h>
#include <drivers.h>
#include <io.h>
#include <platform.h>
#include "vbe.h"

enum {
  VBE_FUNCTION_CONTROLLER_INFO = 0x4f00,
  VBE_FUNCTION_MODE_INFO = 0x4f01,
  VBE_FUNCTION_SET_MODE = 0x4f02,
  VBE_SUCCESS = 0x004f,
  VBE_LINEAR_FRAMEBUFFER = 0x4000,
  VBE_CONTROLLER_BUFFER = VBEINFO_ADDRESS,
  VBE_MODE_BUFFER = 0x7000,
  VBE_MODE_LIMIT = 1024,
};

static platform_video_info_t current_video;
static bool current_video_valid;

static void bios_buffer_pointer(regs16_t *registers, uintptr_t address) {
  registers->es = (uint16_t)(address >> 4);
  registers->di = (uint16_t)(address & 0xf);
}

static int query_mode(int mode, uintptr_t buffer) {
  if (mode < 0 || mode > 0x3ff) {
    return -1;
  }
  regs16_t registers = {0};
  registers.ax = VBE_FUNCTION_MODE_INFO;
  registers.cx = (uint16_t)(mode | VBE_LINEAR_FRAMEBUFFER);
  bios_buffer_pointer(&registers, buffer);
  x86_bios_interrupt(0x10, &registers);
  return registers.ax == VBE_SUCCESS ? 0 : -1;
}

static int set_mode(int mode) {
  if (mode < 0 || mode > 0x3ff) {
    return -1;
  }
  regs16_t registers = {0};
  registers.ax = VBE_FUNCTION_SET_MODE;
  registers.bx = (uint16_t)(mode | VBE_LINEAR_FRAMEBUFFER);
  x86_bios_interrupt(0x10, &registers);
  return registers.ax == VBE_SUCCESS ? 0 : -1;
}

static void update_current_video(const struct VBEINFO *vbe) {
  current_video.framebuffer = (uintptr_t)(uint32_t)vbe->vram;
  current_video.width = (uint32_t)(uint16_t)vbe->xsize;
  current_video.height = (uint32_t)(uint16_t)vbe->ysize;
  current_video_valid = current_video.framebuffer != 0 &&
                        current_video.width != 0 && current_video.height != 0;
}

int platform_video_check_mode(int mode) {
  return query_mode(mode, VBE_CONTROLLER_BUFFER);
}

int platform_video_switch_mode(int mode) {
  if (query_mode(mode, VBE_CONTROLLER_BUFFER) != 0 || set_mode(mode) != 0) {
    return -1;
  }
  update_current_video((const struct VBEINFO *)(uintptr_t)VBE_CONTROLLER_BUFFER);
  return 0;
}

bool platform_video_set_mode(uint32_t width, uint32_t height, uint32_t bpp,
                             platform_video_info_t *info) {
  regs16_t registers = {0};
  registers.ax = VBE_FUNCTION_CONTROLLER_INFO;
  bios_buffer_pointer(&registers, VBE_CONTROLLER_BUFFER);
  x86_bios_interrupt(0x10, &registers);
  if (registers.ax != VBE_SUCCESS) {
    return false;
  }

  const VESAControllerInfo *controller =
      (const VESAControllerInfo *)(uintptr_t)VBE_CONTROLLER_BUFFER;
  const uint16_t *modes =
      (const uint16_t *)rmfarptr2ptr(controller->videoModes);
  if (modes == NULL) {
    return false;
  }

  for (unsigned index = 0; index < VBE_MODE_LIMIT; index++) {
    uint16_t mode = modes[index];
    if (mode == 0xffffu) {
      break;
    }
    if (query_mode(mode, VBE_MODE_BUFFER) != 0) {
      continue;
    }
    const VESAModeInfo *candidate =
        (const VESAModeInfo *)(uintptr_t)VBE_MODE_BUFFER;
    if (candidate->width != width || candidate->height != height ||
        candidate->bitsPerPixel != bpp) {
      continue;
    }
    if (platform_video_switch_mode(mode) != 0 || !current_video_valid) {
      return false;
    }
    if (info != NULL) {
      *info = current_video;
    }
    return true;
  }
  return false;
}

bool platform_video_current_info(platform_video_info_t *info) {
  if (!current_video_valid || info == NULL) {
    return false;
  }
  *info = current_video;
  return true;
}

void platform_video_text_mode(void) {
  regs16_t registers = {0};
  registers.ax = 0x0003;
  x86_bios_interrupt(0x10, &registers);
  current_video_valid = false;
  platform_palette_reset();
  clear();
}

void platform_video_graphics_mode(void) {
  regs16_t registers = {0};
  registers.ax = 0x0013;
  x86_bios_interrupt(0x10, &registers);
  current_video_valid = false;
}
