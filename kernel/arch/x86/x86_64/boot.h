#ifndef X64_BOOT_H
#define X64_BOOT_H
#include <arch/x86/x86_64/cpu.h>
#include <limine.h>
#include <platform.h>

extern volatile struct limine_mp_request x64_mp_request;
extern x64_cpu_t x64_cpus[];
extern platform_video_info_t x64_boot_video;
#endif
