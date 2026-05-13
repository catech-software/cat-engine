#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

constexpr int width  = 800;
constexpr int height = 600;

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

  vk::raii::Context  context;
  vk::raii::Instance instance = nullptr;
  vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;

  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                                                         vk::DebugUtilsMessageTypeFlagsEXT type,
                                                         const vk::DebugUtilsMessengerCallbackDataEXT *data,
                                                         void *) {
    std::cerr << "Validation layer " << vk::to_string(severity) << ' ' << vk::to_string(type) << ' ' << data->pMessage << '\n';
    return vk::False;
  }

  void create_vulkan_instance() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, "CAT Engine Demo", nullptr, nullptr);

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
    if (unsupported_layer != layers.end()) {
      throw std::runtime_error("Required layer not supported: " + std::string(*unsupported_layer));
    }

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
    if (unsupported_ext != exts.end()) {
      throw std::runtime_error("Requied extension not supported: " + std::string(*unsupported_ext));
    }

    vk::InstanceCreateInfo instance_create_info = {
      .pApplicationInfo        = &app_info,
      .enabledLayerCount       = static_cast<std::uint32_t>(layers.size()),
      .ppEnabledLayerNames     = layers.data(),
      .enabledExtensionCount   = static_cast<std::uint32_t>(exts.size()),
      .ppEnabledExtensionNames = exts.data()
    };

    instance = vk::raii::Instance(context, instance_create_info);
  }

  void setup_vulkan_debug_messenger() {
    if (!enable_validation_layers) return;
    vk::DebugUtilsMessageSeverityFlagsEXT message_severities = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
                                                             | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
                                                             | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                                                             | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    vk::DebugUtilsMessageTypeFlagsEXT message_types = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
                                                    | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
                                                    | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    vk::DebugUtilsMessengerCreateInfoEXT messenger_create_info = {
      .messageSeverity = message_severities,
      .messageType = message_types,
      .pfnUserCallback = &debug_callback
    };
    debug_messenger = instance.createDebugUtilsMessengerEXT(messenger_create_info);
  }

  void init() {
    create_vulkan_instance();
    setup_vulkan_debug_messenger();
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
    std::cerr << "FATAL: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
