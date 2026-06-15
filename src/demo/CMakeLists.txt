add_executable(cat-engine-demo "main.cpp" "log.cpp")
target_link_libraries(cat-engine-demo Vulkan::Vulkan glfw glm::glm)
target_include_directories(cat-engine-demo PRIVATE
  "${PROJECT_BINARY_DIR}/generated/src"
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_BINARY_DIR}/generated/include"
  "${PROJECT_SOURCE_DIR}/include"
)
target_compile_definitions(cat-engine-demo PRIVATE "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>")
target_compile_options(cat-engine-demo PRIVATE
  "--embed-dir=${PROJECT_BINARY_DIR}/generated/src"
  "--embed-dir=${PROJECT_SOURCE_DIR}/src"
  "-Wall" "-Wextra" "-Wpedantic"
  "$<$<CONFIG:Debug>:-g3;-Og>"
  "$<$<CONFIG:Release>:-O2>"
  "$<$<CONFIG:RelWithDebInfo>:-g3;-O2>"
  "$<$<CONFIG:MinSizeRel>:-Oz>"
)

set(SHADER_OUT_DIR "${PROJECT_BINARY_DIR}/generated/src/demo/shaders")
compile_slang_shader("${SHADER_OUT_DIR}/default.spv" "shaders/default.slang")
add_custom_target(shaders DEPENDS "${SHADER_OUT_DIR}/default.spv")
add_dependencies(cat-engine-demo shaders)

copy_dlls(cat-engine-demo)
