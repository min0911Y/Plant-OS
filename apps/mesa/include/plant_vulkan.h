#ifndef PLANT_VULKAN_H
#define PLANT_VULKAN_H

#include <gui.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Native single-ICD entry. All Vulkan commands retain their standard ABI. */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

/* The instance must enable VK_KHR_surface. This native window adapter does
 * not claim a Khronos extension number. Destroy the surface before the window.
 * Rectangle coordinates describe the client area in the GUI drawing buffer. */
__attribute__((visibility("default"))) VkResult plant_vulkan_create_surface(
    VkInstance instance, window_t window, const VkRect2D *client,
    const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface);
#ifdef __cplusplus
}
#endif
#endif
