add_executable(cat-engine-demo main.cpp log.cpp)
target_link_libraries(cat-engine-demo Vulkan::Vulkan glfw glm::glm)
target_include_directories(cat-engine-demo PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(cat-engine-demo PRIVATE
  "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>"
  "VULKAN_HPP_NO_STRUCT_CONSTRUCTORS"
)
target_compile_options(cat-engine-demo PRIVATE
  "-Wall" "-Wextra" "-Wpedantic"
  "$<$<CONFIG:Debug>:-g3;-Og>"
  "$<$<CONFIG:Release>:-O2>"
  "$<$<CONFIG:RelWithDebInfo>:-g3;-O2>"
  "$<$<CONFIG:MinSizeRel>:-Oz>"
)
