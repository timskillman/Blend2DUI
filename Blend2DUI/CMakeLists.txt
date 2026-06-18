find_package(SDL3 CONFIG QUIET)

get_filename_component(BLEND2D_DEMO_DEFAULT_THIRD_PARTY_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../../third_party"
  ABSOLUTE
)
get_filename_component(BLEND2D_DEMO_LEGACY_THIRD_PARTY_DIR
  "${CMAKE_SOURCE_DIR}/third_party"
  ABSOLUTE
)

if(NOT DEFINED BLEND2D_DEMO_THIRD_PARTY_DIR)
  set(BLEND2D_DEMO_THIRD_PARTY_DIR "${BLEND2D_DEMO_DEFAULT_THIRD_PARTY_DIR}" CACHE PATH "Path to Blend2D demo third-party dependencies")
else()
  get_filename_component(BLEND2D_DEMO_THIRD_PARTY_DIR_ABSOLUTE
    "${BLEND2D_DEMO_THIRD_PARTY_DIR}"
    ABSOLUTE
  )
  if(BLEND2D_DEMO_THIRD_PARTY_DIR_ABSOLUTE STREQUAL BLEND2D_DEMO_LEGACY_THIRD_PARTY_DIR)
    set(BLEND2D_DEMO_THIRD_PARTY_DIR "${BLEND2D_DEMO_DEFAULT_THIRD_PARTY_DIR}" CACHE PATH "Path to Blend2D demo third-party dependencies" FORCE)
  endif()
endif()

if(NOT TARGET SDL3::SDL3 AND EXISTS "${BLEND2D_DEMO_THIRD_PARTY_DIR}/SDL/CMakeLists.txt")
  set(SDL_SHARED ON CACHE BOOL "Build SDL3 shared library" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "Do not build SDL3 static library" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build SDL3 test library" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "Do not build SDL3 tests" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL3 examples" FORCE)
  set(SDL_X11_XTEST OFF CACHE BOOL "Do not require optional XTest support for SDL3 X11" FORCE)
  add_subdirectory("${BLEND2D_DEMO_THIRD_PARTY_DIR}/SDL" "${CMAKE_BINARY_DIR}/third_party/SDL")
endif()

if(NOT TARGET SDL3::SDL3)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
  endif()
endif()

if(NOT TARGET SDL3::SDL3 AND NOT TARGET PkgConfig::SDL3)
  message(STATUS "SDL3 not found; skipping Blend2DUI targets")
  return()
endif()

add_library(Blend2DUI
  src/Button.cpp
  src/FileDialog.cpp
  src/FontManager.cpp
  src/SceneManager.cpp
  src/TextInput.cpp
)

target_include_directories(Blend2DUI
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(Blend2DUI
  PUBLIC
    blend2d::blend2d
    SvgRender
)

if(TARGET SDL3::SDL3)
  target_link_libraries(Blend2DUI PUBLIC SDL3::SDL3)
else()
  target_link_libraries(Blend2DUI PUBLIC PkgConfig::SDL3)
endif()

target_compile_features(Blend2DUI PUBLIC cxx_std_17)

add_executable(blend2d_ui_demo
  blend2d_ui_demo/SdlBlend2DRenderer.cpp
  blend2d_ui_demo/demo.cpp
)

target_link_libraries(blend2d_ui_demo PRIVATE Blend2DUI)

add_executable(blend2d_ui_file_dialog_filter_test
  tests/file_dialog_filter_test.cpp
)

target_include_directories(blend2d_ui_file_dialog_filter_test
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(blend2d_ui_file_dialog_filter_test
  PRIVATE
    blend2d::blend2d
)

if(TARGET SDL3::SDL3)
  target_link_libraries(blend2d_ui_file_dialog_filter_test PRIVATE SDL3::SDL3)
else()
  target_link_libraries(blend2d_ui_file_dialog_filter_test PRIVATE PkgConfig::SDL3)
endif()

target_compile_features(blend2d_ui_file_dialog_filter_test PRIVATE cxx_std_17)

if(WIN32)
  set_target_properties(blend2d_ui_demo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )

  if(TARGET SDL3::SDL3)
    add_custom_command(
      TARGET blend2d_ui_demo
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_FILE:SDL3::SDL3>"
              "$<TARGET_FILE_DIR:blend2d_ui_demo>"
      COMMENT "Copying SDL3 runtime next to blend2d_ui_demo"
    )
  endif()
endif()
