#include "boot.h"
#include <dos.h>
#include <flanterm.h>

static mtask *display_owner;

bool platform_video_current_info(platform_video_info_t *info) {
  if (!info || !x64_boot_video.framebuffer)
    return false;
  *info = x64_boot_video;
  return true;
}
bool platform_video_set_mode(uint32_t width, uint32_t height, uint32_t bpp,
                             platform_video_info_t *info) {
  if (!width || !height || bpp != 32 ||
      (display_owner && display_owner->tgid != current_task()->tgid) ||
      !platform_video_current_info(info))
    return false;
  display_owner = current_task();
  memset((void *)info->framebuffer, 0, (size_t)info->pitch * info->height);
  arch_dma_sync_for_device((void *)info->framebuffer,
                           (size_t)info->pitch * info->height);
  return true;
}
bool platform_video_console_visible(void) { return display_owner == NULL; }
void platform_video_release_owner(mtask *task) {
  if (display_owner != task)
    return;
  display_owner = NULL;
  extern struct tty *tty_default;
  if (tty_default && tty_default->native_ansi) {
    flanterm_full_refresh(tty_default->vram);
    arch_dma_sync_for_device((void *)x64_boot_video.framebuffer,
                             (size_t)x64_boot_video.pitch *
                                 x64_boot_video.height);
  }
}
