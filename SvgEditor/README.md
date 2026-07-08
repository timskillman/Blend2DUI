# SvgEditor

`SvgEditor` is a Blend2DUI-based 2D SVG scene editor for loading, arranging, grouping, and point-editing vector artwork on a fixed A4 landscape page.

The editor is designed around an immediate-mode UI using Blend2DUI widgets and layout, with SVG import/export handled by the local SVG scene/document code and rendering support from the surrounding Blend2DUI / SvgRender project.

## Current Functionality

- Fixed A4 landscape media with a zoomable, pannable canvas.
- SVG `Open`, `Merge`, and `Save` actions using the Blend2DUI file dialog.
- Merge keeps the current scene and selects the newly merged shapes so they can be repositioned immediately.
- Toolbar-driven editing workflow with icon buttons rendered through Blend2DUI.
- Select mode for:
  - single selection
  - marquee selection
  - move
  - rotate around selection centre
  - scale from the top-left selection anchor
  - duplicate from the selection handle
  - delete from the selection handle or `Delete`
- Group and ungroup for selected shapes.
- Undo / redo for scene editing operations.
- Clipboard-style duplication with `Ctrl+C` / `Ctrl+V`.
- Point edit mode for single editable paths:
  - anchor selection
  - additive anchor selection with `Ctrl`
  - marquee selection of anchors
  - multi-point movement
  - deletion of selected anchors
  - bezier handle editing
  - drill-down / cycling to overlapping sub-paths
- Optional point-edit display mode to show all bezier handles or only handles related to selected points.
- Floating settings panel for:
  - grid on / off
  - grid units in `mm` or `in`
  - snap to minor grid steps
  - bezier handle visibility mode

## Viewport And Grid

- Mouse wheel zooms in and out around the pointer.
- Middle mouse drag pans the canvas.
- The settings panel can stay open while normal canvas interaction continues.
- Grid majors are:
  - every `10 mm` in millimetre mode
  - every `1 in` in inch mode
- Snap uses the minor grid step:
  - `1 mm`
  - `0.1 in`

## Toolbar Overview

Left to right:

1. New
2. Open SVG
3. Save SVG
4. Merge SVG
5. Select mode
6. Point edit mode
7. Undo
8. Redo
9. Group
10. Ungroup
11. Settings

## Keyboard Shortcuts

- `Delete`: delete selected shapes, or selected anchors in point edit mode
- `Ctrl+C`: copy selected shapes
- `Ctrl+V`: paste copied shapes
- `Ctrl+Z`: undo
- `Ctrl+Y`: redo
- `Shift` while scaling selection: free scaling
  - without `Shift`, scaling is aspect-locked

## Build

The editor is built as the `svg_editor` target from this folder's `CMakeLists.txt`.

Typical workflow from the parent Blend2DUI build tree:

```powershell
cmake --build ..\build --config Debug --target svg_editor
```

There is also a small smoke target:

```powershell
cmake --build ..\build --config Debug --target svg_editor_smoke
```

On Windows, the post-build step copies the `assets` folder next to the executable and also copies the SDL runtime when required.

## Notes

- Point editing currently applies to a single editable path at a time.
- Grouped content must be ungrouped before direct point editing.
- The media is fixed to A4 landscape rather than auto-resizing to imported artwork.
- Imported artwork is scaled into the page/media space on load and merge.
