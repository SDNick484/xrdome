cmake_minimum_required(VERSION 3.22)
project(xrdome_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# Same file layout the meson build uses: this file is android/app/src/main/cpp/.
get_filename_component(XRD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# The version: meson.build's project(version:), so the app and the player agree.
file(READ "${XRD_ROOT}/meson.build" XRD_MESON)
if(NOT XRD_MESON MATCHES "\n[ \t]*version[ \t]*:[ \t]*'([^']+)'")
  message(FATAL_ERROR "no project(version:) in meson.build")
endif()
set(XRD_VERSION "${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XRD_ROOT}/meson.build")

# The core's sources: src/core_sources.txt, one path per line; blank and '#' lines dropped
# (the filtering meson.build does). Add a core file to the list, not here.
set(XRD_LIST "${XRD_ROOT}/src/core_sources.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XRD_LIST}")
file(STRINGS "${XRD_LIST}" XRD_LIST_LINES)
set(XRD_CORE_SOURCES "")
foreach(line IN LISTS XRD_LIST_LINES)
  string(STRIP "${line}" line)
  if(NOT line STREQUAL "" AND NOT line MATCHES "^#")
    list(APPEND XRD_CORE_SOURCES "${XRD_ROOT}/${line}")
  endif()
endforeach()
if(NOT XRD_CORE_SOURCES)
  message(FATAL_ERROR "no sources read from ${XRD_LIST}")
endif()

# shaders_gen.h: the same generator and inputs as meson's shaders_gen. It needs Python 3 on
# the build host (tools/embed_shaders.py is standard library only).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(XRD_SHADERS ${XRD_ROOT}/src/render/shaders)
set(XRD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${XRD_GEN_DIR}")
add_custom_command(
  OUTPUT "${XRD_GEN_DIR}/shaders_gen.h"
  COMMAND "${Python3_EXECUTABLE}" "${XRD_ROOT}/tools/embed_shaders.py" "${XRD_GEN_DIR}/shaders_gen.h"
          "VR180_VERT=${XRD_SHADERS}/vr180.vert" "VR180_FRAG=${XRD_SHADERS}/vr180.frag"
          "SHARPEN_FRAG=${XRD_SHADERS}/sharpen.frag"
  DEPENDS "${XRD_ROOT}/tools/embed_shaders.py" "${XRD_SHADERS}/vr180.vert"
          "${XRD_SHADERS}/vr180.frag" "${XRD_SHADERS}/sharpen.frag"
  VERBATIM)
add_custom_target(xrdome_shaders DEPENDS "${XRD_GEN_DIR}/shaders_gen.h")

add_library(xrdome_core STATIC ${XRD_CORE_SOURCES})
add_dependencies(xrdome_core xrdome_shaders)
target_include_directories(xrdome_core PUBLIC "${XRD_ROOT}/src" "${XRD_GEN_DIR}")
target_compile_definitions(xrdome_core PUBLIC "XRD_VERSION=\"${XRD_VERSION}\"")
target_compile_options(xrdome_core PRIVATE -Wall -Wextra)
# libm and pthread are part of bionic's libc; GLESv3 comes from the NDK sysroot.
target_link_libraries(xrdome_core PUBLIC GLESv3 EGL android log)

add_library(xrdome_jni SHARED jni_bridge.c)
target_link_libraries(xrdome_jni PRIVATE xrdome_core)
target_compile_options(xrdome_jni PRIVATE -Wall -Wextra)
