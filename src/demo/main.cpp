#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <utility>
#include <vector>

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

#include "demo/log.h"

constexpr int width  = 800;
constexpr int height = 600;

constexpr Severity log_level = Severity::WARN;

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
  vk::raii::PhysicalDevice         physical_device = nullptr;

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

    std::vector<const char *> layers;
    if (enable_validation_layers) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    std::vector<vk::LayerProperties> layer_props = context.enumerateInstanceLayerProperties();
    std::vector<const char *>::iterator unsupported_layer = std::ranges::find_if(layers, [&layer_props](const char *layer) -> bool {
      return std::ranges::none_of(layer_props, [layer](const vk::LayerProperties& layer_prop) -> bool {
        return std::strcmp(layer, layer_prop.layerName) == 0;
      });
    });
    if (unsupported_layer != layers.end())
      throw std::runtime_error(std::format("Required layer not supported: {}", *unsupported_layer));

    std::uint32_t glfw_num_exts;
    const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_num_exts);
    std::vector<const char *> exts = std::vector(glfw_exts, glfw_exts + glfw_num_exts);
    if (enable_validation_layers) {
      exts.push_back(vk::EXTDebugUtilsExtensionName);
    }
    std::vector<vk::ExtensionProperties> ext_props = context.enumerateInstanceExtensionProperties();
    std::vector<const char*>::iterator unsupported_ext = std::ranges::find_if(exts, [&ext_props](const char *ext) -> bool {
      return std::ranges::none_of(ext_props, [ext](const vk::ExtensionProperties& ext_prop) -> bool {
        return std::strcmp(ext, ext_prop.extensionName);
      });
    });
    if (unsupported_ext != exts.end())
      throw std::runtime_error(std::format("Requied extension not supported: {}", *unsupported_ext));

    vk::InstanceCreateInfo create_info = {
      .pApplicationInfo        = &app_info,
      .enabledLayerCount       = static_cast<std::uint32_t>(layers.size()),
      .ppEnabledLayerNames     = layers.data(),
      .enabledExtensionCount   = static_cast<std::uint32_t>(exts.size()),
      .ppEnabledExtensionNames = exts.data()
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
      vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> create_info_chain = {
        create_info,
        {
          .messageSeverity = severities,
          .messageType = types,
          .pfnUserCallback = &debug_callback
        }
      };
      instance = vk::raii::Instance(context, create_info_chain.get<vk::InstanceCreateInfo>());
      debug_messenger = instance.createDebugUtilsMessengerEXT(create_info_chain.get<vk::DebugUtilsMessengerCreateInfoEXT>());
    } else {
      instance = vk::raii::Instance(context, create_info);
    } 
  }

  void select_physical_device() {
    std::vector<const char *> exts = {
      vk::KHRSwapchainExtensionName
    };
    std::vector<vk::raii::PhysicalDevice> physical_devices = instance.enumeratePhysicalDevices();
    std::vector<vk::raii::PhysicalDevice>::iterator first_device
      = std::ranges::find_if(physical_devices, [&exts](const vk::raii::PhysicalDevice& device) -> bool {
        if (device.getProperties().apiVersion < vk::ApiVersion13) return false;
        if (std::ranges::none_of(device.getQueueFamilyProperties(), [](const vk::QueueFamilyProperties& qfp) -> bool {
          return static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
        })) return false;
        std::vector<vk::ExtensionProperties> ext_props = device.enumerateDeviceExtensionProperties();
        if (!std::ranges::all_of(exts, [&ext_props](const char *ext) -> bool {
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

  void init() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, "CAT Engine Demo", nullptr, nullptr);

    create_vulkan_instance();
    select_physical_device();
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
