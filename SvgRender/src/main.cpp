#include <blend2d/blend2d.h>

#include "SvgRender/SvgRenderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Options {
  std::string output = "blend2d_shapes_demo.png";
  std::string svgInput;
  int svgWidth = 0;
  int svgHeight = 0;
  std::vector<std::string> fonts;
};

static void printUsage(const char* appName) {
  std::cout
      << "Usage: " << appName << " [--output demo.png] [--font file.ttf]... [--svg file.svg] [--width px] [--height px]\n"
      << "\n"
      << "Render a Blend2D PNG demo with polygons, line styles, gradients, and text.\n"
      << "Use --svg to render an SVG file to a PNG bitmap instead of the built-in demo.\n"
      << "Pass at least three --font values, or let the app discover common Linux fonts.\n";
}

static Options parseOptions(int argc, char** argv) {
  Options options;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    }

    if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
      options.output = argv[++i];
      continue;
    }

    if (arg == "--font" && i + 1 < argc) {
      options.fonts.emplace_back(argv[++i]);
      continue;
    }

    if (arg == "--svg" && i + 1 < argc) {
      options.svgInput = argv[++i];
      continue;
    }

    if (arg == "--width" && i + 1 < argc) {
      options.svgWidth = std::atoi(argv[++i]);
      continue;
    }

    if (arg == "--height" && i + 1 < argc) {
      options.svgHeight = std::atoi(argv[++i]);
      continue;
    }

    std::cerr << "Unknown or incomplete option: " << arg << "\n\n";
    printUsage(argv[0]);
    std::exit(2);
  }

  return options;
}

static bool appendFontIfUsable(std::vector<std::string>& fonts, const std::string& path) {
  if (!fs::is_regular_file(path)) {
    return false;
  }

  if (std::find(fonts.begin(), fonts.end(), path) != fonts.end()) {
    return false;
  }

  BLFontFace face;
  if (face.create_from_file(path.c_str()) != BL_SUCCESS) {
    return false;
  }

  fonts.emplace_back(path);
  return true;
}

static fs::path getWindowsFontsDirectory() {
#ifdef _WIN32
  const char* windowsDir = std::getenv("WINDIR");
  if (!windowsDir || !*windowsDir) {
    windowsDir = std::getenv("SystemRoot");
  }

  if (windowsDir && *windowsDir) {
    return fs::path(windowsDir) / "Fonts";
  }
#endif

  return {};
}

static std::vector<std::string> discoverFonts(std::vector<std::string> requested) {
  std::vector<std::string> fonts;

  for (const std::string& path : requested) {
    appendFontIfUsable(fonts, path);
  }

#ifdef _WIN32
  const fs::path windowsFontsDir = getWindowsFontsDirectory();
  if (!windowsFontsDir.empty()) {
    const std::array<const char*, 10> candidates = {{
        "segoeui.ttf",
        "arial.ttf",
        "calibri.ttf",
        "tahoma.ttf",
        "verdana.ttf",
        "times.ttf",
        "georgia.ttf",
        "cambria.ttc",
        "consola.ttf",
        "cour.ttf",
    }};

    for (const char* path : candidates) {
      appendFontIfUsable(fonts, (windowsFontsDir / path).string());
      if (fonts.size() >= 3) {
        break;
      }
    }
  }

  if (fonts.size() >= 3) {
    return fonts;
  }
#endif

  const std::array<const char*, 18> candidates = {{
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSerif.ttf",
      "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/local/share/fonts/DejaVuSans.ttf",
      "/usr/local/share/fonts/DejaVuSerif.ttf",
      "/usr/local/share/fonts/DejaVuSansMono.ttf",
      "/opt/vc/src/hello_pi/hello_font/Vera.ttf",
      "/home/pi/.fonts/DejaVuSans.ttf",
  }};

  for (const char* path : candidates) {
    appendFontIfUsable(fonts, path);
    if (fonts.size() >= 3) {
      break;
    }
  }

  return fonts;
}

static BLGradient makeLinearGradient(double x0, double y0, double x1, double y1) {
  BLGradient gradient(BLLinearGradientValues(x0, y0, x1, y1));
  gradient.add_stop(0.00, BLRgba32(0xFF155E75u));
  gradient.add_stop(0.46, BLRgba32(0xFF22C55Eu));
  gradient.add_stop(1.00, BLRgba32(0xFFF97316u));
  return gradient;
}

static BLGradient makeRadialGradient(double cx, double cy, double r) {
  BLGradient gradient(BLRadialGradientValues(cx, cy, cx - r * 0.35, cy - r * 0.35, r));
  gradient.add_stop(0.00, BLRgba32(0xFFFFFFFFu));
  gradient.add_stop(0.35, BLRgba32(0xFF38BDF8u));
  gradient.add_stop(1.00, BLRgba32(0xFF1E3A8Au));
  return gradient;
}

static BLPath makePolygon(std::initializer_list<BLPoint> points) {
  BLPath path;
  bool first = true;

  for (const BLPoint& point : points) {
    if (first) {
      path.move_to(point.x, point.y);
      first = false;
    } else {
      path.line_to(point.x, point.y);
    }
  }

  path.close();
  return path;
}

static BLFont loadFont(const std::string& path, float size) {
  BLFontFace face;
  BLResult result = face.create_from_file(path.c_str());
  if (result != BL_SUCCESS) {
    std::cerr << "Unable to load font: " << path << "\n";
    std::exit(1);
  }

  BLFont font;
  result = font.create_from_face(face, size);
  if (result != BL_SUCCESS) {
    std::cerr << "Unable to create font from: " << path << "\n";
    std::exit(1);
  }

  return font;
}

static void drawLabeledText(BLContext& ctx,
                            const BLFont& font,
                            const BLPoint& position,
                            const char* text,
                            BLRgba32 color) {
  ctx.set_fill_style(color);
  ctx.fill_utf8_text(position, font, text);
}

static void strokeDashedHorizontalLine(BLContext& ctx,
                                       double x0,
                                       double x1,
                                       double y,
                                       std::initializer_list<double> pattern) {
  if (pattern.size() == 0) {
    ctx.stroke_line(BLLine(x0, y, x1, y));
    return;
  }

  std::vector<double> dashPattern(pattern);
  double x = x0;
  size_t index = 0;
  bool paint = true;

  while (x < x1) {
    const double length = dashPattern[index % dashPattern.size()];
    const double nextX = std::min(x + length, x1);

    if (paint && nextX > x) {
      ctx.stroke_line(BLLine(x, y, nextX, y));
    }

    x = nextX;
    paint = !paint;
    ++index;
  }
}

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  const std::vector<std::string> fontPaths = discoverFonts(options.fonts);

  if (fontPaths.size() < 3) {
#ifdef _WIN32
    const fs::path windowsFontsDir = getWindowsFontsDirectory();
    const fs::path sansExample = windowsFontsDir.empty() ? fs::path("C:/Windows/Fonts/segoeui.ttf")
                                                         : windowsFontsDir / "segoeui.ttf";
    const fs::path serifExample = windowsFontsDir.empty() ? fs::path("C:/Windows/Fonts/arial.ttf")
                                                          : windowsFontsDir / "arial.ttf";
    const fs::path monoExample = windowsFontsDir.empty() ? fs::path("C:/Windows/Fonts/consola.ttf")
                                                         : windowsFontsDir / "consola.ttf";
#endif

    std::cerr
        << "This demo needs at least three usable TTF/TTC fonts.\n"
        << "Pass them explicitly, for example:\n"
#ifdef _WIN32
        << "  " << argv[0]
        << " --font " << sansExample.string()
        << " --font " << serifExample.string()
        << " --font " << monoExample.string() << "\n";
#else
        << "  " << argv[0]
        << " --font /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        << " --font /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
        << " --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf\n";
#endif
    return 1;
  }

  if (!options.svgInput.empty()) {
    SvgRenderOptions svgOptions;
    svgOptions.inputPath = options.svgInput;
    svgOptions.outputPath = options.output;
    svgOptions.width = options.svgWidth;
    svgOptions.height = options.svgHeight;
    svgOptions.fontPaths = fontPaths;
    const auto start = std::chrono::steady_clock::now();
    const bool rendered = renderSvgToPng(svgOptions);
    const auto end = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "SVG conversion time: " << elapsedMs << " ms\n";
    return rendered ? 0 : 1;
  }

  constexpr int width = 1280;
  constexpr int height = 970;

  BLImage image(width, height, BL_FORMAT_PRGB32);
  BLContext ctx(image);

  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFFF8FAFCu));
  ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

  BLFont titleFont = loadFont(fontPaths[0], 54.0f);
  BLFont labelFont = loadFont(fontPaths[1], 24.0f);
  BLFont monoFont = loadFont(fontPaths[2], 21.0f);

  drawLabeledText(ctx, titleFont, BLPoint(52, 82), "Blend2D Shapes on Raspberry Pi",
                  BLRgba32(0xFF102A43u));
  drawLabeledText(ctx, monoFont, BLPoint(56, 122), "polygons  |  line joins  |  dash arrays  |  gradients  |  3 TTF fonts",
                  BLRgba32(0xFF52606Du));

  ctx.set_fill_style(makeLinearGradient(50, 165, 570, 390));
  BLPath mountain = makePolygon({
      BLPoint(70, 365), BLPoint(175, 200), BLPoint(265, 305),
      BLPoint(365, 170), BLPoint(530, 365),
  });
  ctx.fill_path(mountain);

  ctx.set_stroke_style(BLRgba32(0xFF0F172Au));
  ctx.set_stroke_width(5.0);
  ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
  ctx.stroke_path(mountain);

  ctx.set_fill_style(BLRgba32(0xEFFFFFFFu));
  ctx.fill_round_rect(BLRoundRect(80, 385, 430, 74, 8));
  drawLabeledText(ctx, labelFont, BLPoint(102, 432), "filled polygon with rounded stroke joins",
                  BLRgba32(0xFF243B53u));

  BLPath star = makePolygon({
      BLPoint(770, 165), BLPoint(812, 257), BLPoint(914, 268), BLPoint(838, 337),
      BLPoint(858, 438), BLPoint(770, 386), BLPoint(682, 438), BLPoint(702, 337),
      BLPoint(626, 268), BLPoint(728, 257),
  });
  ctx.set_fill_style(makeRadialGradient(770, 300, 165));
  ctx.fill_path(star);
  ctx.set_stroke_style(BLRgba32(0xFF312E81u));
  ctx.set_stroke_width(7.0);
  ctx.set_stroke_join(BL_STROKE_JOIN_BEVEL);
  ctx.stroke_path(star);

  drawLabeledText(ctx, labelFont, BLPoint(626, 485), "radial gradient star, bevel joins",
                  BLRgba32(0xFF243B53u));

  const double lineX0 = 84;
  const double lineX1 = 540;
  const std::array<double, 4> lineY = {{660, 715, 770, 835}};
  const std::array<const char*, 4> names = {{"solid round cap", "dashed butt cap", "dot-dash square cap", "wide translucent"}};

  for (size_t i = 0; i < lineY.size(); ++i) {
    ctx.set_stroke_style(i == 3 ? BLRgba32(0x886D28D9u) : BLRgba32(0xFF0E7490u));
    ctx.set_stroke_width(i == 3 ? 18.0 : 8.0);
    ctx.set_stroke_caps(i == 1 ? BL_STROKE_CAP_BUTT : (i == 2 ? BL_STROKE_CAP_SQUARE : BL_STROKE_CAP_ROUND));

    if (i == 1) {
      strokeDashedHorizontalLine(ctx, lineX0, lineX1, lineY[i], {28.0, 18.0});
    } else if (i == 2) {
      strokeDashedHorizontalLine(ctx, lineX0, lineX1, lineY[i], {4.0, 15.0, 34.0, 15.0});
    } else {
      ctx.stroke_line(BLLine(lineX0, lineY[i], lineX1, lineY[i]));
    }

    drawLabeledText(ctx, monoFont, BLPoint(585, lineY[i] + 8), names[i], BLRgba32(0xFF334E68u));
  }

  ctx.set_stroke_width(2.0);
  ctx.set_stroke_style(BLRgba32(0xFFCBD5E1u));
  ctx.stroke_round_rect(BLRoundRect(50, 620, 1180, 305, 8));

  BLPath wave;
  wave.move_to(800, 710);
  wave.cubic_to(855, 625, 940, 805, 1010, 720);
  wave.cubic_to(1075, 640, 1138, 740, 1190, 675);
  ctx.set_stroke_style(makeLinearGradient(800, 640, 1190, 820));
  ctx.set_stroke_width(11.0);
  ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
  ctx.stroke_path(wave);
  drawLabeledText(ctx, labelFont, BLPoint(820, 895), "cubic Beziers with gradient stroke",
                  BLRgba32(0xFF243B53u));

  const std::array<const char*, 3> samples = {{
      "Font sample 1: smooth vector text",
      "Font sample 2: serif/sans contrast",
      "Font sample 3: monospace measurements",
  }};

  for (size_t i = 0; i < samples.size(); ++i) {
    BLFont sampleFont = loadFont(fontPaths[i], 26.0f);
    drawLabeledText(ctx, sampleFont, BLPoint(52, 515 + static_cast<double>(i) * 34.0),
                    samples[i], BLRgba32(0xFF102A43u));
  }

  ctx.end();

  BLResult result = image.write_to_file(options.output.c_str());
  if (result != BL_SUCCESS) {
    std::cerr << "Failed to write image: " << options.output << "\n";
    return 1;
  }

  std::cout << "Wrote " << options.output << "\n";
  std::cout << "Fonts used:\n";
  for (size_t i = 0; i < 3; ++i) {
    std::cout << "  " << (i + 1) << ". " << fontPaths[i] << "\n";
  }

  return 0;
}
