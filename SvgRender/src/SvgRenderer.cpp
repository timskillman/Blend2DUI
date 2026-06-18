#include "SvgRender/SvgRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct XmlNode {
  std::string name;
  std::map<std::string, std::string> attrs;
  std::vector<XmlNode> children;
  std::string text;
};

struct CssRule {
  std::string selector;
  std::map<std::string, std::string> props;
};

enum class PaintKind { None, Color, Gradient };

struct Paint {
  PaintKind kind = PaintKind::Color;
  BLRgba32 color = BLRgba32(0xFF000000u);
  std::string gradientId;
};

struct Style {
  Paint fill;
  Paint stroke;
  double strokeWidth = 1.0;
  double opacity = 1.0;
  double fillOpacity = 1.0;
  double strokeOpacity = 1.0;
  double fontSize = 18.0;
  std::string fontFamily;
  std::vector<double> dashArray;
  BLStrokeCap lineCap = BL_STROKE_CAP_BUTT;
  BLStrokeJoin lineJoin = BL_STROKE_JOIN_MITER_CLIP;
};

struct GradientStop {
  double offset = 0.0;
  BLRgba32 color = BLRgba32(0xFF000000u);
};

struct SvgGradient {
  bool radial = false;
  double x1 = 0.0;
  double y1 = 0.0;
  double x2 = 1.0;
  double y2 = 0.0;
  double cx = 0.5;
  double cy = 0.5;
  double r = 0.5;
  bool percentUnits = true;
  std::vector<GradientStop> stops;
};

struct SvgPattern {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
  const XmlNode* node = nullptr;
};

struct SvgDocument {
  XmlNode root;
  std::vector<CssRule> cssRules;
  std::unordered_map<std::string, SvgGradient> gradients;
  std::unordered_map<std::string, SvgPattern> patterns;
  std::unordered_map<std::string, const XmlNode*> nodesById;
  std::string baseDir;
  int width = 800;
  int height = 600;
};

struct SvgRenderedImageInfo {
  int svgWidth = 0;
  int svgHeight = 0;
  int outputWidth = 0;
  int outputHeight = 0;
};

static std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static bool appendFontIfUsable(std::vector<std::string>& fonts, const std::string& path) {
  namespace fs = std::filesystem;
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

  fonts.push_back(path);
  return true;
}

static std::filesystem::path getWindowsFontsDirectory() {
#ifdef _WIN32
  const char* windowsDir = std::getenv("WINDIR");
  if (!windowsDir || !*windowsDir) {
    windowsDir = std::getenv("SystemRoot");
  }

  if (windowsDir && *windowsDir) {
    return std::filesystem::path(windowsDir) / "Fonts";
  }
#endif

  return {};
}

static std::vector<std::string> discoverFontPaths(std::vector<std::string> requested) {
  std::vector<std::string> fonts;

  for (const std::string& path : requested) {
    appendFontIfUsable(fonts, path);
  }

#ifdef _WIN32
  const std::filesystem::path windowsFontsDir = getWindowsFontsDirectory();
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

static std::vector<uint8_t> decodeBase64(const std::string& input) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::array<int, 256> table;
  table.fill(-1);
  for (size_t i = 0; i < alphabet.size(); ++i) {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }

  std::vector<uint8_t> output;
  int value = 0;
  int bits = -8;
  for (unsigned char c : input) {
    if (std::isspace(c)) continue;
    if (c == '=') break;
    const int decoded = table[c];
    if (decoded < 0) continue;
    value = (value << 6) | decoded;
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return output;
}

static std::string trim(const std::string& s) {
  size_t first = 0;
  while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) ++first;
  size_t last = s.size();
  while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) --last;
  return s.substr(first, last - first);
}

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

static bool hasSvgExtension(const std::filesystem::path& path) {
  return lower(path.extension().string()) == ".svg";
}

static bool startsWith(const std::string& s, size_t pos, const char* prefix) {
  const size_t n = std::char_traits<char>::length(prefix);
  return pos + n <= s.size() && s.compare(pos, n, prefix) == 0;
}

static std::string decodeEntities(std::string s) {
  const std::vector<std::pair<std::string, std::string>> entities = {
      {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},
  };
  for (const auto& [from, to] : entities) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
      s.replace(pos, from.size(), to);
      pos += to.size();
    }
  }
  return s;
}

class XmlParser {
 public:
  explicit XmlParser(std::string data) : data_(std::move(data)) {}

  XmlNode parse() {
    XmlNode document;
    document.name = "document";

    while (pos_ < data_.size()) {
      skipTextInto(document);
      if (pos_ >= data_.size()) break;
      if (data_[pos_] == '<') {
        if (startsWith(data_, pos_, "<!--")) {
          skipUntil("-->");
        } else if (startsWith(data_, pos_, "<?")) {
          skipUntil("?>");
        } else if (startsWith(data_, pos_, "<!")) {
          skipUntil(">");
        } else if (startsWith(data_, pos_, "</")) {
          skipUntil(">");
        } else {
          document.children.push_back(parseElement());
        }
      }
    }

    if (!document.children.empty()) {
      return document.children.front();
    }
    return document;
  }

 private:
  XmlNode parseElement() {
    XmlNode node;
    ++pos_;
    skipSpace();
    node.name = parseName();

    while (pos_ < data_.size()) {
      skipSpace();
      if (startsWith(data_, pos_, "/>")) {
        pos_ += 2;
        return node;
      }
      if (data_[pos_] == '>') {
        ++pos_;
        break;
      }

      std::string key = parseName();
      skipSpace();
      std::string value;
      if (pos_ < data_.size() && data_[pos_] == '=') {
        ++pos_;
        skipSpace();
        value = parseAttrValue();
      }
      if (!key.empty()) {
        node.attrs[key] = decodeEntities(value);
      }
    }

    while (pos_ < data_.size()) {
      if (startsWith(data_, pos_, "</")) {
        skipUntil(">");
        break;
      }
      if (startsWith(data_, pos_, "<!--")) {
        skipUntil("-->");
      } else if (data_[pos_] == '<') {
        node.children.push_back(parseElement());
      } else {
        skipTextInto(node);
      }
    }

    node.text = decodeEntities(trim(node.text));
    return node;
  }

  void skipTextInto(XmlNode& node) {
    const size_t start = pos_;
    while (pos_ < data_.size() && data_[pos_] != '<') ++pos_;
    if (pos_ > start) {
      node.text += data_.substr(start, pos_ - start);
    }
  }

  std::string parseName() {
    const size_t start = pos_;
    while (pos_ < data_.size()) {
      const char c = data_[pos_];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.') {
        ++pos_;
      } else {
        break;
      }
    }
    return data_.substr(start, pos_ - start);
  }

  std::string parseAttrValue() {
    if (pos_ >= data_.size()) return {};
    const char quote = data_[pos_];
    if (quote == '"' || quote == '\'') {
      ++pos_;
      const size_t start = pos_;
      while (pos_ < data_.size() && data_[pos_] != quote) ++pos_;
      std::string value = data_.substr(start, pos_ - start);
      if (pos_ < data_.size()) ++pos_;
      return value;
    }

    const size_t start = pos_;
    while (pos_ < data_.size() && !std::isspace(static_cast<unsigned char>(data_[pos_])) && data_[pos_] != '>') ++pos_;
    return data_.substr(start, pos_ - start);
  }

  void skipSpace() {
    while (pos_ < data_.size() && std::isspace(static_cast<unsigned char>(data_[pos_]))) ++pos_;
  }

  void skipUntil(const char* marker) {
    const size_t found = data_.find(marker, pos_);
    pos_ = found == std::string::npos ? data_.size() : found + std::char_traits<char>::length(marker);
  }

  std::string data_;
  size_t pos_ = 0;
};

static std::optional<std::string> attr(const XmlNode& node, const std::string& key) {
  auto it = node.attrs.find(key);
  if (it == node.attrs.end()) return std::nullopt;
  return it->second;
}

static double toNumber(const std::string& value, double fallback = 0.0, double percentBase = 1.0) {
  std::string s = trim(value);
  if (s.empty()) return fallback;
  char* end = nullptr;
  const double n = std::strtod(s.c_str(), &end);
  if (end && *end == '%') {
    return n * percentBase / 100.0;
  }
  return end == s.c_str() ? fallback : n;
}

static std::vector<double> parseNumberList(const std::string& s) {
  std::vector<double> values;
  const char* p = s.c_str();
  while (*p) {
    while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p;
    if (!*p) break;
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) {
      ++p;
    } else {
      values.push_back(v);
      p = end;
    }
  }
  return values;
}

static std::map<std::string, std::string> parseStyleDeclarations(const std::string& styleText) {
  std::map<std::string, std::string> props;
  size_t pos = 0;
  while (pos < styleText.size()) {
    const size_t colon = styleText.find(':', pos);
    if (colon == std::string::npos) break;
    const size_t semi = styleText.find(';', colon + 1);
    std::string key = lower(trim(styleText.substr(pos, colon - pos)));
    std::string value = trim(styleText.substr(colon + 1, semi == std::string::npos ? std::string::npos : semi - colon - 1));
    if (!key.empty() && !value.empty()) {
      props[key] = value;
    }
    if (semi == std::string::npos) break;
    pos = semi + 1;
  }
  return props;
}

static std::vector<CssRule> parseCssRules(const std::string& cssText) {
  std::vector<CssRule> rules;
  size_t pos = 0;
  while (pos < cssText.size()) {
    const size_t open = cssText.find('{', pos);
    if (open == std::string::npos) break;
    const size_t close = cssText.find('}', open + 1);
    if (close == std::string::npos) break;
    std::string selectorText = cssText.substr(pos, open - pos);
    std::string body = cssText.substr(open + 1, close - open - 1);
    std::map<std::string, std::string> props = parseStyleDeclarations(body);

    size_t sPos = 0;
    while (sPos < selectorText.size()) {
      const size_t comma = selectorText.find(',', sPos);
      std::string selector = trim(selectorText.substr(sPos, comma == std::string::npos ? std::string::npos : comma - sPos));
      if (!selector.empty() && !props.empty()) {
        rules.push_back({selector, props});
      }
      if (comma == std::string::npos) break;
      sPos = comma + 1;
    }
    pos = close + 1;
  }
  return rules;
}

static bool hasClass(const XmlNode& node, const std::string& klass) {
  auto classAttr = attr(node, "class");
  if (!classAttr) return false;
  std::istringstream in(*classAttr);
  std::string token;
  while (in >> token) {
    if (token == klass) return true;
  }
  return false;
}

static bool selectorMatches(const CssRule& rule, const XmlNode& node) {
  const std::string selector = trim(rule.selector);
  if (selector.empty()) return false;
  if (selector[0] == '.') return hasClass(node, selector.substr(1));
  if (selector[0] == '#') {
    auto id = attr(node, "id");
    return id && *id == selector.substr(1);
  }
  return lower(node.name) == lower(selector);
}

static uint8_t clampByte(double v) {
  return static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(v))));
}

static BLRgba32 withAlpha(BLRgba32 color, double alpha) {
  const uint32_t v = color.value;
  const uint8_t a = clampByte(double((v >> 24) & 0xFFu) * alpha);
  return BLRgba32((uint32_t(a) << 24) | (v & 0x00FFFFFFu));
}

static std::optional<BLRgba32> parseColor(const std::string& raw) {
  std::string s = lower(trim(raw));
  if (s.empty() || s == "none") return std::nullopt;

  static const std::unordered_map<std::string, uint32_t> named = {
      {"black", 0xFF000000u}, {"white", 0xFFFFFFFFu}, {"red", 0xFFFF0000u}, {"green", 0xFF008000u},
      {"blue", 0xFF0000FFu}, {"navy", 0xFF000080u}, {"teal", 0xFF008080u}, {"cyan", 0xFF00FFFFu},
      {"magenta", 0xFFFF00FFu}, {"yellow", 0xFFFFFF00u}, {"orange", 0xFFFFA500u}, {"purple", 0xFF800080u},
      {"gray", 0xFF808080u}, {"grey", 0xFF808080u}, {"transparent", 0x00000000u},
  };
  auto namedIt = named.find(s);
  if (namedIt != named.end()) return BLRgba32(namedIt->second);

  if (s[0] == '#') {
    std::string hex = s.substr(1);
    if (hex.size() == 3) {
      std::string expanded;
      for (char c : hex) {
        expanded.push_back(c);
        expanded.push_back(c);
      }
      hex = expanded;
    }
    if (hex.size() == 6 || hex.size() == 8) {
      uint32_t value = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
      if (hex.size() == 6) value |= 0xFF000000u;
      return BLRgba32(value);
    }
  }

  if (s.rfind("rgb(", 0) == 0 || s.rfind("rgba(", 0) == 0) {
    const size_t open = s.find('(');
    const size_t close = s.find(')', open + 1);
    if (open != std::string::npos && close != std::string::npos) {
      std::vector<double> parts = parseNumberList(s.substr(open + 1, close - open - 1));
      if (parts.size() >= 3) {
        uint8_t a = parts.size() >= 4 ? clampByte(parts[3] <= 1.0 ? parts[3] * 255.0 : parts[3]) : 255;
        return BLRgba32((uint32_t(a) << 24) | (uint32_t(clampByte(parts[0])) << 16) |
                        (uint32_t(clampByte(parts[1])) << 8) | uint32_t(clampByte(parts[2])));
      }
    }
  }

  return std::nullopt;
}

static Paint parsePaint(const std::string& value, const Paint& fallback) {
  std::string s = trim(value);
  if (s == "none") return Paint{PaintKind::None, BLRgba32(0), {}};
  if (s.rfind("url(", 0) == 0) {
    size_t hash = s.find('#');
    size_t close = s.find(')', hash);
    if (hash != std::string::npos) {
      return Paint{PaintKind::Gradient, BLRgba32(0), s.substr(hash + 1, close == std::string::npos ? std::string::npos : close - hash - 1)};
    }
  }
  if (auto color = parseColor(s)) {
    return Paint{PaintKind::Color, *color, {}};
  }
  return fallback;
}

static BLStrokeCap parseCap(const std::string& value) {
  const std::string s = lower(trim(value));
  if (s == "round") return BL_STROKE_CAP_ROUND;
  if (s == "square") return BL_STROKE_CAP_SQUARE;
  return BL_STROKE_CAP_BUTT;
}

static BLStrokeJoin parseJoin(const std::string& value) {
  const std::string s = lower(trim(value));
  if (s == "round") return BL_STROKE_JOIN_ROUND;
  if (s == "bevel") return BL_STROKE_JOIN_BEVEL;
  return BL_STROKE_JOIN_MITER_CLIP;
}

static void applyFontShorthand(Style& style, const std::string& value) {
  std::istringstream in(value);
  std::string token;
  std::string family;
  while (in >> token) {
    if (token.find("px") != std::string::npos || token.find("pt") != std::string::npos) {
      style.fontSize = std::max(1.0, toNumber(token, style.fontSize));
      continue;
    }
    if (token == "normal" || token == "bold" || token == "italic" || token == "oblique") {
      continue;
    }
    if (!family.empty()) family += " ";
    family += token;
  }
  if (!family.empty()) {
    if (!family.empty() && family.back() == ';') family.pop_back();
    family.erase(std::remove(family.begin(), family.end(), '\''), family.end());
    family.erase(std::remove(family.begin(), family.end(), '"'), family.end());
    style.fontFamily = trim(family);
  }
}

static void applyProperty(Style& style, const std::string& key, const std::string& value) {
  const std::string k = lower(trim(key));
  if (k == "fill") style.fill = parsePaint(value, style.fill);
  else if (k == "stroke") style.stroke = parsePaint(value, style.stroke);
  else if (k == "stroke-width") style.strokeWidth = std::max(0.0, toNumber(value, style.strokeWidth));
  else if (k == "opacity") style.opacity = std::max(0.0, std::min(1.0, toNumber(value, style.opacity)));
  else if (k == "fill-opacity") style.fillOpacity = std::max(0.0, std::min(1.0, toNumber(value, style.fillOpacity)));
  else if (k == "stroke-opacity") style.strokeOpacity = std::max(0.0, std::min(1.0, toNumber(value, style.strokeOpacity)));
  else if (k == "font-size") style.fontSize = std::max(1.0, toNumber(value, style.fontSize));
  else if (k == "font-family") style.fontFamily = value;
  else if (k == "stroke-dasharray") style.dashArray = lower(trim(value)) == "none" ? std::vector<double>() : parseNumberList(value);
  else if (k == "stroke-linecap") style.lineCap = parseCap(value);
  else if (k == "stroke-linejoin") style.lineJoin = parseJoin(value);
  else if (k == "font") applyFontShorthand(style, value);
}

static Style computedStyle(const SvgDocument& doc, const XmlNode& node, const Style& parent) {
  Style style = parent;
  const std::array<const char*, 13> presentationAttrs = {{
      "fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity",
      "font-size", "font-family", "font", "stroke-dasharray", "stroke-linecap", "stroke-linejoin", "color",
  }};

  for (const char* key : presentationAttrs) {
    auto v = attr(node, key);
    if (v) applyProperty(style, key, *v);
  }

  for (const CssRule& rule : doc.cssRules) {
    if (selectorMatches(rule, node)) {
      for (const auto& [key, value] : rule.props) {
        applyProperty(style, key, value);
      }
    }
  }

  auto inlineStyle = attr(node, "style");
  if (inlineStyle) {
    for (const auto& [key, value] : parseStyleDeclarations(*inlineStyle)) {
      applyProperty(style, key, value);
    }
  }

  return style;
}

class PathParser {
 public:
  explicit PathParser(std::string data) : data_(std::move(data)) {}

  BLPath parse() {
    BLPath path;
    char cmd = 0;
    double x = 0.0;
    double y = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double lastCubicX = 0.0;
    double lastCubicY = 0.0;
    bool hasLastCubic = false;

    while (skipSeparators()) {
      if (isCommand(peek())) {
        cmd = data_[pos_++];
      }
      if (!cmd) break;

      const bool relative = std::islower(static_cast<unsigned char>(cmd));
      const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

      if (op == 'Z') {
        path.close();
        x = sx;
        y = sy;
        cmd = 0;
        hasLastCubic = false;
        continue;
      }

      if (op == 'M') {
        double nx, ny;
        if (!readNumber(nx) || !readNumber(ny)) break;
        if (relative) {
          nx += x;
          ny += y;
        }
        path.move_to(nx, ny);
        x = sx = nx;
        y = sy = ny;
        cmd = relative ? 'l' : 'L';
        hasLastCubic = false;
        continue;
      }

      if (op == 'L') {
        double nx, ny;
        if (!readNumber(nx) || !readNumber(ny)) break;
        if (relative) {
          nx += x;
          ny += y;
        }
        path.line_to(nx, ny);
        x = nx;
        y = ny;
        hasLastCubic = false;
        continue;
      }

      if (op == 'H') {
        double nx;
        if (!readNumber(nx)) break;
        if (relative) nx += x;
        path.line_to(nx, y);
        x = nx;
        hasLastCubic = false;
        continue;
      }

      if (op == 'V') {
        double ny;
        if (!readNumber(ny)) break;
        if (relative) ny += y;
        path.line_to(x, ny);
        y = ny;
        hasLastCubic = false;
        continue;
      }

      if (op == 'C') {
        double x1, y1, x2, y2, nx, ny;
        if (!readNumber(x1) || !readNumber(y1) || !readNumber(x2) || !readNumber(y2) || !readNumber(nx) || !readNumber(ny)) break;
        if (relative) {
          x1 += x; y1 += y; x2 += x; y2 += y; nx += x; ny += y;
        }
        path.cubic_to(x1, y1, x2, y2, nx, ny);
        lastCubicX = x2;
        lastCubicY = y2;
        hasLastCubic = true;
        x = nx;
        y = ny;
        continue;
      }

      if (op == 'S') {
        double x2, y2, nx, ny;
        if (!readNumber(x2) || !readNumber(y2) || !readNumber(nx) || !readNumber(ny)) break;
        double x1 = hasLastCubic ? 2.0 * x - lastCubicX : x;
        double y1 = hasLastCubic ? 2.0 * y - lastCubicY : y;
        if (relative) {
          x2 += x; y2 += y; nx += x; ny += y;
        }
        path.cubic_to(x1, y1, x2, y2, nx, ny);
        lastCubicX = x2;
        lastCubicY = y2;
        hasLastCubic = true;
        x = nx;
        y = ny;
        continue;
      }

      if (op == 'Q') {
        double x1, y1, nx, ny;
        if (!readNumber(x1) || !readNumber(y1) || !readNumber(nx) || !readNumber(ny)) break;
        if (relative) {
          x1 += x; y1 += y; nx += x; ny += y;
        }
        path.quad_to(x1, y1, nx, ny);
        x = nx;
        y = ny;
        hasLastCubic = false;
        continue;
      }

      if (op == 'A') {
        double rx, ry, rot, largeArc, sweep, nx, ny;
        if (!readNumber(rx) || !readNumber(ry) || !readNumber(rot) || !readNumber(largeArc) ||
            !readNumber(sweep) || !readNumber(nx) || !readNumber(ny)) break;
        if (relative) {
          nx += x;
          ny += y;
        }
        path.elliptic_arc_to(rx, ry, rot * 3.14159265358979323846 / 180.0, largeArc != 0.0, sweep != 0.0, nx, ny);
        x = nx;
        y = ny;
        hasLastCubic = false;
        continue;
      }

      break;
    }

    return path;
  }

 private:
  bool skipSeparators() {
    while (pos_ < data_.size() && (std::isspace(static_cast<unsigned char>(data_[pos_])) || data_[pos_] == ',')) ++pos_;
    return pos_ < data_.size();
  }

  char peek() const { return pos_ < data_.size() ? data_[pos_] : '\0'; }
  static bool isCommand(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

  bool readNumber(double& out) {
    skipSeparators();
    if (pos_ >= data_.size()) return false;
    char* end = nullptr;
    out = std::strtod(data_.c_str() + pos_, &end);
    if (end == data_.c_str() + pos_) return false;
    pos_ = static_cast<size_t>(end - data_.c_str());
    return true;
  }

  std::string data_;
  size_t pos_ = 0;
};

static std::vector<BLPoint> parsePoints(const std::string& pointsText) {
  std::vector<double> values = parseNumberList(pointsText);
  std::vector<BLPoint> points;
  for (size_t i = 0; i + 1 < values.size(); i += 2) {
    points.push_back(BLPoint(values[i], values[i + 1]));
  }
  return points;
}

static BLPath makePathFromPoints(const std::vector<BLPoint>& points, bool close) {
  BLPath path;
  if (!points.empty()) {
    path.move_to(points[0].x, points[0].y);
    for (size_t i = 1; i < points.size(); ++i) {
      path.line_to(points[i].x, points[i].y);
    }
    if (close) path.close();
  }
  return path;
}

static void collectStylesAndGradients(SvgDocument& doc, const XmlNode& node) {
  if (auto id = attr(node, "id")) {
    doc.nodesById[*id] = &node;
  }

  if (lower(node.name) == "style") {
    std::vector<CssRule> rules = parseCssRules(node.text);
    doc.cssRules.insert(doc.cssRules.end(), rules.begin(), rules.end());
  }

  const std::string name = lower(node.name);
  if (name == "lineargradient" || name == "radialgradient") {
    auto id = attr(node, "id");
    if (id) {
      SvgGradient gradient;
      gradient.radial = name == "radialgradient";
      if (auto units = attr(node, "gradientUnits")) {
        gradient.percentUnits = *units != "userSpaceOnUse";
      }

      gradient.x1 = toNumber(attr(node, "x1").value_or("0%"), 0.0, 1.0);
      gradient.y1 = toNumber(attr(node, "y1").value_or("0%"), 0.0, 1.0);
      gradient.x2 = toNumber(attr(node, "x2").value_or("100%"), 1.0, 1.0);
      gradient.y2 = toNumber(attr(node, "y2").value_or("0%"), 0.0, 1.0);
      gradient.cx = toNumber(attr(node, "cx").value_or("50%"), 0.5, 1.0);
      gradient.cy = toNumber(attr(node, "cy").value_or("50%"), 0.5, 1.0);
      gradient.r = toNumber(attr(node, "r").value_or("50%"), 0.5, 1.0);

      for (const XmlNode& child : node.children) {
        if (lower(child.name) != "stop") continue;
        Style stopStyle;
        std::map<std::string, std::string> props;
        if (auto inlineStyle = attr(child, "style")) {
          props = parseStyleDeclarations(*inlineStyle);
        }
        if (auto color = attr(child, "stop-color")) props["stop-color"] = *color;
        if (auto opacity = attr(child, "stop-opacity")) props["stop-opacity"] = *opacity;

        const double offset = toNumber(attr(child, "offset").value_or("0"), 0.0, 1.0);
        BLRgba32 color = parseColor(props.count("stop-color") ? props["stop-color"] : "#000").value_or(BLRgba32(0xFF000000u));
        const double opacity = props.count("stop-opacity") ? toNumber(props["stop-opacity"], 1.0) : 1.0;
        gradient.stops.push_back({std::max(0.0, std::min(1.0, offset)), withAlpha(color, opacity)});
      }

      doc.gradients[*id] = gradient;
    }
  } else if (name == "pattern") {
    auto id = attr(node, "id");
    if (id) {
      SvgPattern pattern;
      pattern.x = toNumber(attr(node, "x").value_or("0"));
      pattern.y = toNumber(attr(node, "y").value_or("0"));
      pattern.width = toNumber(attr(node, "width").value_or("0"));
      pattern.height = toNumber(attr(node, "height").value_or("0"));
      pattern.node = &node;
      doc.patterns[*id] = pattern;
    }
  }

  for (const XmlNode& child : node.children) {
    collectStylesAndGradients(doc, child);
  }
}

class SvgRenderer {
 public:
  SvgRenderer(const SvgDocument& doc, BLContext& ctx, std::vector<std::string> fontPaths)
      : doc_(doc), ctx_(ctx), fontPaths_(std::move(fontPaths)) {}

  void render() {
    Style rootStyle;
    rootStyle.fill = Paint{PaintKind::Color, BLRgba32(0xFF000000u), {}};
    rootStyle.stroke = Paint{PaintKind::None, BLRgba32(0), {}};
    renderNode(doc_.root, rootStyle);
  }

 private:
  static void applyTransformString(BLContext& ctx, const std::string& s) {
    size_t pos = 0;
    while (pos < s.size()) {
      while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
      size_t open = s.find('(', pos);
      if (open == std::string::npos) break;
      size_t close = s.find(')', open + 1);
      if (close == std::string::npos) break;
      std::string op = lower(trim(s.substr(pos, open - pos)));
      std::vector<double> args = parseNumberList(s.substr(open + 1, close - open - 1));
      if (op == "translate" && !args.empty()) ctx.translate(args[0], args.size() > 1 ? args[1] : 0.0);
      else if (op == "scale" && !args.empty()) ctx.scale(args[0], args.size() > 1 ? args[1] : args[0]);
      else if (op == "rotate" && !args.empty()) {
        double radians = args[0] * 3.14159265358979323846 / 180.0;
        if (args.size() >= 3) ctx.rotate(radians, args[1], args[2]);
        else ctx.rotate(radians);
      } else if (op == "matrix" && args.size() >= 6) {
        ctx.apply_transform(BLMatrix2D(args[0], args[1], args[2], args[3], args[4], args[5]));
      }
      pos = close + 1;
    }
  }

  void renderNode(const XmlNode& node, const Style& inherited) {
    const std::string name = lower(node.name);
    if (name == "defs" || name == "style" || name == "lineargradient" || name == "radialgradient") return;

    Style style = computedStyle(doc_, node, inherited);
    ctx_.save();
    applyTransform(node);

    if (name == "svg" || name == "g") {
      for (const XmlNode& child : node.children) renderNode(child, style);
    } else if (name == "rect") {
      drawRect(node, style);
    } else if (name == "circle") {
      drawCircle(node, style);
    } else if (name == "ellipse") {
      drawEllipse(node, style);
    } else if (name == "line") {
      drawLine(node, style);
    } else if (name == "polyline" || name == "polygon") {
      auto pts = parsePoints(attr(node, "points").value_or(""));
      drawPolyline(pts, name == "polygon", style);
    } else if (name == "path") {
      drawPath(PathParser(attr(node, "d").value_or("")).parse(), style);
    } else if (name == "text") {
      drawText(node, style);
    } else if (name == "image") {
      drawImage(node, style);
    } else if (name == "use") {
      drawUse(node, style);
    }

    ctx_.restore();
  }

  void applyTransform(const XmlNode& node) {
    auto t = attr(node, "transform");
    if (!t) return;
    applyTransformString(ctx_, *t);
  }

  bool loadImage(const std::string& href, BLImage& image) const {
    if (href.rfind("data:", 0) == 0) {
      const size_t comma = href.find(',');
      if (comma == std::string::npos) return false;
      const std::string metadata = lower(href.substr(5, comma - 5));
      if (metadata.find(";base64") == std::string::npos) return false;

      const std::vector<uint8_t> bytes = decodeBase64(href.substr(comma + 1));
      return !bytes.empty() && image.read_from_data(bytes.data(), bytes.size()) == BL_SUCCESS;
    }

    std::filesystem::path imagePath(href);
    if (imagePath.is_relative()) {
      imagePath = std::filesystem::path(doc_.baseDir) / imagePath;
    }
    if (hasSvgExtension(imagePath)) {
      SvgRenderOptions svgOptions;
      svgOptions.inputPath = imagePath.string();
      svgOptions.fontPaths = fontPaths_;
      return renderSvgToImage(svgOptions, image);
    }
    return image.read_from_file(imagePath.string().c_str()) == BL_SUCCESS;
  }

  static std::optional<std::string> hrefValue(const XmlNode& node) {
    std::optional<std::string> href = attr(node, "href");
    if (!href) href = attr(node, "xlink:href");
    return href;
  }

  BLGradient gradientFor(const Paint& paint) const {
    auto it = doc_.gradients.find(paint.gradientId);
    if (it == doc_.gradients.end()) return BLGradient(BLLinearGradientValues(0, 0, doc_.width, 0));
    const SvgGradient& src = it->second;

    BLGradient gradient = src.radial
        ? BLGradient(BLRadialGradientValues(src.cx * doc_.width, src.cy * doc_.height,
                                            src.cx * doc_.width, src.cy * doc_.height,
                                            src.r * std::max(doc_.width, doc_.height)))
        : BLGradient(BLLinearGradientValues(src.x1 * doc_.width, src.y1 * doc_.height,
                                            src.x2 * doc_.width, src.y2 * doc_.height));
    for (const GradientStop& stop : src.stops) {
      gradient.add_stop(stop.offset, stop.color);
    }
    return gradient;
  }

  std::optional<BLPattern> patternFor(const Paint& paint) const {
    auto it = doc_.patterns.find(paint.gradientId);
    if (it == doc_.patterns.end() || !it->second.node) return std::nullopt;

    const SvgPattern& src = it->second;
    const int tileWidth = std::max(1, static_cast<int>(std::lround(src.width)));
    const int tileHeight = std::max(1, static_cast<int>(std::lround(src.height)));

    BLImage tile(tileWidth, tileHeight, BL_FORMAT_PRGB32);
    BLContext tileCtx(tile);
    tileCtx.set_comp_op(BL_COMP_OP_SRC_COPY);
    tileCtx.fill_all(BLRgba32(0x00000000u));
    tileCtx.set_comp_op(BL_COMP_OP_SRC_OVER);

    for (const XmlNode& child : src.node->children) {
      if (lower(child.name) != "image") continue;
      auto href = hrefValue(child);
      if (!href || href->empty()) continue;

      BLImage image;
      if (!loadImage(*href, image)) continue;

      const double x = toNumber(attr(child, "x").value_or("0"));
      const double y = toNumber(attr(child, "y").value_or("0"));
      auto widthAttr = attr(child, "width");
      auto heightAttr = attr(child, "height");
      const double w = widthAttr ? toNumber(*widthAttr, image.width()) : image.width();
      const double h = heightAttr ? toNumber(*heightAttr, image.height()) : image.height();
      if (w <= 0.0 || h <= 0.0) continue;

      tileCtx.save();
      if (auto transform = attr(child, "transform")) {
        applyTransformString(tileCtx, *transform);
      }
      tileCtx.blit_image(BLRect(x, y, w, h), image);
      tileCtx.restore();
    }

    tileCtx.end();
    return BLPattern(tile, BL_EXTEND_MODE_REPEAT, BLMatrix2D(1.0, 0.0, 0.0, 1.0, src.x, src.y));
  }

  void setFill(const Style& style) {
    if (style.fill.kind == PaintKind::Gradient) {
      if (auto pattern = patternFor(style.fill)) ctx_.set_fill_style(*pattern);
      else ctx_.set_fill_style(gradientFor(style.fill));
    }
    else ctx_.set_fill_style(withAlpha(style.fill.color, style.opacity * style.fillOpacity));
  }

  void setStroke(const Style& style) {
    if (style.stroke.kind == PaintKind::Gradient) ctx_.set_stroke_style(gradientFor(style.stroke));
    else ctx_.set_stroke_style(withAlpha(style.stroke.color, style.opacity * style.strokeOpacity));
    ctx_.set_stroke_width(style.strokeWidth);
    ctx_.set_stroke_caps(style.lineCap);
    ctx_.set_stroke_join(style.lineJoin);
  }

  void drawPath(const BLPath& path, const Style& style) {
    if (style.fill.kind != PaintKind::None) {
      setFill(style);
      ctx_.fill_path(path);
    }
    if (style.stroke.kind != PaintKind::None && style.strokeWidth > 0.0) {
      setStroke(style);
      if (!style.dashArray.empty()) {
        BLArray<double> dashes;
        for (double dash : style.dashArray) dashes.append(dash);
        ctx_.set_stroke_dash_array(dashes);
        ctx_.stroke_path(path);
        ctx_.set_stroke_dash_array(BLArray<double>());
      } else {
        ctx_.stroke_path(path);
      }
    }
  }

  void drawRect(const XmlNode& node, const Style& style) {
    const double x = toNumber(attr(node, "x").value_or("0"));
    const double y = toNumber(attr(node, "y").value_or("0"));
    const double w = toNumber(attr(node, "width").value_or("0"));
    const double h = toNumber(attr(node, "height").value_or("0"));
    const double rx = toNumber(attr(node, "rx").value_or("0"));
    const double ry = toNumber(attr(node, "ry").value_or(std::to_string(rx)));
    if (w <= 0.0 || h <= 0.0) return;

    BLPath path;
    if (rx > 0.0 || ry > 0.0) {
      path.add_round_rect(BLRoundRect(x, y, w, h, rx, ry));
    } else {
      path.add_rect(BLRect(x, y, w, h));
    }
    drawPath(path, style);
  }

  void drawCircle(const XmlNode& node, const Style& style) {
    const double cx = toNumber(attr(node, "cx").value_or("0"));
    const double cy = toNumber(attr(node, "cy").value_or("0"));
    const double r = toNumber(attr(node, "r").value_or("0"));
    BLPath path;
    path.add_circle(BLCircle(cx, cy, r));
    drawPath(path, style);
  }

  void drawEllipse(const XmlNode& node, const Style& style) {
    const double cx = toNumber(attr(node, "cx").value_or("0"));
    const double cy = toNumber(attr(node, "cy").value_or("0"));
    const double rx = toNumber(attr(node, "rx").value_or("0"));
    const double ry = toNumber(attr(node, "ry").value_or("0"));
    BLPath path;
    path.add_ellipse(BLEllipse(cx, cy, rx, ry));
    drawPath(path, style);
  }

  void drawLine(const XmlNode& node, const Style& style) {
    const double x1 = toNumber(attr(node, "x1").value_or("0"));
    const double y1 = toNumber(attr(node, "y1").value_or("0"));
    const double x2 = toNumber(attr(node, "x2").value_or("0"));
    const double y2 = toNumber(attr(node, "y2").value_or("0"));
    if (style.stroke.kind == PaintKind::None || style.strokeWidth <= 0.0) return;
    setStroke(style);

    strokeSegment(x1, y1, x2, y2, style);
  }

  void strokeSegment(double x1, double y1, double x2, double y2, const Style& style) {
    if (style.dashArray.empty()) {
      ctx_.stroke_line(BLLine(x1, y1, x2, y2));
      return;
    }
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double total = std::hypot(dx, dy);
    if (total <= 0.0) return;
    double dist = 0.0;
    size_t index = 0;
    bool paint = true;
    while (dist < total) {
      const double len = std::max(0.0, style.dashArray[index % style.dashArray.size()]);
      const double next = std::min(total, dist + len);
      if (paint && next > dist) {
        const double a = dist / total;
        const double b = next / total;
        ctx_.stroke_line(BLLine(x1 + dx * a, y1 + dy * a, x1 + dx * b, y1 + dy * b));
      }
      dist = next;
      paint = !paint;
      ++index;
      if (len == 0.0) break;
    }
  }

  void drawPolyline(const std::vector<BLPoint>& points, bool close, const Style& style) {
    if (points.empty()) return;

    BLPath path = makePathFromPoints(points, close);
    if (style.fill.kind != PaintKind::None && close) {
      setFill(style);
      ctx_.fill_path(path);
    }

    if (style.stroke.kind == PaintKind::None || style.strokeWidth <= 0.0) return;
    setStroke(style);

    for (size_t i = 1; i < points.size(); ++i) {
      strokeSegment(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, style);
    }
    if (close && points.size() > 1) {
      strokeSegment(points.back().x, points.back().y, points.front().x, points.front().y, style);
    }
  }

  std::string fontPathFor(const Style& style) const {
    std::string fontPath = fontPaths_.empty() ? std::string() : fontPaths_[0];
    if (lower(style.fontFamily).find("mono") != std::string::npos && fontPaths_.size() >= 3) fontPath = fontPaths_[2];
    else if (lower(style.fontFamily).find("serif") != std::string::npos && fontPaths_.size() >= 2) fontPath = fontPaths_[1];
    return fontPath;
  }

  void drawTextRun(double x, double y, const std::string& text, const Style& style) {
    if (style.fill.kind == PaintKind::None) return;
    if (text.empty()) return;

    std::string fontPath = fontPathFor(style);
    BLFontFace face;
    if (fontPath.empty() || face.create_from_file(fontPath.c_str()) != BL_SUCCESS) return;
    BLFont font;
    if (font.create_from_face(face, static_cast<float>(style.fontSize)) != BL_SUCCESS) return;

    setFill(style);
    ctx_.fill_utf8_text(BLPoint(x, y), font, text.c_str());
  }

  void drawText(const XmlNode& node, const Style& style) {
    if (!node.text.empty()) {
      const double x = toNumber(attr(node, "x").value_or("0"));
      const double y = toNumber(attr(node, "y").value_or("0"));
      drawTextRun(x, y, node.text, style);
    }

    for (const XmlNode& child : node.children) {
      if (lower(child.name) != "tspan") continue;
      Style childStyle = computedStyle(doc_, child, style);
      const double x = toNumber(attr(child, "x").value_or(attr(node, "x").value_or("0")));
      const double y = toNumber(attr(child, "y").value_or(attr(node, "y").value_or("0")));
      drawTextRun(x, y, child.text, childStyle);
    }
  }

  void drawUse(const XmlNode& node, const Style& style) {
    auto href = hrefValue(node);
    if (!href || href->empty() || (*href)[0] != '#') return;

    auto it = doc_.nodesById.find(href->substr(1));
    if (it == doc_.nodesById.end() || it->second == &node) return;

    const double x = toNumber(attr(node, "x").value_or("0"));
    const double y = toNumber(attr(node, "y").value_or("0"));
    if (x != 0.0 || y != 0.0) {
      ctx_.save();
      ctx_.translate(x, y);
      renderNode(*it->second, style);
      ctx_.restore();
    } else {
      renderNode(*it->second, style);
    }
  }

  void drawImage(const XmlNode& node, const Style& style) {
    std::optional<std::string> href = hrefValue(node);
    if (!href || href->empty()) return;

    BLImage image;
    if (!loadImage(*href, image)) {
      std::cerr << "Unable to load SVG image reference on element";
      if (auto id = attr(node, "id")) std::cerr << " #" << *id;
      std::cerr << "\n";
      return;
    }

    const double x = toNumber(attr(node, "x").value_or("0"));
    const double y = toNumber(attr(node, "y").value_or("0"));
    auto widthAttr = attr(node, "width");
    auto heightAttr = attr(node, "height");
    const double w = widthAttr ? toNumber(*widthAttr, image.width()) : image.width();
    const double h = heightAttr ? toNumber(*heightAttr, image.height()) : image.height();
    if (w <= 0.0 || h <= 0.0) return;

    const double previousAlpha = ctx_.global_alpha();
    ctx_.set_global_alpha(previousAlpha * style.opacity);
    ctx_.blit_image(BLRect(x, y, w, h), image);
    ctx_.set_global_alpha(previousAlpha);
  }

  const SvgDocument& doc_;
  BLContext& ctx_;
  std::vector<std::string> fontPaths_;
};

static bool parseSvgDocument(const std::string& svgText, SvgDocument& doc) {
  doc.root = XmlParser(svgText).parse();
  if (lower(doc.root.name) != "svg") {
    std::cerr << "SVG input does not contain an <svg> root element.\n";
    return false;
  }
  doc.width = static_cast<int>(std::max(1.0, toNumber(attr(doc.root, "width").value_or("800"), 800.0)));
  doc.height = static_cast<int>(std::max(1.0, toNumber(attr(doc.root, "height").value_or("600"), 600.0)));

  if (auto viewBox = attr(doc.root, "viewBox")) {
    std::vector<double> vb = parseNumberList(*viewBox);
    if (vb.size() == 4 && !attr(doc.root, "width")) doc.width = static_cast<int>(vb[2]);
    if (vb.size() == 4 && !attr(doc.root, "height")) doc.height = static_cast<int>(vb[3]);
  }

  collectStylesAndGradients(doc, doc.root);
  return true;
}

static bool renderSvgToImageInternal(const SvgRenderOptions& options, BLImage& image, SvgRenderedImageInfo* info) {
  const std::string svgText = readFile(options.inputPath);
  if (svgText.empty()) {
    std::cerr << "Unable to read SVG file: " << options.inputPath << "\n";
    return false;
  }

  SvgDocument doc;
  std::filesystem::path inputPath(options.inputPath);
  doc.baseDir = inputPath.has_parent_path() ? inputPath.parent_path().string() : ".";
  if (!parseSvgDocument(svgText, doc)) {
    return false;
  }

  double scale = 1.0;
  if (options.width > 0) {
    scale = static_cast<double>(options.width) / static_cast<double>(doc.width);
  } else if (options.height > 0) {
    scale = static_cast<double>(options.height) / static_cast<double>(doc.height);
  }

  const int outputWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(doc.width) * scale)));
  const int outputHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(doc.height) * scale)));

  BLImage renderedImage(outputWidth, outputHeight, BL_FORMAT_PRGB32);
  BLContext ctx(renderedImage);
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0x00000000u));
  ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
  ctx.scale(scale);

  SvgRenderer renderer(doc, ctx, discoverFontPaths(options.fontPaths));
  renderer.render();
  ctx.end();

  image = std::move(renderedImage);

  if (info) {
    info->svgWidth = doc.width;
    info->svgHeight = doc.height;
    info->outputWidth = outputWidth;
    info->outputHeight = outputHeight;
  }
  return true;
}

}  // namespace

bool renderSvgToImage(const SvgRenderOptions& options, BLImage& image) {
  return renderSvgToImageInternal(options, image, nullptr);
}

bool renderSvgToPng(const SvgRenderOptions& options) {
  BLImage image;
  SvgRenderedImageInfo info;
  if (!renderSvgToImageInternal(options, image, &info)) {
    return false;
  }

  BLResult result = image.write_to_file(options.outputPath.c_str());
  if (result != BL_SUCCESS) {
    std::cerr << "Failed to write rendered SVG bitmap: " << options.outputPath << "\n";
    return false;
  }

  std::cout << "Rendered " << options.inputPath << " -> " << options.outputPath << "\n";
  std::cout << "SVG canvas: " << info.svgWidth << " x " << info.svgHeight << "\n";
  std::cout << "Output bitmap: " << info.outputWidth << " x " << info.outputHeight << "\n";
  return true;
}
