#include <arch.h>
#include <kasan.h>
#include <limits.h>

enum {
  MULTIBOOT2_BOOT_MAGIC = 0x36d76289u,
  MULTIBOOT2_TAG_END = 0,
  MULTIBOOT2_TAG_MODULE = 3,
  MULTIBOOT2_TAG_MEMORY_MAP = 6,
  MULTIBOOT2_MEMORY_AVAILABLE = 1,
  MULTIBOOT2_TAG_ALIGNMENT = 8,
  X86_PAGE_SIZE = 0x1000,
  X86_BOOT_PHYSICAL_LIMIT = 0xc0000000u,
};

typedef struct {
  uint32_t type;
  uint32_t size;
} multiboot2_tag_t;

typedef struct {
  multiboot2_tag_t tag;
  uint32_t start;
  uint32_t end;
  char command_line[];
} multiboot2_module_tag_t;

typedef struct {
  uint64_t address;
  uint64_t length;
  uint32_t type;
  uint32_t reserved;
} multiboot2_memory_entry_t;

typedef struct {
  multiboot2_tag_t tag;
  uint32_t entry_size;
  uint32_t entry_version;
  multiboot2_memory_entry_t entries[];
} multiboot2_memory_map_tag_t;

static boot_info_t boot_info;

static uintptr_t align_up(uintptr_t value, uintptr_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

static bool boot_memory_range_add(uint64_t base, uint64_t length,
                                  boot_memory_type_t type) {
  if (length == 0) {
    return true;
  }
  if (boot_info.memory_range_count != 0) {
    boot_memory_range_t *previous =
        &boot_info.memory_ranges[boot_info.memory_range_count - 1];
    uint64_t previous_end = previous->base + previous->length;
    if (previous->type == type && previous_end >= previous->base &&
        previous_end == base && length <= ULLONG_MAX - previous_end) {
      previous->length += length;
      return true;
    }
  }
  if (boot_info.memory_range_count == BOOT_MEMORY_RANGE_CAPACITY) {
    return false;
  }
  boot_info.memory_ranges[boot_info.memory_range_count++] =
      (boot_memory_range_t){.base = base, .length = length, .type = type};
  return true;
}

static void relocate_module(uintptr_t destination, uintptr_t source,
                            uint32_t size) {
  uint8_t *to = (uint8_t *)destination;
  const uint8_t *from = (const uint8_t *)source;

  if (destination < source) {
    for (uint32_t i = 0; i < size; i++) {
      to[i] = from[i];
    }
    return;
  }
  for (uint32_t i = size; i != 0; i--) {
    to[i - 1] = from[i - 1];
  }
}

static uintptr_t
find_module_destination(const multiboot2_memory_map_tag_t *memory_map,
                        uint32_t module_size) {
  if (module_size > UINT_MAX - (X86_PAGE_SIZE - 1)) {
    return 0;
  }
  uint32_t aligned_size = align_up(module_size, X86_PAGE_SIZE);
  uintptr_t minimum = align_up(KASAN_SHADOW_END + 1u, X86_PAGE_SIZE);
  uintptr_t best = 0;
  const uint8_t *tag_end =
      (const uint8_t *)memory_map + memory_map->tag.size;

  for (const multiboot2_memory_entry_t *entry = memory_map->entries;
       (const uint8_t *)entry + sizeof(*entry) <= tag_end;
       entry = (const multiboot2_memory_entry_t *)((const uint8_t *)entry +
                                                   memory_map->entry_size)) {
    if (entry->type != MULTIBOOT2_MEMORY_AVAILABLE ||
        entry->address >= X86_BOOT_PHYSICAL_LIMIT) {
      continue;
    }

    uint64_t end64 = entry->address + entry->length;
    if (end64 < entry->address) {
      end64 = ULLONG_MAX;
    }
    uintptr_t start = entry->address < minimum ? minimum : entry->address;
    uintptr_t end = end64 > X86_BOOT_PHYSICAL_LIMIT
                        ? X86_BOOT_PHYSICAL_LIMIT
                        : (uintptr_t)end64;
    start = align_up(start, X86_PAGE_SIZE);
    if (start > end || aligned_size > end - start) {
      continue;
    }
    if (best == 0 || start < best) {
      best = start;
    }
  }
  return best;
}

int x86_boot_prepare(uint32_t magic, uintptr_t information_address) {
  if (magic != MULTIBOOT2_BOOT_MAGIC) {
    return 0;
  }
  if ((information_address & (MULTIBOOT2_TAG_ALIGNMENT - 1)) != 0) {
    return -1;
  }

  const uint32_t *information = (const uint32_t *)information_address;
  uint32_t total_size = information[0];
  if (total_size < 16 || information_address > UINT_MAX - total_size) {
    return -1;
  }

  const multiboot2_module_tag_t *module = NULL;
  const multiboot2_memory_map_tag_t *memory_map = NULL;
  bool found_end = false;
  const uint8_t *end = (const uint8_t *)information + total_size;
  const multiboot2_tag_t *tag =
      (const multiboot2_tag_t *)((const uint8_t *)information + 8);

  while ((const uint8_t *)tag + sizeof(*tag) <= end) {
    if (tag->size < sizeof(*tag) || (const uint8_t *)tag + tag->size > end) {
      return -1;
    }
    if (tag->type == MULTIBOOT2_TAG_END) {
      found_end = tag->size == sizeof(*tag);
      break;
    }
    if (tag->type == MULTIBOOT2_TAG_MODULE && module == NULL &&
        tag->size >= sizeof(multiboot2_module_tag_t)) {
      module = (const multiboot2_module_tag_t *)tag;
    } else if (tag->type == MULTIBOOT2_TAG_MEMORY_MAP &&
               tag->size >= sizeof(multiboot2_memory_map_tag_t)) {
      const multiboot2_memory_map_tag_t *candidate =
          (const multiboot2_memory_map_tag_t *)tag;
      if (candidate->entry_size < sizeof(multiboot2_memory_entry_t)) {
        return -1;
      }
      memory_map = candidate;
    }

    uintptr_t next = align_up((uintptr_t)tag + tag->size,
                              MULTIBOOT2_TAG_ALIGNMENT);
    if (next <= (uintptr_t)tag) {
      return -1;
    }
    tag = (const multiboot2_tag_t *)next;
  }

  if (!found_end) {
    return -1;
  }
  if (memory_map != NULL) {
    const uint8_t *memory_map_end =
        (const uint8_t *)memory_map + memory_map->tag.size;
    for (const multiboot2_memory_entry_t *entry = memory_map->entries;
         (const uint8_t *)entry + sizeof(*entry) <= memory_map_end;
         entry = (const multiboot2_memory_entry_t *)((const uint8_t *)entry +
                                                     memory_map->entry_size)) {
      boot_memory_type_t type = entry->type == MULTIBOOT2_MEMORY_AVAILABLE
                                    ? BOOT_MEMORY_USABLE
                                    : BOOT_MEMORY_RESERVED;
      if (!boot_memory_range_add(entry->address, entry->length, type)) {
        return -1;
      }
    }
  }
  if (module == NULL) {
    return 0;
  }
  if (memory_map == NULL || module->end <= module->start) {
    return -1;
  }

  uint32_t module_size = module->end - module->start;
  uintptr_t destination = find_module_destination(memory_map, module_size);
  if (destination == 0) {
    return -1;
  }

  relocate_module(destination, module->start, module_size);
  boot_info.initramfs.address = destination;
  boot_info.initramfs.size = module_size;
  return 0;
}

const boot_info_t *arch_boot_info(void) { return &boot_info; }
