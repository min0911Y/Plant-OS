#include "boot.h"
#include <dos.h>
#include <limits.h>

#define REQUEST __attribute__((used, section(".limine_requests")))
__attribute__((used,
               section(".limine_requests_start"))) static volatile uint64_t
    requests_start[] = LIMINE_REQUESTS_START_MARKER;
REQUEST static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);
REQUEST static volatile struct limine_hhdm_request hhdm = {
    .id = LIMINE_HHDM_REQUEST_ID};
REQUEST static volatile struct limine_memmap_request memory = {
    .id = LIMINE_MEMMAP_REQUEST_ID};
REQUEST static volatile struct limine_framebuffer_request framebuffer = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID};
REQUEST static volatile struct limine_module_request modules = {
    .id = LIMINE_MODULE_REQUEST_ID};
REQUEST static volatile struct limine_rsdp_request rsdp = {
    .id = LIMINE_RSDP_REQUEST_ID};
REQUEST static volatile struct limine_paging_mode_request paging = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL};
REQUEST volatile struct limine_mp_request x64_mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .flags = LIMINE_MP_REQUEST_X86_64_X2APIC};
__attribute__((
    used,
    section(".limine_requests_end"))) static volatile uint64_t requests_end[] =
    LIMINE_REQUESTS_END_MARKER;

uintptr_t x64_hhdm;
platform_video_info_t x64_boot_video;
static boot_info_t boot_info;

static __attribute__((noreturn)) void boot_failure(const char *reason) {
  logk("x86_64 boot: %s\n", reason);
  arch_halt();
}

void arch_boot_verify(void) {
  init_serial();
  if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision) || !hhdm.response ||
      !memory.response || !paging.response || paging.response->mode != 0 ||
      !framebuffer.response || !framebuffer.response->framebuffer_count ||
      !modules.response || modules.response->module_count != 1) {
    boot_failure("required Limine responses unavailable");
  }
  x64_hhdm = hhdm.response->offset;
  x64_cpu_initialize(&x64_cpus[0]);
  size_t count = memory.response->entry_count;
  if (!count || count > UINT_MAX ||
      count > (SIZE_MAX - 4095) / sizeof(boot_memory_range_t)) {
    boot_failure("invalid memory map");
  }
  size_t map_bytes =
      (count * sizeof(boot_memory_range_t) + 4095) & ~(size_t)4095;
  size_t storage_index = count;
  for (size_t i = 0; i < count; i++) {
    struct limine_memmap_entry *entry = memory.response->entries[i];
    if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= map_bytes) {
      storage_index = i;
      boot_info.memory_ranges = x64_physical_pointer(entry->base);
      break;
    }
  }
  if (storage_index == count) {
    boot_failure("no memory for boot map");
  }
  boot_info.memory_range_count = count;
  for (size_t i = 0; i < count; i++) {
    struct limine_memmap_entry *entry = memory.response->entries[i];
    boot_memory_range_t *range = &boot_info.memory_ranges[i];
    *range = (boot_memory_range_t){entry->base, entry->length,
                                   entry->type == LIMINE_MEMMAP_USABLE
                                       ? BOOT_MEMORY_USABLE
                                       : BOOT_MEMORY_RESERVED};
    if (i == storage_index) {
      range->base += map_bytes;
      range->length -= map_bytes;
    }
  }
  struct limine_file *module = modules.response->modules[0];
  if (!module->address || !module->size || module->size > UINT_MAX) {
    boot_failure("invalid initramfs");
  }
  boot_info.initramfs =
      (boot_module_t){(uintptr_t)module->address, module->size};
  boot_info.rsdp_physical =
      rsdp.response ? (uintptr_t)rsdp.response->address : 0;
  struct limine_framebuffer *fb = framebuffer.response->framebuffers[0];
  if (!fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
      fb->bpp != 32 || !fb->width || !fb->height || fb->width > UINT_MAX / 4 ||
      fb->height > UINT_MAX || fb->pitch < fb->width * 4 ||
      fb->pitch > UINT_MAX || fb->height > SIZE_MAX / fb->pitch ||
      fb->red_mask_size != 8 || fb->green_mask_size != 8 ||
      fb->blue_mask_size != 8) {
    boot_failure("unsupported framebuffer layout");
  }
  x64_boot_video = (platform_video_info_t){
      .framebuffer = (uintptr_t)fb->address,
      .width = fb->width,
      .height = fb->height,
      .pitch = fb->pitch,
      .physical_address = (uintptr_t)fb->address - x64_hhdm,
      .red_size = fb->red_mask_size,
      .red_shift = fb->red_mask_shift,
      .green_size = fb->green_mask_size,
      .green_shift = fb->green_mask_shift,
      .blue_size = fb->blue_mask_size,
      .blue_shift = fb->blue_mask_shift};
  logk("Plant OS x86_64: Limine native, framebuffer %ux%u pitch=%u\n",
       x64_boot_video.width, x64_boot_video.height, x64_boot_video.pitch);
}

const boot_info_t *arch_boot_info(void) { return &boot_info; }
uintptr_t arch_memory_detect(const boot_info_t *info) {
  uintptr_t end = 0;
  for (size_t i = 0; i < info->memory_range_count; i++) {
    const boot_memory_range_t *range = &info->memory_ranges[i];
    if (range->type == BOOT_MEMORY_USABLE &&
        range->base + range->length > end) {
      end = range->base + range->length;
    }
  }
  return end;
}
