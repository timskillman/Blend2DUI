cmake_minimum_required(VERSION 3.16)

project(SvgEditor LANGUAGES CXX)

add_executable(svg_editor
  assets/icons/entry-arrow.svg
  assets/icons/fill.svg
  assets/icons/fit.svg
  assets/icons/folder.svg
  assets/icons/group.svg
  assets/icons/new.svg
  assets/icons/open-svg.svg
  assets/icons/path-edit.svg
  assets/icons/point-edit.svg
  assets/icons/redo.svg
  assets/icons/save.svg
  assets/icons/save-selection.svg
  assets/icons/stroke.svg
  assets/icons/undo.svg
  assets/icons/ungroup.svg
  src/GroupEdit.cpp
  src/PointEdit.cpp
  src/PointSelectionController.cpp
  src/SelectionTool.cpp
  src/ShapeSelectionController.cpp
  src/SvgDocument.cpp
  src/SvgEditorApp.cpp
  src/SvgSceneIO.cpp
  src/Transform.cpp
  src/UndoRedo.cpp
  src/main.cpp
)

add_executable(svg_editor_smoke
  src/GroupEdit.cpp
  src/SelectionTool.cpp
  src/SvgDocument.cpp
  src/SvgSceneIO.cpp
  src/Transform.cpp
  src/UndoRedo.cpp
  src/UndoSmoke.cpp
)

target_include_directories(svg_editor
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(svg_editor_smoke
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(svg_editor
  PRIVATE
    Blend2DUI
)

target_link_libraries(svg_editor_smoke
  PRIVATE
    Blend2DUI
)

target_compile_features(svg_editor PRIVATE cxx_std_17)
target_compile_features(svg_editor_smoke PRIVATE cxx_std_17)

if(WIN32)
  set_target_properties(svg_editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:svg_editor>"
  )

  add_custom_command(
    TARGET svg_editor
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:svg_editor>/assets"
    COMMENT "Copying SvgEditor assets"
  )

  if(TARGET SDL3::SDL3)
    add_custom_command(
      TARGET svg_editor
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_FILE:SDL3::SDL3>"
              "$<TARGET_FILE_DIR:svg_editor>"
      COMMENT "Copying SDL3 runtime next to svg_editor"
    )
  endif()
endif()
