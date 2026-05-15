#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <format>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

#include "demo/log.h"

constexpr int width  = 800;
constexpr int height = 600;

constexpr Severity log_level = Severity::WARN;

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

class Demo {
public:
  void run() {
    init();
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
    }
    cleanup();
  }

private:
  GLFWwindow* window;

  vk::raii::Context                context;
  vk::raii::Instance               instance        = nullptr;
  vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;
  vk::raii::SurfaceKHR             surface         = nullptr;
  vk::raii::PhysicalDevice         physical_device = nullptr;
  vk::raii::Device                 device          = nullptr;
  vk::raii::Queue                  queue           = nullptr;

  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                         vk::DebugUtilsMessageTypeFlagsEXT type,
                                                         const vk::DebugUtilsMessengerCallbackDataEXT *data,
                                                         void *) {
    Severity log_severity;
    switch (severity) {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
      log_severity = Severity::DEBUG;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
      log_severity = Severity::INFO;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
      log_severity = Severity::WARN;
      break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
      log_severity = Severity::ERROR;
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
      case Severity::TRACE:
      case Severity::DEBUG:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;
        [[fallthrough]];
      case Severity::INFO:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
        [[fallthrough]];
      case Severity::WARN:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
        [[fallthrough]];
      case Severity::ERROR:
        severities |= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        [[fallthrough]];
      case Severity::FATAL:
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
                           vk::PhysicalDeviceVulkan13Features,
                           vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> features
          = device.getFeatures2<vk::PhysicalDeviceFeatures2,
                                vk::PhysicalDeviceVulkan13Features,
                                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        return features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering
            && features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;
      });
    if (first_device == physical_devices.end()) throw std::runtime_error("Failed to find a device with Vulkan support");
    physical_device = *first_device;
  }

  void create_logical_device() {
    std::vector<vk::QueueFamilyProperties> queue_family_props = physical_device.getQueueFamilyProperties();
    uint32_t queue_family_index = UINT32_MAX;
    for (auto [ qfp_index, qfp ] : queue_family_props | std::views::enumerate) {
      if (qfp.queueFlags & vk::QueueFlagBits::eGraphics && physical_device.getSurfaceSupportKHR(qfp_index, *surface)) {
        queue_family_index = qfp_index;
        break;
      }
    }
    assert(queue_family_index != UINT32_MAX && "Queue family doesn't support graphics even though it should");
    float queue_priority = 0.5f;
    vk::DeviceQueueCreateInfo queue_create_info = {
      .queueFamilyIndex = queue_family_index,
      .queueCount       = 1,
      .pQueuePriorities = &queue_priority
    };

    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> features = {
      {},
      { .dynamicRendering = true },
      { .extendedDynamicState = true }
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

  void init() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, "CAT Engine Demo", nullptr, nullptr);

    create_vulkan_instance();
    create_surface();
    select_physical_device();
    create_logical_device();
  }

  void cleanup() {
    glfwDestroyWindow(window);

    glfwTerminate();
  }
};

int main() {
  try {
    Demo demo;
    demo.run();
  } catch (const std::exception& e) {
    log(Severity::FATAL, e.what());
    return 1;
  }
  return 0;
}
