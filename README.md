# Why this toolkit?

When it comes to C++, cross-platform, UI toolkits there are suprisingly very few options that are lightweight, attractive and free! This toolkit aims to use the fastest 2D renderer out there, [Blend2D](https://github.com/blend2d/blend2d) using a lightwight cross-platform render backend - [SDL3](https://github.com/libsdl-org/SDL).

This UI toolkit in an Immediate Mode Graphical User Interface (IMGUI) that aims to be far more versatile, attractive, fast and lightweight even to run on something as small as a Pi Zero!  Indeed development will continue to be tested on a Pi Zero to validate the UI's workability.

This small set of demos takes a first step in rendering versatile widgets including gradient animated buttons, high-speed text areas and file dialogs.  It also renders high quality, scaleable SVG icons used for UI elements making for a professional interface. 
So much more to come but I hope this demo gives you a flavour of what the UI will do.

![alt text](https://github.com/timskillman/Blend2DUI/blob/master/assets/screenshot.jpg "Blend2D UI Demo Screenshot")

# Blend2D Demo and UI Toolkit

This repository contains a C++17 Blend2D demo project with two main pieces:

- `blend2d_shapes_demo`: a command-line renderer that writes PNG output and can render SVGs through the `SvgRender` library.
- `Blend2DUI`: a reusable SDL3/Blend2D UI library plus `blend2d_ui_demo`, an interactive demo application.

The UI library currently includes buttons, sliders, text input, file dialogs, platform font discovery, scene management, and SDL event integration. Demo-specific layout code lives in `Blend2DUI/blend2d_ui_demo`, while the reusable widgets live under `Blend2DUI/include` and `Blend2DUI/src`.

Note: You will need to download the [Blend2D](https://github.com/blend2d/blend2d) and [SDL3](https://github.com/libsdl-org/SDL) libraries and place them in a 'third_party' folder as shown below for these examples to compile and run.
This code has been tested on RasperryPi's, Windows and Mac.

## Repository Layout

```text
assets/                  Demo assets, including SVG examples
SvgRender/               SVG renderer library and command-line demo entry point
Blend2DUI/include/       Reusable UI headers
Blend2DUI/src/           Reusable UI implementation
Blend2DUI/blend2d_ui_demo/ Demo-specific renderer, layout, and entry point
../third_party/blend2d/  Shared vendored Blend2D, when present
../third_party/SDL/      Shared vendored SDL3, when present
```

Generated CMake and Visual Studio files are ignored by `.gitignore`. Keep build output in `build/` or a preset build directory.

## Prerequisites

- CMake 3.16 or newer
- A C++17 compiler
- Blend2D, either vendored at `../third_party/blend2d` or installed where CMake can find it
- SDL3 for the UI demo, either vendored at `../third_party/SDL` or installed where CMake/pkg-config can find it
- A desktop session for `blend2d_ui_demo`

On Linux or Raspberry Pi OS, install the usual compiler tools and fonts:

```sh
sudo apt update
sudo apt install -y build-essential cmake fonts-dejavu-core fonts-liberation2
```

## Build

Configure and build with CMake:

```sh
cmake -S . -B build
cmake --build build
```

Build only the interactive UI demo:

```sh
cmake --build build --target blend2d_ui_demo
```

Build only the PNG/SVG renderer:

```sh
cmake --build build --target blend2d_shapes_demo
```

Build the file-dialog filter test:

```sh
cmake --build build --target blend2d_ui_file_dialog_filter_test
```

If Blend2D is installed outside the usual search paths:

```sh
cmake -S . -B build \
  -DBLEND2D_INCLUDE_DIR=/path/to/blend2d/src \
  -DBLEND2D_LIBRARY=/path/to/libblend2d.so
cmake --build build
```

## Windows and Visual Studio

The repository includes a `vs2026-x64` CMake preset that keeps Visual Studio output in `build/vs2026-x64`.

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-debug
cmake --open build/vs2026-x64
```

For generated Visual Studio solutions, `blend2d_ui_demo` is set as the startup project. The debugger working directory points at the source tree, and `SDL3.dll` is copied beside `blend2d_ui_demo.exe` automatically when SDL3 is built as an imported target.

## Run The UI Demo

After building:

```powershell
.\build\Blend2DUI\Debug\blend2d_ui_demo.exe
```

For single-config generators on Linux:

```sh
./build/Blend2DUI/blend2d_ui_demo
```

The demo exercises the reusable UI controls, including buttons, horizontal and vertical sliders, single-line text input, multi-line text input, scrollbars, quick links, and the file dialog.

To profile the render loop, set `BLEND2DUI_PROFILE=1` before launching the demo. The app prints averaged timings every 120 frames for renderer stages and demo UI sections such as menu, inputs, sliders, canvas, file dialog, texture upload, and present.

```powershell
$env:BLEND2DUI_PROFILE = "1"
.\build\Blend2DUI\Debug\blend2d_ui_demo.exe
```

## Auto UI Layout

`Blend2DUI` includes an optional cursor layout layer for controls that do not need absolute `BLRect` placement. Define a styled `UI_RectArea`, call `UI_CursorRect(area)`, then draw widgets with size strings such as `"80x40"`, `"10%x40"`, or `"15%x50%"`. Percentages are resolved against the active rectangle drawable area, so they update when the area changes.

```cpp
UI_RectStyleDefinition panelStyle("Padding:16, RectFill:#FFFFFF, RectStroke:#D0D7E2, RectStrokeWidth:1, RectCorner:8");
UI_RectArea panel;
panel.SetRect(BLRect(28, 28, width - 56, 72), panelStyle);

UI_CursorRect(panel);
UI_CursorLeft(14);
UI_Button("select", "132x40", buttonStyle, UI_ButtonContent("Select"));
UI_Button("load", "15%x40", buttonStyle, UI_ButtonContent("Load..."));
```

Cursor helpers include `UI_CursorSave`, `UI_CursorUse`, `UI_CursorOffset`, `UI_CursorNext`, `UI_CursorLeft`, `UI_CursorRight`, `UI_CursorTop`, `UI_CursorBottom`, `UI_CursorVerticalCenter`, `UI_CursorHorizontalCenter`, `UI_CursorLine`, and `UI_CursorGap`. Size-string overloads are currently available for buttons, text inputs, and sliders. Controls are clipped to the active rectangle drawable area, and regions outside that drawable area do not receive pointer selection.

Text input and slider options also accept string definitions:

```cpp
UI_TextInputOptions textOptions("Mode:MultiLine, Resizable:true, Placeholder:'Notes'");
UI_TextInput("notes", "480x120", textOptions, notesText, inputStyle);

UI_SliderOptions sliderOptions("Heading:'Volume', Min:0, Max:100, Default:50, Step:1, Integer:true, Thumb:Circle");
UI_Slider("volume", "360x58", sliderOptions, volume, sliderStyle);
```

## Run The Shapes Demo

By default the shapes demo writes `blend2d_shapes_demo.png` and auto-detects common Linux, Raspberry Pi, macOS, and Windows font paths.

```sh
./build/blend2d_shapes_demo --output blend2d_shapes_demo.png
```

To force specific fonts:

```sh
./build/blend2d_shapes_demo \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf \
  --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf \
  --output pi-demo.png
```

The renderer exits with a clear error if it cannot find enough usable fonts.

## Render SVG

Render an SVG file to a bitmap:

```sh
./build/blend2d_shapes_demo --svg assets/svg-demo.svg --output svg-demo.png
```

Scale the output bitmap by width while preserving the SVG aspect ratio:

```sh
./build/blend2d_shapes_demo --svg assets/svg-demo.svg --width 480 --output svg-demo-480.png
```

When `--width` is omitted, zero, or negative, the renderer uses the SVG canvas size derived from `width`/`height` or the `viewBox`.

The SVG renderer supports a practical subset for diagrams and demos:

- shapes: `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon`
- paths: `M`, `L`, `H`, `V`, `C`, `Q`, `A`, `Z`, including relative commands
- text with TTF/TTC font selection from discovered fonts
- fills and strokes, including opacity
- dashed lines and dashed polylines
- linear and radial gradients from `defs`
- CSS from `style` blocks for tag, `.class`, and `#id` selectors, plus inline `style`

It is not intended to be a full browser SVG engine, but it covers the common features used by generated and hand-authored diagram SVGs.

## Notes For Contributors

- Keep reusable UI code in `Blend2DUI/include` and `Blend2DUI/src`.
- Keep demo-only rendering and layout code in `Blend2DUI/blend2d_ui_demo`.
- Prefer adding tests under `Blend2DUI/tests` for reusable behavior.
- Avoid committing generated build output; `.gitignore` covers the common CMake, Visual Studio, binary, and demo-output files.
