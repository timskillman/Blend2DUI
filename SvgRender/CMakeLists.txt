add_library(SvgRender
  src/SvgRenderer.cpp
)

target_include_directories(SvgRender
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(TARGET blend2d::blend2d)
  target_link_libraries(SvgRender PUBLIC blend2d::blend2d)
elseif(BLEND2D_INCLUDE_DIR AND BLEND2D_LIBRARY)
  target_include_directories(SvgRender PUBLIC "${BLEND2D_INCLUDE_DIR}")
  target_link_libraries(SvgRender PUBLIC "${BLEND2D_LIBRARY}")
endif()

target_compile_features(SvgRender PUBLIC cxx_std_17)
