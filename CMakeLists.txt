cmake_minimum_required(VERSION 3.16)

project(blend2d_shapes_demo VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(blend2d_shapes_demo
  SvgRender/src/main.cpp
)

if(WIN32)
  set_target_properties(blend2d_shapes_demo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endif()

get_filename_component(BLEND2D_THIRD_PARTY_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party"
  ABSOLUTE
)

if(EXISTS "${BLEND2D_THIRD_PARTY_DIR}/blend2d/CMakeLists.txt")
  set(BLEND2D_STATIC ON CACHE BOOL "Build Blend2D as a static library" FORCE)
  set(BLEND2D_TEST OFF CACHE BOOL "Disable Blend2D tests" FORCE)
  set(BLEND2D_DEMOS OFF CACHE BOOL "Disable Blend2D demos" FORCE)
  set(BLEND2D_NO_INSTALL ON CACHE BOOL "Disable Blend2D install rules" FORCE)
  set(ASMJIT_DIR "${BLEND2D_THIRD_PARTY_DIR}/blend2d/3rdparty/asmjit" CACHE PATH "Location of 'asmjit'" FORCE)
  add_subdirectory("${BLEND2D_THIRD_PARTY_DIR}/blend2d" "${CMAKE_BINARY_DIR}/third_party/blend2d")
else()
  find_package(blend2d CONFIG QUIET)
  if(NOT TARGET blend2d::blend2d)
    find_package(Blend2D CONFIG QUIET)
  endif()
endif()

if(TARGET blend2d::blend2d)
  target_link_libraries(blend2d_shapes_demo PRIVATE blend2d::blend2d)
else()
  find_path(BLEND2D_INCLUDE_DIR blend2d.h)
  find_library(BLEND2D_LIBRARY blend2d)

  if(NOT BLEND2D_INCLUDE_DIR OR NOT BLEND2D_LIBRARY)
    message(FATAL_ERROR
      "Blend2D was not found. Install Blend2D or pass "
      "-DBLEND2D_INCLUDE_DIR=/path/to/include and "
      "-DBLEND2D_LIBRARY=/path/to/libblend2d.so")
  endif()

  target_include_directories(blend2d_shapes_demo PRIVATE "${BLEND2D_INCLUDE_DIR}")
  target_link_libraries(blend2d_shapes_demo PRIVATE "${BLEND2D_LIBRARY}")
endif()

add_subdirectory(SvgRender)
target_link_libraries(blend2d_shapes_demo PRIVATE SvgRender)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(blend2d_shapes_demo PRIVATE stdc++fs)
endif()

target_compile_options(blend2d_shapes_demo PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Blend2DUI/CMakeLists.txt")
  add_subdirectory(Blend2DUI)
endif()

if(WIN32 AND TARGET blend2d_ui_demo)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT blend2d_ui_demo)
endif()

install(TARGETS blend2d_shapes_demo
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES README.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

set(CPACK_PACKAGE_NAME "blend2d-svg-converter")
set(CPACK_PACKAGE_VENDOR "Codex")
set(CPACK_PACKAGE_CONTACT "tim@localhost")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Blend2D SVG to PNG converter")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://blend2d.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Tim")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
