cmake_minimum_required(VERSION 3.16)

project(SvgRender LANGUAGES CXX)

get_filename_component(SVGRENDER_DEFAULT_THIRD_PARTY_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../../third_party"
  ABSOLUTE
)

function(svg_render_register_blend2d_target)
  if(TARGET blend2d::blend2d OR NOT BLEND2D_INCLUDE_DIR OR NOT BLEND2D_LIBRARY)
    return()
  endif()

  add_library(blend2d::blend2d UNKNOWN IMPORTED)
  set_target_properties(blend2d::blend2d PROPERTIES
    IMPORTED_LOCATION "${BLEND2D_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${BLEND2D_INCLUDE_DIR}"
  )
endfunction()

if(NOT TARGET blend2d::blend2d)
  svg_render_register_blend2d_target()

  if(NOT TARGET blend2d::blend2d AND EXISTS "${SVGRENDER_DEFAULT_THIRD_PARTY_DIR}/blend2d/CMakeLists.txt")
    set(BLEND2D_STATIC ON CACHE BOOL "Build Blend2D as a static library" FORCE)
    set(BLEND2D_TEST OFF CACHE BOOL "Disable Blend2D tests" FORCE)
    set(BLEND2D_DEMOS OFF CACHE BOOL "Disable Blend2D demos" FORCE)
    set(BLEND2D_NO_INSTALL ON CACHE BOOL "Disable Blend2D install rules" FORCE)
    set(ASMJIT_DIR "${SVGRENDER_DEFAULT_THIRD_PARTY_DIR}/blend2d/3rdparty/asmjit" CACHE PATH "Location of 'asmjit'" FORCE)
    add_subdirectory("${SVGRENDER_DEFAULT_THIRD_PARTY_DIR}/blend2d" "${CMAKE_BINARY_DIR}/third_party/blend2d")
  endif()

  if(NOT TARGET blend2d::blend2d)
    find_package(blend2d CONFIG QUIET)
    if(NOT TARGET blend2d::blend2d)
      find_package(Blend2D CONFIG QUIET)
    endif()
  endif()

  if(NOT TARGET blend2d::blend2d)
    find_path(BLEND2D_INCLUDE_DIR NAMES blend2d/blend2d.h)
    find_library(BLEND2D_LIBRARY blend2d)
    svg_render_register_blend2d_target()
  endif()
endif()

if(NOT TARGET blend2d::blend2d)
  message(FATAL_ERROR
    "Blend2D was not found. Install Blend2D, make it discoverable to CMake, "
    "or place it in ../../third_party/blend2d.")
endif()

add_library(SvgRender
  src/SvgRenderer.cpp
)

target_include_directories(SvgRender
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(SvgRender PUBLIC blend2d::blend2d)

target_compile_features(SvgRender PUBLIC cxx_std_17)
