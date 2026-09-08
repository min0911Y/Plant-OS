#define VK_NO_PROTOTYPES
#include "compute_comp_spv.h"
#include "tea_comp_spv.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <plant_vulkan.h>
#include <rpc.h>
#include <syscall.h>
#include <task.h>
#include <thread>
#include <vector>

// clang-format off
#define VULKAN_COMMANDS(X) \
  X(DestroyInstance) \
  X(EnumeratePhysicalDevices) \
  X(GetPhysicalDeviceProperties) \
  X(GetPhysicalDeviceQueueFamilyProperties) \
  X(GetPhysicalDeviceMemoryProperties) \
  X(CreateDevice) \
  X(DestroyDevice) \
  X(GetDeviceQueue) \
  X(DeviceWaitIdle) \
  X(CreateBuffer) \
  X(DestroyBuffer) \
  X(GetBufferMemoryRequirements) \
  X(AllocateMemory) \
  X(FreeMemory) \
  X(BindBufferMemory) \
  X(MapMemory) \
  X(UnmapMemory) \
  X(CreateShaderModule) \
  X(DestroyShaderModule) \
  X(CreateDescriptorSetLayout) \
  X(DestroyDescriptorSetLayout) \
  X(CreatePipelineLayout) \
  X(DestroyPipelineLayout) \
  X(CreateComputePipelines) \
  X(DestroyPipeline) \
  X(CreateDescriptorPool) \
  X(DestroyDescriptorPool) \
  X(AllocateDescriptorSets) \
  X(UpdateDescriptorSets) \
  X(CreateCommandPool) \
  X(DestroyCommandPool) \
  X(AllocateCommandBuffers) \
  X(BeginCommandBuffer) \
  X(EndCommandBuffer) \
  X(CmdBindPipeline) \
  X(CmdBindDescriptorSets) \
  X(CmdDispatch) \
  X(CmdPipelineBarrier) \
  X(CreateFence) \
  X(DestroyFence) \
  X(QueueSubmit) \
  X(WaitForFences) \
  X(ResetFences) \
  X(CreateHeadlessSurfaceEXT) \
  X(DestroySurfaceKHR) \
  X(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
  X(CreateSwapchainKHR) \
  X(DestroySwapchainKHR) \
  X(GetSwapchainImagesKHR) \
  X(AcquireNextImageKHR) \
  X(QueuePresentKHR)
// clang-format on

#define VK_CHECK(expression)                                                   \
  do {                                                                         \
    VkResult result = (expression);                                            \
    if (result != VK_SUCCESS) {                                                \
      logkf("LVPTEST FAIL %s result=%d\n", #expression, result);               \
      return false;                                                            \
    }                                                                          \
  } while (0)

struct Vulkan {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = 0;
#define DECLARE(name) PFN_vk##name name = nullptr;
  VULKAN_COMMANDS(DECLARE)
#undef DECLARE

  bool initialize() {
    auto create = reinterpret_cast<PFN_vkCreateInstance>(
        vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create)
      return false;
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Plant OS lavapipe test",
        .apiVersion = VK_API_VERSION_1_1};
    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instance_extensions};
    VK_CHECK(create(&info, nullptr, &instance));
#define LOAD(name)                                                             \
  name = reinterpret_cast<PFN_vk##name>(                                       \
      vk_icdGetInstanceProcAddr(instance, "vk" #name));                        \
  if (!name) {                                                                 \
    logkf("LVPTEST missing vk%s\n", #name);                                    \
    return false;                                                              \
  }
    VULKAN_COMMANDS(LOAD)
#undef LOAD
    uint32_t count = 0;
    VK_CHECK(EnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count)
      return false;
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(EnumeratePhysicalDevices(instance, &count, devices.data()));
    physical = devices[0];
    VkPhysicalDeviceProperties properties;
    GetPhysicalDeviceProperties(physical, &properties);
    if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
      return false;
    logkf("LVPTEST device=%s\n", properties.deviceName);
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    while (family < count &&
           !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
      family++;
    if (family == count)
      return false;
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority};
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions};
    VK_CHECK(CreateDevice(physical, &device_info, nullptr, &device));
    GetDeviceQueue(device, family, 0, &queue);
    return queue != VK_NULL_HANDLE;
  }

  ~Vulkan() {
    if (device)
      DestroyDevice(device, nullptr);
    if (instance && DestroyInstance)
      DestroyInstance(instance, nullptr);
  }
};

struct Compute {
  Vulkan &vk;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkShaderModule shader = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptors = VK_NULL_HANDLE;
  VkCommandPool commands = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  void *mapped = nullptr;

  bool initialize(const uint32_t *code, size_t code_size, VkDeviceSize bytes,
                  uint32_t groups) {
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vk.CreateBuffer(vk.device, &buffer_info, nullptr, &buffer));
    VkMemoryRequirements requirements;
    vk.GetBufferMemoryRequirements(vk.device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vk.GetPhysicalDeviceMemoryProperties(vk.physical, &properties);
    uint32_t type = 0;
    constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (
        type < properties.memoryTypeCount &&
        (!(requirements.memoryTypeBits & (1u << type)) ||
         (properties.memoryTypes[type].propertyFlags & required) != required))
      type++;
    if (type == properties.memoryTypeCount)
      return false;
    VkMemoryAllocateInfo allocate = {.sType =
                                         VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = requirements.size,
                                     .memoryTypeIndex = type};
    VK_CHECK(vk.AllocateMemory(vk.device, &allocate, nullptr, &memory));
    VK_CHECK(vk.BindBufferMemory(vk.device, buffer, memory, 0));
    VK_CHECK(vk.MapMemory(vk.device, memory, 0, requirements.size, 0, &mapped));
    memset(mapped, 0, bytes);
    VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = code};
    VK_CHECK(vk.CreateShaderModule(vk.device, &shader_info, nullptr, &shader));
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding};
    VK_CHECK(vk.CreateDescriptorSetLayout(vk.device, &set_info, nullptr,
                                          &set_layout));
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout};
    VK_CHECK(vk.CreatePipelineLayout(vk.device, &layout_info, nullptr,
                                     &pipeline_layout));
    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = shader,
                  .pName = "main"},
        .layout = pipeline_layout};
    VK_CHECK(vk.CreateComputePipelines(vk.device, VK_NULL_HANDLE, 1,
                                       &pipeline_info, nullptr, &pipeline));
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size};
    VK_CHECK(
        vk.CreateDescriptorPool(vk.device, &pool_info, nullptr, &descriptors));
    VkDescriptorSetAllocateInfo set_allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptors,
        .descriptorSetCount = 1,
        .pSetLayouts = &set_layout};
    VkDescriptorSet set;
    VK_CHECK(vk.AllocateDescriptorSets(vk.device, &set_allocate, &set));
    VkDescriptorBufferInfo data = {.buffer = buffer, .range = buffer_info.size};
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &data};
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
    VkCommandPoolCreateInfo command_pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk.family};
    VK_CHECK(
        vk.CreateCommandPool(vk.device, &command_pool, nullptr, &commands));
    VkCommandBufferAllocateInfo command_allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commands,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VK_CHECK(vk.AllocateCommandBuffers(vk.device, &command_allocate, &command));
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vk.BeginCommandBuffer(command, &begin));
    vk.CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vk.CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                             pipeline_layout, 0, 1, &set, 0, nullptr);
    vk.CmdDispatch(command, groups, 1, 1);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vk.CmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                          nullptr, 0, nullptr);
    VK_CHECK(vk.EndCommandBuffer(command));
    VkFenceCreateInfo fence_info = {.sType =
                                        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vk.CreateFence(vk.device, &fence_info, nullptr, &fence));
    return true;
  }

  bool dispatch() {
    VK_CHECK(vk.ResetFences(vk.device, 1, &fence));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &command};
    VK_CHECK(vk.QueueSubmit(vk.queue, 1, &submit, fence));
    VK_CHECK(vk.WaitForFences(vk.device, 1, &fence, true, UINT64_MAX));
    return true;
  }

  bool run() {
    constexpr uint32_t elements = 64;
    if (!initialize(compute_comp_spv, sizeof(compute_comp_spv),
                    elements * sizeof(uint32_t), elements / 4) ||
        !dispatch())
      return false;
    const uint32_t *values = static_cast<const uint32_t *>(mapped);
    for (uint32_t i = 0; i < elements; i++) {
      if (values[i] != i * 3 + 7) {
        logkf("LVPTEST compute index=%u actual=%u\n", i, values[i]);
        return false;
      }
    }
    logkf("LVPCOMPUTE PASS elements=%u\n", elements);
    return true;
  }

  bool benchmark() {
    constexpr uint32_t blocks = 16 * 1024 * 1024;
    constexpr unsigned warmup = 3, rounds = 5, batches = 4;
    constexpr size_t bytes = size_t(blocks) * 2 * sizeof(uint32_t);
    if (!initialize(tea_comp_spv, sizeof(tea_comp_spv), bytes, blocks / 512))
      return false;
    std::vector<uint32_t> reference(size_t(blocks) * 2);
    for (uint32_t index = 0; index < blocks; index++) {
      uint32_t left = index, right = 0, sum = 0;
      for (unsigned round = 0; round < 32; round++) {
        sum += 0x9e3779b9u;
        left += (right << 4) ^ (right + sum) ^ (right >> 5);
        right += (left << 4) ^ (left + sum) ^ (left >> 5);
      }
      reference[2 * index] = left;
      reference[2 * index + 1] = right;
    }
    // Published TEA zero-key, zero-plaintext test vector anchors the reference.
    if (reference[0] != 0x41ea3a0a || reference[1] != 0x94baa940)
      return false;
    const char *workers = getenv("LP_NUM_THREADS");
    logkf("LVPSCALE CONFIG workload=tea32 blocks=%u bytes=%llu local_size=512 "
          "warmup=%u rounds=%u batches=%u cpus=%u workers=%s\n",
          blocks, (unsigned long long)bytes, warmup, rounds, batches,
          cpu_count(), workers ? workers : "default");
    for (unsigned i = 0; i < warmup; i++) {
      if (!dispatch())
        return false;
    }
    for (unsigned round = 0; round < rounds; round++) {
      uint64_t elapsed = 0;
      for (unsigned batch = 0; batch < batches; batch++) {
        memset(mapped, 0, bytes);
        uint64_t start = monotonic_ns();
        if (!dispatch())
          return false;
        elapsed += monotonic_ns() - start;
        if (memcmp(mapped, reference.data(), bytes)) {
          logkf("LVPSCALE FAIL output round=%u batch=%u\n", round, batch);
          return false;
        }
      }
      logkf("LVPSCALE SAMPLE round=%u elapsed_ns=%llu\n", round,
            (unsigned long long)elapsed);
    }
    logkf("LVPSCALE PASS workers=%s verified_blocks=%llu\n",
          workers ? workers : "default",
          (unsigned long long)blocks * rounds * batches);
    return true;
  }

  ~Compute() {
    vk.DeviceWaitIdle(vk.device);
    if (fence)
      vk.DestroyFence(vk.device, fence, nullptr);
    if (commands)
      vk.DestroyCommandPool(vk.device, commands, nullptr);
    if (descriptors)
      vk.DestroyDescriptorPool(vk.device, descriptors, nullptr);
    if (pipeline)
      vk.DestroyPipeline(vk.device, pipeline, nullptr);
    if (pipeline_layout)
      vk.DestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
    if (set_layout)
      vk.DestroyDescriptorSetLayout(vk.device, set_layout, nullptr);
    if (shader)
      vk.DestroyShaderModule(vk.device, shader, nullptr);
    if (mapped)
      vk.UnmapMemory(vk.device, memory);
    if (buffer)
      vk.DestroyBuffer(vk.device, buffer, nullptr);
    if (memory)
      vk.FreeMemory(vk.device, memory, nullptr);
  }
};

struct Headless {
  Vulkan &vk;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkCommandPool commands = VK_NULL_HANDLE;
  VkFence acquired = VK_NULL_HANDLE, submitted = VK_NULL_HANDLE;

  bool run() {
    VkHeadlessSurfaceCreateInfoEXT surface_info = {
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    VK_CHECK(vk.CreateHeadlessSurfaceEXT(vk.instance, &surface_info, nullptr,
                                         &surface));
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, surface,
                                                        &capabilities));
    VkSwapchainCreateInfoKHR create = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = capabilities.minImageCount,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {32, 32},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR};
    VK_CHECK(vk.CreateSwapchainKHR(vk.device, &create, nullptr, &swapchain));
    uint32_t count = 0;
    VK_CHECK(vk.GetSwapchainImagesKHR(vk.device, swapchain, &count, nullptr));
    std::vector<VkImage> images(count);
    VK_CHECK(
        vk.GetSwapchainImagesKHR(vk.device, swapchain, &count, images.data()));
    std::vector<uint32_t> indices(count);
    VkFenceCreateInfo fence_info = {.sType =
                                        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vk.CreateFence(vk.device, &fence_info, nullptr, &acquired));
    VK_CHECK(vk.CreateFence(vk.device, &fence_info, nullptr, &submitted));
    for (uint32_t i = 0; i < count; i++) {
      VK_CHECK(vk.AcquireNextImageKHR(vk.device, swapchain, 0, VK_NULL_HANDLE,
                                      acquired, &indices[i]));
      VK_CHECK(vk.WaitForFences(vk.device, 1, &acquired, true, UINT64_MAX));
      VK_CHECK(vk.ResetFences(vk.device, 1, &acquired));
    }
    uint32_t index = UINT32_MAX;
    if (vk.AcquireNextImageKHR(vk.device, swapchain, 0, VK_NULL_HANDLE,
                               acquired, &index) != VK_NOT_READY)
      return false;
    VkResult waited = VK_SUCCESS;
    std::thread waiter([&] {
      waited = vk.AcquireNextImageKHR(vk.device, swapchain, 50000000,
                                      VK_NULL_HANDLE, acquired, &index);
    });
    waiter.join();
    if (waited != VK_TIMEOUT)
      return false;
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk.family};
    VK_CHECK(vk.CreateCommandPool(vk.device, &pool_info, nullptr, &commands));
    VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commands,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VkCommandBuffer command;
    VK_CHECK(vk.AllocateCommandBuffers(vk.device, &allocate, &command));
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vk.BeginCommandBuffer(command, &begin));
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = images[indices[0]],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vk.CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier);
    VK_CHECK(vk.EndCommandBuffer(command));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &command};
    VK_CHECK(vk.QueueSubmit(vk.queue, 1, &submit, submitted));
    VK_CHECK(vk.WaitForFences(vk.device, 1, &submitted, true, UINT64_MAX));
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .swapchainCount = 1,
                                .pSwapchains = &swapchain,
                                .pImageIndices = indices.data()};
    VK_CHECK(vk.QueuePresentKHR(vk.queue, &present));
    VK_CHECK(vk.AcquireNextImageKHR(vk.device, swapchain, 0, VK_NULL_HANDLE,
                                    acquired, &index));
    if (index != indices[0])
      return false;
    logkf("LVPHEADLESS PASS images=%u\n", count);
    return true;
  }

  ~Headless() {
    vk.DeviceWaitIdle(vk.device);
    if (commands)
      vk.DestroyCommandPool(vk.device, commands, nullptr);
    if (submitted)
      vk.DestroyFence(vk.device, submitted, nullptr);
    if (acquired)
      vk.DestroyFence(vk.device, acquired, nullptr);
    if (swapchain)
      vk.DestroySwapchainKHR(vk.device, swapchain, nullptr);
    if (surface)
      vk.DestroySurfaceKHR(vk.instance, surface, nullptr);
  }
};

static bool draw(SDL_Renderer *renderer, unsigned phase) {
  const SDL_Vertex triangle[] = {
      {{40, 200}, {1, 0, 0, 1}, {0, 0}},
      {{280, 200}, {0, 1, 0, 1}, {0, 0}},
      {{160, 40}, {0, 0, 1, 1}, {0, 0}},
  };
  if (!SDL_SetRenderDrawColor(renderer, phase ? 48 : 16, 32, phase ? 16 : 48,
                              255) ||
      !SDL_RenderClear(renderer) ||
      !SDL_RenderGeometry(renderer, nullptr, triangle, 3, nullptr, 0))
    return false;
  SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
  if (!pixels)
    return false;
  const SDL_Point points[] = {
      {8, 8}, {65, 190}, {255, 190}, {160, 70}, {160, 140}};
  bool valid = true;
  for (unsigned i = 0; i < SDL_arraysize(points); i++) {
    Uint8 r, g, b, a;
    valid &=
        SDL_ReadSurfacePixel(pixels, points[i].x, points[i].y, &r, &g, &b, &a);
    if (i == 0)
      valid &= r == (phase ? 48 : 16) && g == 32 && b == (phase ? 16 : 48);
    if (i == 1)
      valid &= r > 180 && g < 60 && b < 60;
    if (i == 2)
      valid &= g > 180 && r < 60 && b < 60;
    if (i == 3)
      valid &= b > 180 && r < 60 && g < 60;
    if (i == 4)
      valid &= r > 50 && g > 50 && b > 50;
  }
  SDL_DestroySurface(pixels);
  return valid && SDL_RenderPresent(renderer);
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "--benchmark")) {
    if (argc != 2) {
      char *end;
      const char *value = argc == 4 ? argv[3] : "";
      unsigned long workers = strtoul(value, &end, 10);
      if (argc != 4 || strcmp(argv[2], "--workers") || !*value || *end ||
          workers > INT_MAX || setenv("LP_NUM_THREADS", value, 1)) {
        printf("Usage: lvptest.bin --benchmark [--workers N]\n");
        return 1;
      }
    }
    Vulkan vk;
    if (!vk.initialize() || !Compute{vk}.benchmark()) {
      logkf("LVPSCALE FAIL initialization or execution\n");
      return 1;
    }
    return 0;
  }
  bool test = argc == 2 && !strcmp(argv[1], "--test");
  {
    Vulkan vk;
    if (!vk.initialize() || !Compute{vk}.run() || !Headless{vk}.run()) {
      logkf("LVPTEST FAIL Vulkan initialization or execution\n");
      return 1;
    }
  }
  rpc_endpoint_t gui;
  if (rpc_connect(GUI_SERVICE_NAME, &gui, 0) != RPC_OK) {
    int child = fork();
    if (child < 0)
      return 1;
    if (!child) {
      char program[] = "gui.bin";
      return exec(program, program);
    }
    if (rpc_connect(GUI_SERVICE_NAME, &gui, 30000) != RPC_OK)
      return 1;
  }
  sleep(1000);
  if (!SDL_Init(SDL_INIT_VIDEO))
    return 1;
  if (!SDL_Vulkan_LoadLibrary("/lib/liblvp.so")) {
    logkf("LVPTEST FAIL explicit Vulkan load: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  if (!SDL_Vulkan_LoadLibrary("/lib/liblvp.so")) {
    logkf("LVPTEST FAIL repeated Vulkan load: %s\n", SDL_GetError());
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
    return 1;
  }
  SDL_Vulkan_UnloadLibrary();
  SDL_Vulkan_UnloadLibrary();
  SDL_Window *window =
      SDL_CreateWindow("lavapipe", 320, 240, SDL_WINDOW_VULKAN);
  SDL_Renderer *renderer =
      window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  int status = 1;
  if (renderer && !strcmp(SDL_GetRendererName(renderer), "vulkan")) {
    unsigned phase = 0;
    while (phase < 2) {
      if (!draw(renderer, phase))
        break;
      int x, y;
      SDL_GetWindowPosition(window, &x, &y);
      logkf("LVPFRAME READY phase=%u x=%d y=%d\n", phase, x, y);
      Uint64 deadline = test ? SDL_GetTicks() + 30000 : UINT64_MAX;
      SDL_Event event;
      bool next = false;
      while (SDL_GetTicks() < deadline && !next) {
        if (!SDL_WaitEventTimeout(&event, 100))
          continue;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (event.type == SDL_EVENT_KEY_DOWN &&
             event.key.scancode == SDL_SCANCODE_ESCAPE)) {
          status = test ? 1 : 0;
          phase = 2;
          break;
        }
        next = event.type == SDL_EVENT_KEY_DOWN &&
               event.key.scancode == SDL_SCANCODE_SPACE;
      }
      if (phase == 2 || !next)
        break;
      if (++phase == 2)
        status = 0;
    }
  }
  if (status)
    logkf("LVPTEST FAIL graphics: %s\n", SDL_GetError());
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (!status)
    logkf("LVPTEST PASS compute, triangle, swapchain and keyboard\n");
  return status;
}
