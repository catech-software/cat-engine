#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <format>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

#include "demo/log.h"

constexpr int width  = 800;
constexpr int height = 600;

static std::vector<const char *> layers    = {};
static std::vector<const char *> inst_exts = {};
static std::vector<const char *> dev_exts  = {
  vk::KHRSwapchainExtensionName
};

// validation layers are only enabled by default on debug builds
#ifdef NDEBUG
constexpr bool enable_validation_layers = false;
#else
constexpr bool enable_validation_layers = true;
#endif

static std::array shader = std::to_array<unsigned char>({
#embed "demo/shaders/default.spv"
});

class demo {
public:
  void run() {
    init();
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      draw();
    }
    cleanup();
  }

private:
  GLFWwindow* window;

  vk::raii::Context                context;
  vk::raii::Instance               instance              = nullptr;
  vk::raii::DebugUtilsMessengerEXT debug_messenger       = nullptr;
  vk::raii::SurfaceKHR             surface               = nullptr;
  vk::raii::PhysicalDevice         physical_device       = nullptr;
  vk::raii::Device                 device                = nullptr;
  std::uint32_t                    queue_family_index    = std::numeric_limits<std::uint32_t>::max();
  vk::raii::Queue                  queue                 = nullptr;
  vk::raii::SwapchainKHR           swapchain             = nullptr;
  std::vector<vk::Image>           swapchain_images;
  vk::SurfaceFormatKHR             swapchain_format;
  vk::Extent2D                     swapchain_extent;
  std::vector<vk::raii::ImageView> swapchain_image_views;
  vk::raii::PipelineLayout         pipeline_layout       = nullptr;
  vk::raii::Pipeline               pipeline              = nullptr;
  vk::raii::CommandPool            command_pool          = nullptr;
  vk::raii::CommandBuffer          command_buffer        = nullptr;

  vk::raii::Semaphore present_semaphore = nullptr;
  vk::raii::Semaphore render_semaphore  = nullptr;
  vk::raii::Fence     draw_fence        = nullptr;

  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                         vk::DebugUtilsMessageTypeFlagsEXT type,
                                                         const vk::DebugUtilsMessengerCallbackDataEXT *data,
                                                         void *) {
    enum severity log_severity;
    switch (severity) {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
      log_severity = severity::debug;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
      log_severity = severity::info;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
      log_severity = severity::warn;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
      log_severity = severity::error;
      break;
    default:
      std::unreachable();
    }
    log(log_severity, std::format("Vulkan validation layers: {} {}", vk::to_string(type), data->pMessage));
    return vk::False;
  }

  void create_vulkan_instance() {
    constexpr vk::ApplicationInfo app_info = {
      .pApplicationName   = "CAT Engine Demo",
      .applicationVersion = VK_MAKE_VERSION(0, 0, 0),
      .pEngineName        = "CAT Engine",
      .engineVersion      = VK_MAKE_VERSION(0, 0, 0),
      .apiVersion         = vk::ApiVersion13
    };

    if (enable_validation_layers) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    std::vector<vk::LayerProperties> layer_props = context.enumerateInstanceLayerProperties();
    decltype(layers)::iterator unsupported_layer = std::ranges::find_if(layers, [&layer_props](const char *layer) -> bool {
      return std::ranges::none_of(layer_props, [layer](const vk::LayerProperties& layer_prop) -> bool {
        return std::strcmp(layer, layer_prop.layerName) == 0;
      });
    });
    if (unsupported_layer != layers.end())
      throw std::runtime_error(std::format("Required layer not supported: {}", *unsupported_layer));

    std::uint32_t glfw_num_exts;
    const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_num_exts);
    inst_exts.insert(inst_exts.end(), glfw_exts, glfw_exts + glfw_num_exts);
    if (enable_validation_layers) {
      inst_exts.push_back(vk::EXTDebugUtilsExtensionName);
    }
    std::vector<vk::ExtensionProperties> ext_props = context.enumerateInstanceExtensionProperties();
    decltype(inst_exts)::iterator unsupported_ext = std::ranges::find_if(inst_exts, [&ext_props](const char *ext) -> bool {
      return std::ranges::none_of(ext_props, [ext](const vk::ExtensionProperties& ext_prop) -> bool {
        return std::strcmp(ext, ext_prop.extensionName);
      });
    });
    if (unsupported_ext != inst_exts.end())
      throw std::runtime_error(std::format("Requied extension not supported: {}", *unsupported_ext));

    vk::InstanceCreateInfo create_info = {
      .pApplicationInfo        = &app_info,
      .enabledLayerCount       = static_cast<std::uint32_t>(layers.size()),
      .ppEnabledLayerNames     = layers.data(),
      .enabledExtensionCount   = static_cast<std::uint32_t>(inst_exts.size()),
      .ppEnabledExtensionNames = inst_exts.data()
    };

    if (enable_validation_layers) {
      vk::DebugUtilsMessageSeverityFlagsEXT severities;
      switch (log_level) {
      case severity::trace:
      case severity::debug:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;
        [[fallthrough]];
      case severity::info:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
        [[fallthrough]];
      case severity::warn:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
        [[fallthrough]];
      case severity::error:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        [[fallthrough]];
      case severity::fatal:
        break;
      }
      vk::DebugUtilsMessageTypeFlagsEXT types
        = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
        | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
        | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
      vk::DebugUtilsMessengerCreateInfoEXT messenger_create_info = {
        .messageSeverity = severities,
        .messageType     = types,
        .pfnUserCallback = &debug_callback
      };
      create_info.pNext = &messenger_create_info;
      instance = vk::raii::Instance(context, create_info);
      debug_messenger = instance.createDebugUtilsMessengerEXT(messenger_create_info);
    } else {
      instance = vk::raii::Instance(context, create_info);
    } 
  }

  void create_surface() {
    VkSurfaceKHR csurface;
    if (static_cast<vk::Result>(glfwCreateWindowSurface(*instance, window, nullptr, &csurface)) != vk::Result::eSuccess)
      throw std::runtime_error("Failed to create a window surface");
    surface = vk::raii::SurfaceKHR(instance, csurface);
  }

  void select_physical_device() {
    std::vector<vk::raii::PhysicalDevice> physical_devices = instance.enumeratePhysicalDevices();
    decltype(physical_devices)::iterator first_device
      = std::ranges::find_if(physical_devices, [this](const vk::raii::PhysicalDevice& device) -> bool {
        if (device.getProperties().apiVersion < vk::ApiVersion13) return false;

        if (std::ranges::none_of(device.getQueueFamilyProperties() | std::views::enumerate,
          [this, &device](std::tuple<std::size_t, vk::QueueFamilyProperties>&& qfp_tuple) -> bool {
            auto [ qfp_index, qfp ] = qfp_tuple;
            return qfp.queueFlags & vk::QueueFlagBits::eGraphics && device.getSurfaceSupportKHR(qfp_index, surface);
          })) return false;

        std::vector<vk::ExtensionProperties> ext_props = device.enumerateDeviceExtensionProperties();
        if (!std::ranges::all_of(dev_exts, [&ext_props](const char *ext) -> bool {
          return std::ranges::any_of(ext_props, [ext](const vk::ExtensionProperties& ext_prop) -> bool {
            return std::strcmp(ext, ext_prop.extensionName) == 0;
          });
        })) return false;

        vk::StructureChain<vk::PhysicalDeviceFeatures2,
                           vk::PhysicalDeviceVulkan11Features,
                           vk::PhysicalDeviceVulkan13Features,
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> features
          = device.getFeatures2<vk::PhysicalDeviceFeatures2,
                                vk::PhysicalDeviceVulkan11Features,
                                vk::PhysicalDeviceVulkan13Features,
                                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        return features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters
            && features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2
            && features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
            && features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;
      });
    if (first_device == physical_devices.end()) throw std::runtime_error("Failed to find a device with Vulkan support");
    physical_device = *first_device;
  }

  void create_logical_device() {
    std::vector<vk::QueueFamilyProperties> queue_family_props = physical_device.getQueueFamilyProperties();
    for (auto [ qfp_index, qfp ] : queue_family_props | std::views::enumerate) {
      if (qfp.queueFlags & vk::QueueFlagBits::eGraphics && physical_device.getSurfaceSupportKHR(qfp_index, *surface)) {
        queue_family_index = qfp_index;
        break;
      }
    }
    assert(queue_family_index != std::numeric_limits<std::uint32_t>::max()
        && "Queue family doesn't support graphics even though it should");
    float queue_priority = 0.5f;
    vk::DeviceQueueCreateInfo queue_create_info = {
      .queueFamilyIndex = queue_family_index,
      .queueCount       = 1,
      .pQueuePriorities = &queue_priority
    };

    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> features = {
      {},
      {
        .shaderDrawParameters = true
      },
      {
        .synchronization2 = true,
        .dynamicRendering = true
      },
      {
        .extendedDynamicState = true
      }
    };

    vk::DeviceCreateInfo create_info = {
      .pNext                   = &features.get<vk::PhysicalDeviceFeatures2>(),
      .queueCreateInfoCount    = 1,
      .pQueueCreateInfos       = &queue_create_info,
      .enabledExtensionCount   = static_cast<std::uint32_t>(dev_exts.size()),
      .ppEnabledExtensionNames = dev_exts.data()
    };
    device = vk::raii::Device(physical_device, create_info);
    queue  = vk::raii::Queue(device, queue_family_index, 0);
  }

  void create_swapchain() {
    std::vector<vk::SurfaceFormatKHR> formats = physical_device.getSurfaceFormatsKHR(*surface);
    assert(!formats.empty() && "No surface formats supported");
    decltype(formats)::iterator preferred_format = std::ranges::find_if(formats, [](const vk::SurfaceFormatKHR& format) -> bool {
      return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    swapchain_format = preferred_format != formats.end() ? *preferred_format : formats[0];

    std::vector<vk::PresentModeKHR> present_modes = physical_device.getSurfacePresentModesKHR(*surface);
    assert(std::ranges::any_of(present_modes, [](vk::PresentModeKHR present_mode) -> bool {
      return present_mode == vk::PresentModeKHR::eFifo;
    }) && "FIFO present mode isn't support even though it should be");
    vk::PresentModeKHR present_mode = std::ranges::any_of(present_modes, [](vk::PresentModeKHR present_mode) -> bool {
      return present_mode == vk::PresentModeKHR::eMailbox;
    }) ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;

    vk::SurfaceCapabilitiesKHR capabilities = physical_device.getSurfaceCapabilitiesKHR(*surface);
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()
     || capabilities.currentExtent.height != std::numeric_limits<std::uint32_t>::max()) {
      swapchain_extent = capabilities.currentExtent;
    } else {
      int width, height;
      glfwGetFramebufferSize(window, &width, &height);
      swapchain_extent = vk::Extent2D{
        .width  = std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        .height = std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
      };
    }

    // prefer triple buffering
    std::uint32_t min_image_count = capabilities.maxImageCount == 0
                                  ? std::max(3u, capabilities.minImageCount)
                                  : std::clamp(3u, capabilities.minImageCount, capabilities.maxImageCount);

    vk::SwapchainCreateInfoKHR create_info = {
      .surface          = *surface,
      .minImageCount    = min_image_count,
      .imageFormat      = swapchain_format.format,
      .imageColorSpace  = swapchain_format.colorSpace,
      .imageExtent      = swapchain_extent,
      .imageArrayLayers = 1,
      .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
      .imageSharingMode = vk::SharingMode::eExclusive,
      .preTransform     = capabilities.currentTransform,
      .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode      = present_mode,
      .clipped          = true
    };
    swapchain = vk::raii::SwapchainKHR(device, create_info);
    swapchain_images = swapchain.getImages();
  }

  void create_image_views() {
    vk::ImageViewCreateInfo create_info = {
      .viewType         = vk::ImageViewType::e2D,
      .format           = swapchain_format.format,
      .subresourceRange = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1
      }
    };

    for (vk::Image& image : swapchain_images) {
      create_info.image = image;
      swapchain_image_views.emplace_back(device, create_info);
    }
  }

  [[nodiscard]]
  vk::raii::ShaderModule create_shader_module(const std::uint32_t *code, std::size_t size) const {
    vk::ShaderModuleCreateInfo create_info = {
      .codeSize = size,
      .pCode    = code
    };
    return vk::raii::ShaderModule(device, create_info);
  }

  void create_graphics_pipeline() {
    vk::raii::ShaderModule module
      = create_shader_module(reinterpret_cast<std::uint32_t *>(shader.data()), shader.size() * sizeof(unsigned char));
    
    std::array shader_stages = std::to_array<vk::PipelineShaderStageCreateInfo>({
      {
        .stage  = vk::ShaderStageFlagBits::eVertex,
        .module = module,
        .pName  = "main"
      },
      {
        .stage  = vk::ShaderStageFlagBits::eFragment,
        .module = module,
        .pName  = "main"
      }
    });

    vk::PipelineVertexInputStateCreateInfo vertex_input_state;

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state = {
      .topology = vk::PrimitiveTopology::eTriangleList
    };

    vk::PipelineViewportStateCreateInfo viewport_state = {
      .viewportCount = 1,
      .scissorCount  = 1
    };

    vk::PipelineRasterizationStateCreateInfo rasterization_state = {
      .depthClampEnable        = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode             = vk::PolygonMode::eFill,
      .cullMode                = vk::CullModeFlagBits::eBack,
      .frontFace               = vk::FrontFace::eCounterClockwise,
      .depthBiasEnable         = vk::False,
      .lineWidth               = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisample_state = {
      .rasterizationSamples = vk::SampleCountFlagBits::e1,
      .sampleShadingEnable  = vk::False
    };

    vk::PipelineColorBlendAttachmentState color_blend_attachment = {
      .blendEnable         = vk::True,
      .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
      .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
      .colorBlendOp        = vk::BlendOp::eAdd,
      .srcAlphaBlendFactor = vk::BlendFactor::eOne,
      .dstAlphaBlendFactor = vk::BlendFactor::eZero,
      .alphaBlendOp        = vk::BlendOp::eAdd,
      .colorWriteMask      = vk::ColorComponentFlagBits::eR
                           | vk::ColorComponentFlagBits::eG
                           | vk::ColorComponentFlagBits::eB
                           | vk::ColorComponentFlagBits::eA
    };
    vk::PipelineColorBlendStateCreateInfo color_blend_state = {
      .logicOpEnable   = vk::False,
      .logicOp         = vk::LogicOp::eCopy,
      .attachmentCount = 1,
      .pAttachments    = &color_blend_attachment
    };

    std::array dynamic_states = std::to_array<vk::DynamicState>({
      vk::DynamicState::eViewport,
      vk::DynamicState::eScissor
    });
    vk::PipelineDynamicStateCreateInfo dynamic_state = {
      .dynamicStateCount = dynamic_states.size(),
      .pDynamicStates    = dynamic_states.data()
    };

    vk::PipelineLayoutCreateInfo layout_create_info = {
      .setLayoutCount         = 0,
      .pushConstantRangeCount = 0
    };
    pipeline_layout = vk::raii::PipelineLayout(device, layout_create_info);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> create_info = {
      {
        .stageCount          = shader_stages.size(),
        .pStages             = shader_stages.data(),
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = &dynamic_state,
        .layout              = pipeline_layout,
        .renderPass          = nullptr
      },
      {
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format.format
      }
    };
    pipeline = vk::raii::Pipeline(device, nullptr, create_info.get<vk::GraphicsPipelineCreateInfo>());
  }

  void create_command_pool() {
    vk::CommandPoolCreateInfo create_info = {
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = queue_family_index
    };
    command_pool = vk::raii::CommandPool(device, create_info);
  }

  void create_command_buffer() {
    vk::CommandBufferAllocateInfo alloc_info = {
      .commandPool        = command_pool,
      .level              = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = 1
    };
    command_buffer = std::move(vk::raii::CommandBuffers(device, alloc_info).front());
  }

  void create_sync_objects() {
    present_semaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{});
    render_semaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{});
    draw_fence = vk::raii::Fence(device, { .flags = vk::FenceCreateFlagBits::eSignaled });
  }

  void transition_image_layout(
    std::uint32_t           image_index,
    vk::ImageLayout         old_layout,
    vk::ImageLayout         new_layout,
    vk::AccessFlags2        src_access_mask,
    vk::AccessFlags2        dst_access_mask,
    vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask
  ) {
    vk::ImageMemoryBarrier2 barrier = {
      .srcStageMask        = src_stage_mask,
      .srcAccessMask       = src_access_mask,
      .dstStageMask        = dst_stage_mask,
      .dstAccessMask       = dst_access_mask,
      .oldLayout           = old_layout,
      .newLayout           = new_layout,
      .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
      .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
      .image               = swapchain_images[image_index],
      .subresourceRange    = {
        .aspectMask     = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1
      }
    };
    vk::DependencyInfo dependency_info = {
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers    = &barrier
    };
    command_buffer.pipelineBarrier2(dependency_info);
  }

  void record_command_buffer(std::uint32_t image_index) {
    command_buffer.begin({});

    transition_image_layout(
      image_index,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal,
      {},
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    vk::ClearValue clear_color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    vk::RenderingAttachmentInfo attachment_info = {
      .imageView   = swapchain_image_views[image_index],
      .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp      = vk::AttachmentLoadOp::eClear,
      .storeOp     = vk::AttachmentStoreOp::eStore,
      .clearValue  = clear_color
    };

    vk::RenderingInfo rendering_info = {
      .renderArea           = {
        .offset = { 0, 0 },
        .extent = swapchain_extent
      },
      .layerCount           = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments    = &attachment_info
    };
    command_buffer.beginRendering(rendering_info);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    command_buffer.setViewport(0, vk::Viewport{
      .x        = 0.0f,
      .y        = 0.0f,
      .width    = static_cast<float>(swapchain_extent.width),
      .height   = static_cast<float>(swapchain_extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f
    });
    command_buffer.setScissor(0, vk::Rect2D{
      .offset = { 0, 0 },
      .extent = swapchain_extent
    });
    command_buffer.draw(6, 1, 0, 0);
    command_buffer.endRendering();

    transition_image_layout(
      image_index,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      {},
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::PipelineStageFlagBits2::eBottomOfPipe
    );

    command_buffer.end();
  }

  void init() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, "CAT Engine Demo", nullptr, nullptr);

    create_vulkan_instance();
    create_surface();
    select_physical_device();
    create_logical_device();
    create_swapchain();
    create_image_views();
    create_graphics_pipeline();
    create_command_pool();
    create_command_buffer();
    create_sync_objects();
  }

  void draw() {
    vk::Result fence_result = device.waitForFences(*draw_fence, vk::True, std::numeric_limits<std::uint64_t>::max());
    if (fence_result != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for device to finish drawing");
    device.resetFences(*draw_fence);

    auto [image_result, image_index]
      = swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), present_semaphore, nullptr);
    record_command_buffer(image_index);
    queue.waitIdle();

    vk::SemaphoreSubmitInfo wait_semaphore_submit_info = {
      .semaphore = present_semaphore,
      .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput
    };
    vk::CommandBufferSubmitInfo command_buffer_submit_info = {
      .commandBuffer = command_buffer
    };
    vk::SemaphoreSubmitInfo signal_semaphore_submit_info = {
      .semaphore = render_semaphore,
      // TODO: figure out if there is a more appropriate setting for this
      .stageMask = vk::PipelineStageFlagBits2::eAllGraphics
    };
    vk::SubmitInfo2 submit_info = {
      .waitSemaphoreInfoCount   = 1,
      .pWaitSemaphoreInfos      = &wait_semaphore_submit_info,
      .commandBufferInfoCount   = 1,
      .pCommandBufferInfos      = &command_buffer_submit_info,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos    = &signal_semaphore_submit_info
    };
    queue.submit2(submit_info, draw_fence);

    vk::PresentInfoKHR present_info = {
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &*render_semaphore,
      .swapchainCount     = 1,
      .pSwapchains        = &*swapchain,
      .pImageIndices      = &image_index
    };
    vk::Result present_result = queue.presentKHR(present_info);
  }

  void cleanup() {
    device.waitIdle();

    swapchain.~SwapchainKHR();
    surface.~SurfaceKHR();
    glfwDestroyWindow(window);
    glfwTerminate();
  }
};

int main() {
  try {
    demo app;
    app.run();
  } catch (const std::exception& e) {
    log(severity::fatal, e.what());
    return 1;
  }
  return 0;
}
