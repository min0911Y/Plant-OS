#ifndef KERNEL_DMA_H
#define KERNEL_DMA_H

#include <arch.h>
#include <ctypes.h>

typedef uint64_t dma_addr_t;

static inline bool dma_map(const void *address, size_t size,
                           dma_addr_t address_limit,
                           dma_addr_t *dma_address) {
  dma_addr_t mapped;
  if (dma_address == NULL || !arch_dma_map(address, size, &mapped) ||
      mapped > address_limit ||
      (size != 0 && size - 1 > address_limit - mapped)) {
    return false;
  }
  *dma_address = mapped;
  return true;
}

static inline void dma_sync_for_device(const void *address, size_t size) {
  arch_dma_sync_for_device(address, size);
}

static inline void dma_sync_for_cpu(const void *address, size_t size) {
  arch_dma_sync_for_cpu(address, size);
}

#endif
