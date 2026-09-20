// SPDX-License-Identifier: MIT
#include "plant_vulkan.h"
#include "util/cnd_monotonic.h"
#include "util/timespec.h"
#include "vulkan/runtime/vk_instance.h"
#include "vulkan/wsi/wsi_common_private.h"
#include <limits.h>

struct plant_surface {
  VkIcdSurfaceBase base;
  window_t window;
  VkRect2D client;
  VkExtent2D margin;
};

struct plant_image {
  struct wsi_image base;
  bool busy;
};

struct plant_swapchain {
  struct wsi_swapchain base;
  struct plant_surface surface;
  mtx_t mutex;
  struct u_cnd_monotonic available;
  VkResult status;
  uint32_t next;
  struct plant_image images[];
};

static bool surface_buffer(struct plant_surface *surface,
                           window_buffer_t *buffer) {
  VkRect2D *r = &surface->client;
  if (window_get_buffer(surface->window, buffer) != 0 || r->offset.x < 0 ||
      r->offset.y < 0 || buffer->width > INT16_MAX ||
      buffer->height > INT16_MAX ||
      (uint32_t)r->offset.x + surface->margin.width >= buffer->width ||
      (uint32_t)r->offset.y + surface->margin.height >= buffer->height)
    return false;
  r->extent.width = buffer->width - r->offset.x - surface->margin.width;
  r->extent.height = buffer->height - r->offset.y - surface->margin.height;
  return true;
}

VkResult plant_vulkan_create_surface(VkInstance handle, window_t window,
                                     const VkRect2D *client,
                                     const VkAllocationCallbacks *allocator,
                                     VkSurfaceKHR *result) {
  if (!handle || !client || !result)
    return VK_ERROR_INITIALIZATION_FAILED;
  *result = VK_NULL_HANDLE;
  VK_FROM_HANDLE(vk_instance, instance, handle);
  struct plant_surface value = {
      .base.platform = VK_ICD_WSI_PLATFORM_PLANTOS,
      .window = window,
      .client = *client,
  };
  window_buffer_t buffer;
  if (window_get_buffer(window, &buffer) != 0 || client->offset.x < 0 ||
      client->offset.y < 0 || !client->extent.width || !client->extent.height ||
      (uint32_t)client->offset.x >= buffer.width ||
      (uint32_t)client->offset.y >= buffer.height ||
      client->extent.width > buffer.width - client->offset.x ||
      client->extent.height > buffer.height - client->offset.y)
    return VK_ERROR_SURFACE_LOST_KHR;
  value.margin =
      (VkExtent2D){buffer.width - client->offset.x - client->extent.width,
                   buffer.height - client->offset.y - client->extent.height};
  if (!surface_buffer(&value, &buffer))
    return VK_ERROR_SURFACE_LOST_KHR;
  struct plant_surface *surface =
      vk_alloc2(&instance->alloc, allocator, sizeof(*surface), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
  if (!surface)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *surface = value;
  *result = VkIcdSurfaceBase_to_handle(&surface->base);
  return VK_SUCCESS;
}

static VkResult surface_support(VkIcdSurfaceBase *base, struct wsi_device *wsi,
                                uint32_t family, VkBool32 *supported) {
  window_buffer_t buffer;
  *supported = wsi->sw && family < wsi->queue_family_count &&
               surface_buffer((struct plant_surface *)base, &buffer);
  return VK_SUCCESS;
}

static VkResult surface_capabilities(VkIcdSurfaceBase *base,
                                     struct wsi_device *wsi, const void *next,
                                     VkSurfaceCapabilities2KHR *caps) {
  struct plant_surface *surface = (struct plant_surface *)base;
  window_buffer_t buffer;
  if (!surface_buffer(surface, &buffer))
    return VK_ERROR_SURFACE_LOST_KHR;
  caps->surfaceCapabilities = (VkSurfaceCapabilitiesKHR){
      .minImageCount = 2,
      .maxImageCount = 0,
      .currentExtent = surface->client.extent,
      .minImageExtent = surface->client.extent,
      .maxImageExtent = surface->client.extent,
      .maxImageArrayLayers = 1,
      .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .supportedUsageFlags = wsi_caps_get_image_usage(),
  };
  vk_foreach_struct(extension, caps->pNext) {
    switch (extension->sType) {
    case VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR:
      ((VkSurfaceProtectedCapabilitiesKHR *)extension)->supportsProtected =
          false;
      break;
    case VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR: {
      VkSurfacePresentScalingCapabilitiesKHR *scaling = (void *)extension;
      scaling->supportedPresentScaling = 0;
      scaling->supportedPresentGravityX = scaling->supportedPresentGravityY = 0;
      scaling->minScaledImageExtent = scaling->maxScaledImageExtent =
          surface->client.extent;
      break;
    }
    case VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR: {
      VkSurfacePresentModeCompatibilityKHR *modes = (void *)extension;
      if (modes->pPresentModes && modes->presentModeCount)
        modes->pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
      modes->presentModeCount = 1;
      break;
    }
    default:
      break;
    }
  }
  return VK_SUCCESS;
}

static const VkSurfaceFormatKHR format = {VK_FORMAT_B8G8R8A8_UNORM,
                                          VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

static VkResult surface_formats(VkIcdSurfaceBase *surface,
                                struct wsi_device *wsi, uint32_t *count,
                                VkSurfaceFormatKHR *formats) {
  VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormatKHR, output, formats, count);
  vk_outarray_append_typed(VkSurfaceFormatKHR, &output, item) {
    *item = format;
  }
  return vk_outarray_status(&output);
}

static VkResult surface_formats2(VkIcdSurfaceBase *surface,
                                 struct wsi_device *wsi, const void *next,
                                 uint32_t *count,
                                 VkSurfaceFormat2KHR *formats) {
  VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormat2KHR, output, formats, count);
  vk_outarray_append_typed(VkSurfaceFormat2KHR, &output, item) {
    item->surfaceFormat = format;
  }
  return vk_outarray_status(&output);
}

static VkResult surface_modes(VkIcdSurfaceBase *surface, struct wsi_device *wsi,
                              uint32_t *count, VkPresentModeKHR *modes) {
  VK_OUTARRAY_MAKE_TYPED(VkPresentModeKHR, output, modes, count);
  vk_outarray_append_typed(VkPresentModeKHR, &output, item) {
    *item = VK_PRESENT_MODE_FIFO_KHR;
  }
  return vk_outarray_status(&output);
}

static VkResult surface_rectangles(VkIcdSurfaceBase *base,
                                   struct wsi_device *wsi, uint32_t *count,
                                   VkRect2D *rectangles) {
  struct plant_surface *surface = (struct plant_surface *)base;
  window_buffer_t buffer;
  if (!surface_buffer(surface, &buffer))
    return VK_ERROR_SURFACE_LOST_KHR;
  VK_OUTARRAY_MAKE_TYPED(VkRect2D, output, rectangles, count);
  vk_outarray_append_typed(VkRect2D, &output, item) {
    *item = (VkRect2D){.extent = surface->client.extent};
  }
  return vk_outarray_status(&output);
}

static struct wsi_image *swapchain_image(struct wsi_swapchain *base,
                                         uint32_t index) {
  return &((struct plant_swapchain *)base)->images[index].base;
}

static VkResult swapchain_acquire(struct wsi_swapchain *base,
                                  const VkAcquireNextImageInfoKHR *info,
                                  uint32_t *index) {
  struct plant_swapchain *chain = (struct plant_swapchain *)base;
  uint64_t now = monotonic_ns();
  uint64_t timeout = info->timeout;
  uint64_t deadline = timeout >= UINT64_MAX - now ? UINT64_MAX : now + timeout;
  struct timespec until = {.tv_sec = deadline / 1000000000,
                           .tv_nsec = deadline % 1000000000};
  mtx_lock(&chain->mutex);
  VkResult result = chain->status;
  while (result == VK_SUCCESS) {
    uint32_t candidate = chain->next;
    for (uint32_t i = 0; i < base->image_count; i++) {
      if (!chain->images[candidate].busy) {
        chain->images[candidate].busy = true;
        chain->next = (candidate + 1) % base->image_count;
        *index = candidate;
        mtx_unlock(&chain->mutex);
        return VK_SUCCESS;
      }
      if (++candidate == base->image_count)
        candidate = 0;
    }
    if (!timeout) {
      result = VK_NOT_READY;
      break;
    }
    int waited = deadline == UINT64_MAX
                     ? u_cnd_monotonic_wait(&chain->available, &chain->mutex)
                     : u_cnd_monotonic_timedwait(&chain->available,
                                                 &chain->mutex, &until);
    result = waited == thrd_timedout  ? VK_TIMEOUT
             : waited != thrd_success ? VK_ERROR_DEVICE_LOST
                                      : chain->status;
  }
  mtx_unlock(&chain->mutex);
  return result;
}

static VkResult swapchain_release(struct wsi_swapchain *base, uint32_t count,
                                  const uint32_t *indices) {
  struct plant_swapchain *chain = (struct plant_swapchain *)base;
  mtx_lock(&chain->mutex);
  for (uint32_t i = 0; i < count; i++)
    chain->images[indices[i]].busy = false;
  u_cnd_monotonic_broadcast(&chain->available);
  mtx_unlock(&chain->mutex);
  return VK_SUCCESS;
}

static VkResult swapchain_present(struct wsi_swapchain *base, uint32_t index,
                                  uint64_t present_id,
                                  const VkPresentRegionKHR *damage) {
  struct plant_swapchain *chain = (struct plant_swapchain *)base;
  struct wsi_image *image = &chain->images[index].base;
  const VkRect2D *r = &chain->surface.client;
  window_buffer_t buffer;
  mtx_lock(&chain->mutex);
  VkExtent2D extent = r->extent;
  if (chain->status == VK_SUCCESS) {
    if (!surface_buffer(&chain->surface, &buffer) || !image->cpu_map) {
      chain->status = VK_ERROR_SURFACE_LOST_KHR;
    } else if (extent.width != r->extent.width ||
               extent.height != r->extent.height) {
      chain->status = VK_ERROR_OUT_OF_DATE_KHR;
    } else {
      uint8_t *destination = (uint8_t *)buffer.pixels +
                             r->offset.y * buffer.pitch + r->offset.x * 4;
      const uint8_t *source = image->cpu_map;
      for (uint32_t y = 0; y < r->extent.height; y++)
        memcpy(destination + y * buffer.pitch,
               source + y * image->row_pitches[0], (size_t)r->extent.width * 4);
      unsigned x1 = r->offset.x + r->extent.width,
               y1 = r->offset.y + r->extent.height;
      if (window_present(chain->surface.window,
                         (r->offset.x << 16) | r->offset.y, (x1 << 16) | y1))
        chain->status = VK_ERROR_SURFACE_LOST_KHR;
    }
  }
  chain->images[index].busy = false;
  u_cnd_monotonic_broadcast(&chain->available);
  VkResult result = chain->status;
  mtx_unlock(&chain->mutex);
  return result;
}

static VkResult swapchain_destroy(struct wsi_swapchain *base,
                                  const VkAllocationCallbacks *allocator) {
  struct plant_swapchain *chain = (struct plant_swapchain *)base;
  for (uint32_t i = 0; i < base->image_count; i++)
    wsi_destroy_image(base, &chain->images[i].base);
  wsi_swapchain_finish(base);
  u_cnd_monotonic_destroy(&chain->available);
  mtx_destroy(&chain->mutex);
  vk_free(allocator, chain);
  return VK_SUCCESS;
}

static VkResult swapchain_wait(struct wsi_swapchain *base, uint64_t id,
                               uint64_t timeout) {
  return wsi_swapchain_wait_for_present_semaphore(base, id, timeout);
}

static VkResult surface_swapchain(VkIcdSurfaceBase *surface, VkDevice device,
                                  struct wsi_device *wsi,
                                  const VkSwapchainCreateInfoKHR *info,
                                  const VkAllocationCallbacks *allocator,
                                  struct wsi_swapchain **output) {
  struct plant_surface *native = (struct plant_surface *)surface;
  if (info->oldSwapchain) {
    VK_FROM_HANDLE(wsi_swapchain, old_base, info->oldSwapchain);
    struct plant_swapchain *old = (struct plant_swapchain *)old_base;
    mtx_lock(&old->mutex);
    old->status = VK_ERROR_OUT_OF_DATE_KHR;
    u_cnd_monotonic_broadcast(&old->available);
    mtx_unlock(&old->mutex);
  }
  window_buffer_t buffer;
  if (!surface_buffer(native, &buffer))
    return VK_ERROR_SURFACE_LOST_KHR;
  if (!wsi->sw || info->imageFormat != format.format ||
      info->imageColorSpace != format.colorSpace ||
      info->presentMode != VK_PRESENT_MODE_FIFO_KHR ||
      info->imageArrayLayers != 1)
    return VK_ERROR_FEATURE_NOT_PRESENT;
  if (info->imageExtent.width != native->client.extent.width ||
      info->imageExtent.height != native->client.extent.height)
    return VK_ERROR_OUT_OF_DATE_KHR;
  size_t count = info->minImageCount;
  if (count < 2 || count > (SIZE_MAX - sizeof(struct plant_swapchain)) /
                               sizeof(struct plant_image))
    return VK_ERROR_INITIALIZATION_FAILED;
  struct plant_swapchain *chain =
      vk_zalloc(allocator, sizeof(*chain) + count * sizeof(chain->images[0]), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
  if (!chain)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  if (mtx_init(&chain->mutex, mtx_plain) != thrd_success) {
    vk_free(allocator, chain);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  if (u_cnd_monotonic_init(&chain->available) != thrd_success) {
    mtx_destroy(&chain->mutex);
    vk_free(allocator, chain);
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
  struct wsi_cpu_image_params params = {.base.image_type = WSI_IMAGE_TYPE_CPU};
  VkResult result = wsi_swapchain_init(wsi, &chain->base, device, info,
                                       &params.base, allocator);
  if (result != VK_SUCCESS) {
    u_cnd_monotonic_destroy(&chain->available);
    mtx_destroy(&chain->mutex);
    vk_free(allocator, chain);
    return result;
  }
  chain->surface = *native;
  chain->base.destroy = swapchain_destroy;
  chain->base.get_wsi_image = swapchain_image;
  chain->base.acquire_next_image = swapchain_acquire;
  chain->base.release_images = swapchain_release;
  chain->base.queue_present = swapchain_present;
  chain->base.wait_for_present = swapchain_wait;
  chain->base.present_mode = VK_PRESENT_MODE_FIFO_KHR;
  chain->base.image_count = count;
  for (size_t i = 0; i < count; i++) {
    result = wsi_create_image(&chain->base, &chain->base.image_info,
                              &chain->images[i].base);
    if (result != VK_SUCCESS) {
      chain->base.image_count = i;
      swapchain_destroy(&chain->base, allocator);
      return result;
    }
  }
  *output = &chain->base;
  return VK_SUCCESS;
}

VkResult wsi_plantos_init_wsi(struct wsi_device *wsi,
                              const VkAllocationCallbacks *allocator) {
  struct wsi_interface *interface = vk_alloc(
      allocator, sizeof(*interface), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
  if (!interface)
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  *interface = (struct wsi_interface){
      .get_support = surface_support,
      .get_capabilities2 = surface_capabilities,
      .get_formats = surface_formats,
      .get_formats2 = surface_formats2,
      .get_present_modes = surface_modes,
      .get_present_rectangles = surface_rectangles,
      .create_swapchain = surface_swapchain,
  };
  wsi->wsi[VK_ICD_WSI_PLATFORM_PLANTOS] = interface;
  return VK_SUCCESS;
}

void wsi_plantos_finish_wsi(struct wsi_device *wsi,
                            const VkAllocationCallbacks *allocator) {
  vk_free(allocator, wsi->wsi[VK_ICD_WSI_PLATFORM_PLANTOS]);
  wsi->wsi[VK_ICD_WSI_PLATFORM_PLANTOS] = NULL;
}
