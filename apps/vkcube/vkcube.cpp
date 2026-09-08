#define VK_NO_PROTOTYPES
#include "cube_frag_spv.h"
#include "cube_vert_spv.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <plant_vulkan.h>
#include <rpc.h>
#include <syscall.h>
#include <task.h>
#include <vector>

// clang-format off
#define VULKAN_COMMANDS(X) \
  X(DestroyInstance) X(EnumeratePhysicalDevices) \
  X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
  X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceSurfaceSupportKHR) \
  X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) \
  X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) \
  X(DestroySurfaceKHR) X(CreateSwapchainKHR) X(DestroySwapchainKHR) \
  X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) \
  X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) \
  X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
  X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
  X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateShaderModule) X(DestroyShaderModule) \
  X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) \
  X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) \
  X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
  X(CmdBindPipeline) X(CmdPushConstants) X(CmdDraw) \
  X(CreateFence) X(DestroyFence) X(ResetFences) X(WaitForFences) X(QueueSubmit)
// clang-format on
#define VK_CHECK(expression)                                                   \
  do {                                                                         \
    VkResult result = (expression);                                            \
    if (result != VK_SUCCESS) {                                                \
      logkf("VKCUBE FAIL %s result=%d\n", #expression, result);                \
      return false;                                                            \
    }                                                                          \
  } while (0)

struct Cube {
  static constexpr uint32_t width = 640, height = 480;
  SDL_Window *window = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkImage depth = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView depth_view = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkShaderModule shaders[2] = {};
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  struct Frame {
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };
  std::vector<Frame> frames;
#define DECLARE(name) PFN_vk##name name = nullptr;
  VULKAN_COMMANDS(DECLARE)
#undef DECLARE

  bool initialize() {
    window = SDL_CreateWindow("Vulkan rotating cube", width, height,
                              SDL_WINDOW_VULKAN);
    if (!window)
      return false;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(
        vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create)
      return false;
    VkApplicationInfo application = {.sType =
                                         VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                     .pApplicationName = "Plant OS Vulkan cube",
                                     .apiVersion = VK_API_VERSION_1_1};
    const char *extension = VK_KHR_SURFACE_EXTENSION_NAME;
    VkInstanceCreateInfo info = {.sType =
                                     VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &application,
                                 .enabledExtensionCount = 1,
                                 .ppEnabledExtensionNames = &extension};
    VK_CHECK(create(&info, nullptr, &instance));
#define LOAD(name)                                                             \
  name = reinterpret_cast<PFN_vk##name>(                                       \
      vk_icdGetInstanceProcAddr(instance, "vk" #name));                        \
  if (!name)                                                                   \
    return false;
    VULKAN_COMMANDS(LOAD)
#undef LOAD
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
      return false;
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
    logkf("VKCUBE CONFIG device=%s cpus=%u workers=%s size=%ux%u\n",
          properties.deviceName, cpu_count(),
          getenv("LP_NUM_THREADS") ?: "auto", width, height);
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    for (; family < count; family++) {
      VkBool32 supported;
      VK_CHECK(GetPhysicalDeviceSurfaceSupportKHR(physical, family, surface,
                                                  &supported));
      if (supported && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        break;
    }
    if (family == count)
      return false;
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority};
    extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_info = {.sType =
                                          VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                      .queueCreateInfoCount = 1,
                                      .pQueueCreateInfos = &queue_info,
                                      .enabledExtensionCount = 1,
                                      .ppEnabledExtensionNames = &extension};
    VK_CHECK(CreateDevice(physical, &device_info, nullptr, &device));
    GetDeviceQueue(device, family, 0, &queue);
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(GetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface,
                                                     &capabilities));
    VK_CHECK(
        GetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(count);
    VK_CHECK(GetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                                formats.data()));
    auto format = std::find_if(formats.begin(), formats.end(), [](auto f) {
      return f.format == VK_FORMAT_B8G8R8A8_UNORM &&
             f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (format == formats.end() ||
        !(capabilities.supportedUsageFlags &
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        !(capabilities.supportedCompositeAlpha &
          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
      return false;
    uint32_t image_count = std::max(2u, capabilities.minImageCount);
    if (capabilities.maxImageCount)
      image_count = std::min(image_count, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = format->format,
        .imageColorSpace = format->colorSpace,
        .imageExtent = {width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE};
    VK_CHECK(CreateSwapchainKHR(device, &swap_info, nullptr, &swapchain));
    VK_CHECK(GetSwapchainImagesKHR(device, swapchain, &count, nullptr));
    std::vector<VkImage> images(count);
    VK_CHECK(GetSwapchainImagesKHR(device, swapchain, &count, images.data()));
    frames.resize(count);
    VkImageCreateInfo depth_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(CreateImage(device, &depth_info, nullptr, &depth));
    VkMemoryRequirements requirements;
    GetImageMemoryRequirements(device, depth, &requirements);
    VkPhysicalDeviceMemoryProperties memory_properties;
    GetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t type = 0;
    while (type < memory_properties.memoryTypeCount &&
           !(requirements.memoryTypeBits & (1u << type)))
      type++;
    if (type == memory_properties.memoryTypeCount)
      return false;
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type};
    VK_CHECK(AllocateMemory(device, &allocation, nullptr, &memory));
    VK_CHECK(BindImageMemory(device, depth, memory, 0));
    VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
    VK_CHECK(CreateImageView(device, &view, nullptr, &depth_view));
    VkAttachmentDescription attachments[] = {
        {.format = format->format,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        {.format = VK_FORMAT_D32_SFLOAT,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference color_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint =
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &color_ref,
                                    .pDepthStencilAttachment = &depth_ref};
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency};
    VK_CHECK(CreateRenderPass(device, &render_info, nullptr, &render_pass));
    for (uint32_t i = 0; i < count; i++) {
      view.image = images[i];
      view.format = format->format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      VK_CHECK(CreateImageView(device, &view, nullptr, &frames[i].view));
      VkImageView views[] = {frames[i].view, depth_view};
      VkFramebufferCreateInfo framebuffer = {
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .renderPass = render_pass,
          .attachmentCount = 2,
          .pAttachments = views,
          .width = width,
          .height = height,
          .layers = 1};
      VK_CHECK(CreateFramebuffer(device, &framebuffer, nullptr,
                                 &frames[i].framebuffer));
    }
    VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(cube_vert_spv),
        .pCode = cube_vert_spv};
    VK_CHECK(CreateShaderModule(device, &shader_info, nullptr, &shaders[0]));
    shader_info.codeSize = sizeof(cube_frag_spv);
    shader_info.pCode = cube_frag_spv;
    VK_CHECK(CreateShaderModule(device, &shader_info, nullptr, &shaders[1]));
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = shaders[0],
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = shaders[1],
         .pName = "main"}};
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0,
                                5 * sizeof(float)};
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push};
    VK_CHECK(CreatePipelineLayout(device, &layout_info, nullptr, &layout));
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {0, 0, width, height, 0, 1};
    VkRect2D scissor = {{0, 0}, {width, height}};
    VkPipelineViewportStateCreateInfo viewport_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState blend = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo blend_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend};
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster,
        .pMultisampleState = &samples,
        .pDepthStencilState = &depth_state,
        .pColorBlendState = &blend_info,
        .layout = layout,
        .renderPass = render_pass};
    VK_CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                     nullptr, &pipeline));
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = family};
    VK_CHECK(CreateCommandPool(device, &pool_info, nullptr, &pool));
    VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    VK_CHECK(AllocateCommandBuffers(device, &command_info, &command));
    VkFenceCreateInfo fence_info = {.sType =
                                        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(CreateFence(device, &fence_info, nullptr, &fence));
    return true;
  }

  bool draw(float angle, uint64_t &render_ns) {
    uint32_t index;
    VK_CHECK(AcquireNextImageKHR(device, swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                 fence, &index));
    VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(ResetFences(device, 1, &fence));
    uint64_t start = monotonic_ns();
    VK_CHECK(ResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(BeginCommandBuffer(command, &begin));
    VkClearValue clears[] = {{{16.f / 255, 24.f / 255, 40.f / 255, 1}},
                             {.depthStencil = {1, 0}}};
    VkRenderPassBeginInfo pass = {.sType =
                                      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = render_pass,
                                  .framebuffer = frames[index].framebuffer,
                                  .renderArea = {{0, 0}, {width, height}},
                                  .clearValueCount = 2,
                                  .pClearValues = clears};
    CmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    float rotation[] = {sinf(angle), cosf(angle), sinf(angle * .7f),
                        cosf(angle * .7f), float(width) / height};
    CmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(rotation), rotation);
    CmdDraw(command, 36, 1, 0, 0);
    CmdEndRenderPass(command);
    VK_CHECK(EndCommandBuffer(command));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &command};
    VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
    VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    render_ns = monotonic_ns() - start;
    VK_CHECK(ResetFences(device, 1, &fence));
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .swapchainCount = 1,
                                .pSwapchains = &swapchain,
                                .pImageIndices = &index};
    VK_CHECK(QueuePresentKHR(queue, &present));
    return true;
  }

  bool checkpoint(unsigned phase) {
    uint64_t ignored;
    if (!draw(phase ? 1.1f : .4f, ignored))
      return false;
    int x, y;
    SDL_GetWindowPosition(window, &x, &y);
    logkf("VKCUBE FRAME phase=%u x=%d y=%d\n", phase, x, y);
    uint64_t deadline = monotonic_ns() + 30000000000ull;
    SDL_Event event;
    while (monotonic_ns() < deadline) {
      if (SDL_WaitEventTimeout(&event, 100) &&
          event.type == SDL_EVENT_KEY_DOWN &&
          event.key.scancode == SDL_SCANCODE_SPACE)
        return true;
    }
    return false;
  }

  bool run(bool benchmark) {
    if (benchmark && !checkpoint(0))
      return false;
    constexpr unsigned warmup = 60, samples = 3600, rounds = 3;
    std::vector<uint64_t> times(samples);
    uint64_t origin = monotonic_ns(), total = 0, rendering = 0;
    for (unsigned frame = 0; !benchmark || frame < warmup + samples * rounds;
         frame++) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (event.type == SDL_EVENT_KEY_DOWN &&
             event.key.scancode == SDL_SCANCODE_ESCAPE))
          return !benchmark;
      }
      float angle =
          benchmark
              ? float(frame < warmup ? frame : (frame - warmup) % samples) *
                    .01745329252f
              : float(monotonic_ns() - origin) * 1e-9f;
      uint64_t start = monotonic_ns(), render;
      if (!draw(angle, render))
        return false;
      if (frame < warmup)
        continue;
      unsigned index = (frame - warmup) % samples;
      times[index] = monotonic_ns() - start;
      total += times[index];
      rendering += render;
      if (index + 1 != samples)
        continue;
      std::sort(times.begin(), times.end());
      char report[256];
      snprintf(report, sizeof(report),
               "VKCUBE BENCH round=%u frames=%u elapsed_ns=%llu render_ns=%llu "
               "fps=%.3f median_ms=%.3f p95_ms=%.3f",
               (frame - warmup) / samples, samples, (unsigned long long)total,
               (unsigned long long)rendering, samples * 1e9 / total,
               times[samples / 2] / 1e6, times[samples * 95 / 100] / 1e6);
      logkf("%s\n", report);
      printf("%s\n", report);
      char title[80];
      snprintf(title, sizeof(title), "Vulkan cube | %.1f FPS",
               samples * 1e9 / total);
      SDL_SetWindowTitle(window, title);
      total = rendering = 0;
    }
    return checkpoint(1);
  }

  ~Cube() {
    if (device) {
      DeviceWaitIdle(device);
      if (fence)
        DestroyFence(device, fence, nullptr);
      if (pool)
        DestroyCommandPool(device, pool, nullptr);
      if (pipeline)
        DestroyPipeline(device, pipeline, nullptr);
      if (layout)
        DestroyPipelineLayout(device, layout, nullptr);
      for (auto shader : shaders)
        if (shader)
          DestroyShaderModule(device, shader, nullptr);
      for (auto frame : frames) {
        if (frame.framebuffer)
          DestroyFramebuffer(device, frame.framebuffer, nullptr);
        if (frame.view)
          DestroyImageView(device, frame.view, nullptr);
      }
      if (render_pass)
        DestroyRenderPass(device, render_pass, nullptr);
      if (depth_view)
        DestroyImageView(device, depth_view, nullptr);
      if (depth)
        DestroyImage(device, depth, nullptr);
      if (memory)
        FreeMemory(device, memory, nullptr);
      if (swapchain)
        DestroySwapchainKHR(device, swapchain, nullptr);
      DestroyDevice(device, nullptr);
    }
    if (surface)
      DestroySurfaceKHR(instance, surface, nullptr);
    if (instance && DestroyInstance)
      DestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
  }
};

int main(int argc, char **argv) {
  bool benchmark = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--benchmark")) {
      benchmark = true;
    } else if (!strcmp(argv[i], "--workers") && i + 1 < argc) {
      char *end;
      const char *value = argv[++i];
      unsigned long workers = strtoul(value, &end, 10);
      if (!*value || *end || workers > INT_MAX ||
          setenv("LP_NUM_THREADS", value, 1))
        return 1;
    } else {
      printf("Usage: vkcube.bin [--benchmark] [--workers N]\n");
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
  bool success;
  {
    Cube cube;
    success = cube.initialize() && cube.run(benchmark);
  }
  SDL_Quit();
  logkf("VKCUBE %s\n", success ? "PASS" : "FAIL");
  return success ? 0 : 1;
}
