#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "demo/log.h"

// this ordering is FUCKED

constexpr int width  = 800;
constexpr int height = 600;

constexpr int frames_in_flight = 2;

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

struct vertex {
  glm::vec2 position;
  glm::vec3 color;

  static vk::VertexInputBindingDescription get_binding_description() {
    return {
      .binding   = 0,
      .stride    = sizeof(vertex),
      .inputRate = vk::VertexInputRate::eVertex
    };
  }

  static std::array<vk::VertexInputAttributeDescription, 2> get_attribute_descriptions() {
    return std::to_array<vk::VertexInputAttributeDescription>({
      { .location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat,    .offset = offsetof(vertex, position) },
      { .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(vertex, color) }
    });
  }
};

static std::array verticies = std::to_array<vertex>({
  { .position = { -0.5f, -0.5f }, .color = { 0.0f, 0.0f, 0.0f } },
  { .position = { -0.5f,  0.5f }, .color = { 0.0f, 0.0f, 1.0f } },
  { .position = {  0.5f,  0.5f }, .color = { 0.0f, 1.0f, 1.0f } },
  { .position = {  0.5f, -0.5f }, .color = { 0.0f, 1.0f, 0.0f } },
});

static std::array indices = std::to_array<std::uint16_t>({
  0, 1, 2,
  2, 3, 0
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

  vk::raii::Context                    context;
  vk::raii::Instance                   instance               = nullptr;
  vk::raii::DebugUtilsMessengerEXT     debug_messenger        = nullptr;
  vk::raii::SurfaceKHR                 surface                = nullptr;
  vk::raii::PhysicalDevice             physical_device        = nullptr;
  vk::raii::Device                     device                 = nullptr;
  std::uint32_t                        queue_family_index     = std::numeric_limits<std::uint32_t>::max();
  vk::raii::Queue                      queue                  = nullptr;
  vk::raii::SwapchainKHR               swapchain              = nullptr;
  std::vector<vk::Image>               swapchain_images;
  vk::SurfaceFormatKHR                 swapchain_format;
  vk::Extent2D                         swapchain_extent;
  std::vector<vk::raii::ImageView>     swapchain_image_views;
  vk::raii::PipelineLayout             pipeline_layout        = nullptr;
  vk::raii::Pipeline                   pipeline               = nullptr;
  vk::raii::CommandPool                command_pool           = nullptr;
  vk::raii::CommandPool                transient_command_pool = nullptr;
  vk::raii::Buffer                     vertex_buffer          = nullptr;
  vk::raii::DeviceMemory               vertex_buffer_memory   = nullptr;
  vk::raii::Buffer                     index_buffer           = nullptr;
  vk::raii::DeviceMemory               index_buffer_memory    = nullptr;
  std::vector<vk::raii::CommandBuffer> command_buffers;

  std::vector<vk::raii::Semaphore> present_semaphores;
  std::vector<vk::raii::Semaphore> render_semaphores;
  std::vector<vk::raii::Fence>     draw_fences;

  std::uint32_t frame_index = 0;

  bool framebuffer_resized = false;

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

  static void framebuffer_resize_callback(GLFWwindow* window, [[maybe_unused]] int width, [[maybe_unused]] int height) {
    demo *app = reinterpret_cast<demo *>(glfwGetWindowUserPointer(window));
    app->framebuffer_resized = true;
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

    vk::VertexInputBindingDescription binding_description = vertex::get_binding_description();
    std::array attribute_descriptions = vertex::get_attribute_descriptions();
    vk::PipelineVertexInputStateCreateInfo vertex_input_state = {
      .vertexBindingDescriptionCount   = 1,
      .pVertexBindingDescriptions      = &binding_description,
      .vertexAttributeDescriptionCount = attribute_descriptions.size(),
      .pVertexAttributeDescriptions    = attribute_descriptions.data()
    };

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

  void create_command_pools() {
    vk::CommandPoolCreateInfo create_info = {
      .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = queue_family_index
    };
    command_pool = vk::raii::CommandPool(device, create_info);

    vk::CommandPoolCreateInfo transient_create_info = {
      .flags            = vk::CommandPoolCreateFlagBits::eTransient,
      .queueFamilyIndex = queue_family_index
    };
    transient_command_pool = vk::raii::CommandPool(device, transient_create_info);
  }

  std::uint32_t find_memory_type(std::uint32_t type_filter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties mem_properties = physical_device.getMemoryProperties();
    for (std::uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
      if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties)
        return i;
    }
    throw std::runtime_error("Failed to find a suitable memory type");
  }

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> create_buffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties
  ) const {
    vk::BufferCreateInfo create_info = {
      .size        = size,
      .usage       = usage,
      .sharingMode = vk::SharingMode::eExclusive
    };
    vk::raii::Buffer buffer = vk::raii::Buffer(device, create_info);

    vk::MemoryRequirements mem_requirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo alloc_info = {
      .allocationSize  = mem_requirements.size,
      .memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties)
    };
    vk::raii::DeviceMemory buffer_memory = vk::raii::DeviceMemory(device, alloc_info);
    buffer.bindMemory(buffer_memory, 0);

    return { std::move(buffer), std::move(buffer_memory) };
  };

  void copy_buffer(vk::raii::Buffer& src_buffer, vk::raii::Buffer& dst_buffer, vk::DeviceSize size) const {
    vk::CommandBufferAllocateInfo alloc_info = {
      .commandPool        = transient_command_pool,
      .level              = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = 1
    };
    vk::raii::CommandBuffer copy_command_buffer = std::move(vk::raii::CommandBuffers(device, alloc_info).front());
    copy_command_buffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    copy_command_buffer.copyBuffer(src_buffer, dst_buffer, { { .srcOffset = 0, .dstOffset = 0, .size = size } });
    copy_command_buffer.end();

    vk::CommandBufferSubmitInfo command_buffer_info = { .commandBuffer = copy_command_buffer };
    queue.submit2({ { .commandBufferInfoCount = 1, .pCommandBufferInfos = &command_buffer_info } }, nullptr);
    queue.waitIdle();
  }

  void create_vertex_buffer() {
    vk::DeviceSize buffer_size = sizeof(verticies[0]) * verticies.size();

    auto [ staging_buffer, staging_buffer_memory ] = create_buffer(
      buffer_size,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible
    | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    void *staging_data = staging_buffer_memory.mapMemory(0, buffer_size);
    std::memcpy(staging_data, verticies.data(), buffer_size);
    staging_buffer_memory.unmapMemory();

    std::tie(vertex_buffer, vertex_buffer_memory) = create_buffer(
      buffer_size,
      vk::BufferUsageFlagBits::eVertexBuffer
    | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal
    );
    copy_buffer(staging_buffer, vertex_buffer, buffer_size);
  }

  void create_index_buffer() {
    vk::DeviceSize buffer_size = sizeof(indices[0]) * indices.size();

    auto [ staging_buffer, staging_buffer_memory ] = create_buffer(
      buffer_size,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible
    | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    void *staging_data = staging_buffer_memory.mapMemory(0, buffer_size);
    std::memcpy(staging_data, indices.data(), buffer_size);
    staging_buffer_memory.unmapMemory();

    std::tie(index_buffer, index_buffer_memory) = create_buffer(
      buffer_size,
      vk::BufferUsageFlagBits::eIndexBuffer
    | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal
    );
    copy_buffer(staging_buffer, index_buffer, buffer_size);
  }

  void alloc_command_buffers() {
    vk::CommandBufferAllocateInfo alloc_info = {
      .commandPool        = command_pool,
      .level              = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = frames_in_flight
    };
    command_buffers = vk::raii::CommandBuffers(device, alloc_info);
  }

  void create_sync_objects() {
    for (std::size_t i = 0; i < swapchain_images.size(); i++) {
      render_semaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
    }

    for (std::size_t i = 0; i < frames_in_flight; i++) {
      present_semaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
      draw_fences.emplace_back(device, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
    }
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
    command_buffers[frame_index].pipelineBarrier2(dependency_info);
  }

  void record_command_buffer(std::uint32_t image_index) {
    vk::raii::CommandBuffer& command_buffer = command_buffers[frame_index];
    command_buffer.begin({});

    transition_image_layout(
      image_index,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::AccessFlagBits2::eNone,
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
    command_buffer.bindVertexBuffers(0, *vertex_buffer, { 0 });
    command_buffer.bindIndexBuffer(index_buffer, 0, vk::IndexType::eUint16);
    command_buffer.drawIndexed(indices.size(), 1, 0, 0, 0);
    command_buffer.endRendering();

    transition_image_layout(
      image_index,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::AccessFlagBits2::eNone,
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::PipelineStageFlagBits2::eNone
    );

    command_buffer.end();
  }

  void cleanup_swapchain() {
    swapchain_image_views.clear();
    swapchain = nullptr;
  }

  void recreate_swapchain() {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
      glfwWaitEvents();
      glfwGetFramebufferSize(window, &width, &height);
    }

    device.waitIdle();

    cleanup_swapchain();

    create_swapchain();
    create_image_views();
  }

  void init() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(width, height, "CAT Engine Demo", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);

    create_vulkan_instance();
    create_surface();
    select_physical_device();
    create_logical_device();
    create_swapchain();
    create_image_views();
    create_graphics_pipeline();
    create_command_pools();
    create_vertex_buffer();
    create_index_buffer();
    alloc_command_buffers();
    create_sync_objects();
  }

  void draw() {
    vk::Result fence_result
      = device.waitForFences(*draw_fences[frame_index], vk::True, std::numeric_limits<std::uint64_t>::max());
    if (fence_result != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for device to finish drawing");

    auto [ image_result, image_index ]
      = swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), present_semaphores[frame_index], nullptr);
    if (image_result == vk::Result::eErrorOutOfDateKHR) {
      recreate_swapchain();
      return;
    } else if (image_result != vk::Result::eSuccess && image_result != vk::Result::eSuboptimalKHR) {
      assert(image_result == vk::Result::eTimeout || image_result == vk::Result::eNotReady);
      throw std::runtime_error("Failed to acquire swapchain image");
    }
    device.resetFences(*draw_fences[frame_index]);
    command_buffers[frame_index].reset();
    record_command_buffer(image_index);

    vk::SemaphoreSubmitInfo wait_semaphore_submit_info = {
      .semaphore = present_semaphores[frame_index],
      .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput
    };
    vk::CommandBufferSubmitInfo command_buffer_submit_info = { .commandBuffer = command_buffers[frame_index] };
    vk::SemaphoreSubmitInfo signal_semaphore_submit_info = {
      .semaphore = render_semaphores[image_index],
      .stageMask = vk::PipelineStageFlagBits2::eVertexShader
    };
    vk::SubmitInfo2 submit_info = {
      .waitSemaphoreInfoCount   = 1,
      .pWaitSemaphoreInfos      = &wait_semaphore_submit_info,
      .commandBufferInfoCount   = 1,
      .pCommandBufferInfos      = &command_buffer_submit_info,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos    = &signal_semaphore_submit_info
    };
    queue.submit2(submit_info, draw_fences[frame_index]);

    vk::PresentInfoKHR present_info = {
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &*render_semaphores[image_index],
      .swapchainCount     = 1,
      .pSwapchains        = &*swapchain,
      .pImageIndices      = &image_index
    };
    vk::Result present_result = queue.presentKHR(present_info);
    if (present_result == vk::Result::eSuboptimalKHR || present_result == vk::Result::eErrorOutOfDateKHR || framebuffer_resized) {
      framebuffer_resized = false;
      recreate_swapchain();
    } else {
      assert(present_result == vk::Result::eSuccess);
    }

    ++frame_index %= frames_in_flight;
  }

  void cleanup() {
    device.waitIdle();

    cleanup_swapchain();
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
