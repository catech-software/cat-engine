cmake_minimum_required(VERSION 3.26...4.4)
project(cat-engine CXX)

function(compile_slang_shader MODULE)
  cmake_path(GET MODULE PARENT_PATH OUT_DIR)
  add_custom_command(
    OUTPUT "${OUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" "-E" "make_directory" "${OUT_DIR}"
    VERBATIM
  )
  list(TRANSFORM ARGN PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
  add_custom_command(
    OUTPUT "${MODULE}"
    COMMAND "${SLANG_EXECUTABLE}" "-target" "spirv" "-profile" "spirv_1_6" "-o" "${MODULE}" ${ARGN}
    DEPENDS ${ARGN} "${OUT_DIR}"
    WORKING_DIRECTORY "${OUT_DIR}"
    VERBATIM
  )
endfunction()

function(copy_dlls TARGET)
  add_custom_command(
    TARGET "${TARGET}" POST_BUILD
    #TODO: when 4.4 becomes widely available, change this to copy_if_different
    COMMAND "${CMAKE_COMMAND}" "-E" "copy" "-t" "$<TARGET_FILE_DIR:${TARGET}>" "$<TARGET_RUNTIME_DLLS:${TARGET}>"
    COMMAND_EXPAND_LISTS
    VERBATIM
  )
endfunction()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(slang REQUIRED)
find_package(glm REQUIRED)

add_subdirectory("src")
