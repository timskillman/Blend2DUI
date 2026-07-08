#include "SvgEditor/SvgSceneIO.h"

#include "Utility.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace SvgEditor {
namespace {

using Blend2DUI::lower;
using Blend2DUI::trim;

struct CssRule {
  std::string selector;
  std::map<std::string, std::string> props;
};

struct ParseState {
  SvgDocument document;
  std::vector<CssRule> cssRules;
  std::unordered_map<std::string, const XmlNode*> nodesById;
  std::vector<std::string> fontPaths;
};

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool appendFontIfUsable(std::vector<std::string>& fonts, const std::string& path) {
  namespace fs = std::filesystem;
  if (!fs::is_regular_file(path)) return false;
  if (std::find(fonts.begin(), fonts.end(), path) != fonts.end()) return false;
  BLFontFace face;
  if (face.create_from_file(path.c_str()) != BL_SUCCESS) return false;
  fonts.push_back(path);
  return true;
}

std::filesystem::path getWindowsFontsDirectory() {
#ifdef _WIN32
  const char* windowsDir = std::getenv("WINDIR");
  if (!windowsDir || !*windowsDir) windowsDir = std::getenv("SystemRoot");
  if (windowsDir && *windowsDir) return std::filesystem::path(windowsDir) / "Fonts";
#endif
  return {};
}

std::vector<std::string> discoverFontPaths() {
  std::vector<std::string> fonts;
#ifdef _WIN32
  const std::filesystem::path windowsFontsDir = getWindowsFontsDirectory();
  if (!windowsFontsDir.empty()) {
    const std::array<const char*, 8> candidates = {{
        "segoeui.ttf",
        "arial.ttf",
        "calibri.ttf",
        "tahoma.ttf",
        "verdana.ttf",
        "times.ttf",
        "georgia.ttf",
        "consola.ttf",
    }};
    for (const char* candidate : candidates) {
      appendFontIfUsable(fonts, (windowsFontsDir / candidate).string());
      if (fonts.size() >= 3) break;
    }
  }
#endif
  const std::array<const char*, 8> fallback = {{
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSerif-Regular.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf",
  }};
  for (const char* candidate : fallback) {
    appendFontIfUsable(fonts, candidate);
    if (fonts.size() >= 3) break;
  }
  return fonts;
}

bool startsWith(const std::string& text, size_t pos, const char* prefix) {
  const size_t size = std::char_traits<char>::length(prefix);
  return pos + size <= text.size() && text.compare(pos, size, prefix) == 0;
}

std::string decodeEntities(std::string value) {
  const std::vector<std::pair<std::string, std::string>> entities = {
      {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},
  };
  for (const auto& [from, to] : entities) {
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
      value.replace(pos, from.size(), to);
      pos += to.size();
    }
  }
  return value;
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
        if (startsWith(data_, pos_, "<!--")) skipUntil("-->");
        else if (startsWith(data_, pos_, "<?")) skipUntil("?>");
        else if (startsWith(data_, pos_, "<!")) skipUntil(">");
        else if (startsWith(data_, pos_, "</")) skipUntil(">");
        else document.children.push_back(parseElement());
      }
    }
    return document.children.empty() ? document : document.children.front();
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
      if (!key.empty()) node.attrs[key] = decodeEntities(value);
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
    if (pos_ > start) node.text += data_.substr(start, pos_ - start);
  }

  std::string parseName() {
    const size_t start = pos_;
    while (pos_ < data_.size()) {
      const char c = data_[pos_];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.') ++pos_;
      else break;
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

std::optional<std::string> attr(const XmlNode& node, const std::string& key) {
  auto it = node.attrs.find(key);
  if (it == node.attrs.end()) return std::nullopt;
  return it->second;
}

double toNumber(const std::string& value, double fallback = 0.0, double percentBase = 1.0) {
  std::string text = trim(value);
  if (text.empty()) return fallback;
  char* end = nullptr;
  const double result = std::strtod(text.c_str(), &end);
  if (end && *end == '%') return result * percentBase / 100.0;
  return end == text.c_str() ? fallback : result;
}

std::vector<double> parseNumberList(const std::string& text) {
  std::vector<double> values;
  const char* cursor = text.c_str();
  while (*cursor) {
    while (*cursor && (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ',')) ++cursor;
    if (!*cursor) break;
    char* end = nullptr;
    const double value = std::strtod(cursor, &end);
    if (end == cursor) {
      ++cursor;
    } else {
      values.push_back(value);
      cursor = end;
    }
  }
  return values;
}

std::map<std::string, std::string> parseStyleDeclarations(const std::string& text) {
  std::map<std::string, std::string> props;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t colon = text.find(':', pos);
    if (colon == std::string::npos) break;
    const size_t semi = text.find(';', colon + 1);
    const std::string key = lower(trim(text.substr(pos, colon - pos)));
    const std::string value = trim(text.substr(colon + 1, semi == std::string::npos ? std::string::npos : semi - colon - 1));
    if (!key.empty() && !value.empty()) props[key] = value;
    if (semi == std::string::npos) break;
    pos = semi + 1;
  }
  return props;
}

std::vector<CssRule> parseCssRules(const std::string& cssText) {
  std::vector<CssRule> rules;
  size_t pos = 0;
  while (pos < cssText.size()) {
    const size_t open = cssText.find('{', pos);
    if (open == std::string::npos) break;
    const size_t close = cssText.find('}', open + 1);
    if (close == std::string::npos) break;
    const std::string selectors = cssText.substr(pos, open - pos);
    const std::map<std::string, std::string> props = parseStyleDeclarations(cssText.substr(open + 1, close - open - 1));
    size_t selectorPos = 0;
    while (selectorPos < selectors.size()) {
      const size_t comma = selectors.find(',', selectorPos);
      const std::string selector = trim(selectors.substr(selectorPos, comma == std::string::npos ? std::string::npos : comma - selectorPos));
      if (!selector.empty() && !props.empty()) rules.push_back(CssRule{selector, props});
      if (comma == std::string::npos) break;
      selectorPos = comma + 1;
    }
    pos = close + 1;
  }
  return rules;
}

bool hasClass(const XmlNode& node, const std::string& klass) {
  auto classAttr = attr(node, "class");
  if (!classAttr) return false;
  std::istringstream in(*classAttr);
  std::string token;
  while (in >> token) {
    if (token == klass) return true;
  }
  return false;
}

bool selectorMatches(const CssRule& rule, const XmlNode& node) {
  const std::string selector = trim(rule.selector);
  if (selector.empty()) return false;
  if (selector[0] == '.') return hasClass(node, selector.substr(1));
  if (selector[0] == '#') {
    auto id = attr(node, "id");
    return id && *id == selector.substr(1);
  }
  return lower(node.name) == lower(selector);
}

uint8_t clampByte(double value) {
  return static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(value))));
}

std::optional<BLRgba32> parseColor(const std::string& raw) {
  std::string text = lower(trim(raw));
  if (text.empty() || text == "none") return std::nullopt;

  static const std::unordered_map<std::string, uint32_t> named = {
      {"black", 0xFF000000u}, {"white", 0xFFFFFFFFu}, {"red", 0xFFFF0000u}, {"green", 0xFF008000u},
      {"blue", 0xFF0000FFu}, {"navy", 0xFF000080u}, {"teal", 0xFF008080u}, {"cyan", 0xFF00FFFFu},
      {"magenta", 0xFFFF00FFu}, {"yellow", 0xFFFFFF00u}, {"orange", 0xFFFFA500u}, {"purple", 0xFF800080u},
      {"gray", 0xFF808080u}, {"grey", 0xFF808080u}, {"transparent", 0x00000000u},
  };
  auto namedIt = named.find(text);
  if (namedIt != named.end()) return BLRgba32(namedIt->second);

  if (text[0] == '#') {
    std::string hex = text.substr(1);
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

  if (text.rfind("rgb(", 0) == 0 || text.rfind("rgba(", 0) == 0) {
    const size_t open = text.find('(');
    const size_t close = text.find(')', open + 1);
    if (open != std::string::npos && close != std::string::npos) {
      const std::vector<double> parts = parseNumberList(text.substr(open + 1, close - open - 1));
      if (parts.size() >= 3) {
        const uint8_t a = parts.size() >= 4 ? clampByte(parts[3] <= 1.0 ? parts[3] * 255.0 : parts[3]) : 255;
        return BLRgba32((uint32_t(a) << 24) |
                        (uint32_t(clampByte(parts[0])) << 16) |
                        (uint32_t(clampByte(parts[1])) << 8) |
                        uint32_t(clampByte(parts[2])));
      }
    }
  }
  return std::nullopt;
}

Paint parsePaint(const std::string& value, const ParseState& state) {
  const std::string text = trim(value);
  if (text == "none") return Paint{PaintKind::None, BLRgba32(0), {}};
  if (text.rfind("url(", 0) == 0) {
    const size_t hash = text.find('#');
    const size_t close = text.find(')', hash);
    if (hash != std::string::npos) {
      const std::string refId = text.substr(hash + 1, close == std::string::npos ? std::string::npos : close - hash - 1);
      if (state.document.patterns().count(refId)) return Paint{PaintKind::PatternRef, BLRgba32(0), refId};
      return Paint{PaintKind::GradientRef, BLRgba32(0), refId};
    }
  }
  if (auto color = parseColor(text)) return Paint{PaintKind::Color, *color, {}};
  return Paint{PaintKind::None, BLRgba32(0), {}};
}

BLStrokeCap parseCap(const std::string& value) {
  const std::string text = lower(trim(value));
  if (text == "round") return BL_STROKE_CAP_ROUND;
  if (text == "square") return BL_STROKE_CAP_SQUARE;
  return BL_STROKE_CAP_BUTT;
}

BLStrokeJoin parseJoin(const std::string& value) {
  const std::string text = lower(trim(value));
  if (text == "round") return BL_STROKE_JOIN_ROUND;
  if (text == "bevel") return BL_STROKE_JOIN_BEVEL;
  return BL_STROKE_JOIN_MITER_CLIP;
}

void applyFontShorthand(Style& style, const std::string& value) {
  std::istringstream in(value);
  std::string token;
  std::string family;
  while (in >> token) {
    if (token.find("px") != std::string::npos || token.find("pt") != std::string::npos) {
      style.fontSize = std::max(1.0, toNumber(token, style.fontSize));
      continue;
    }
    if (token == "normal" || token == "bold" || token == "italic" || token == "oblique") continue;
    if (!family.empty()) family += " ";
    family += token;
  }
  if (!family.empty()) {
    family.erase(std::remove(family.begin(), family.end(), '\''), family.end());
    family.erase(std::remove(family.begin(), family.end(), '"'), family.end());
    style.fontFamily = trim(family);
  }
}

void applyProperty(Style& style, const std::string& key, const std::string& value, const ParseState& state) {
  const std::string lowerKey = lower(trim(key));
  if (lowerKey == "fill") style.fill = parsePaint(value, state);
  else if (lowerKey == "stroke") style.stroke = parsePaint(value, state);
  else if (lowerKey == "stroke-width") style.strokeWidth = std::max(0.0, toNumber(value, style.strokeWidth));
  else if (lowerKey == "opacity") style.opacity = std::clamp(toNumber(value, style.opacity), 0.0, 1.0);
  else if (lowerKey == "fill-opacity") style.fillOpacity = std::clamp(toNumber(value, style.fillOpacity), 0.0, 1.0);
  else if (lowerKey == "stroke-opacity") style.strokeOpacity = std::clamp(toNumber(value, style.strokeOpacity), 0.0, 1.0);
  else if (lowerKey == "font-size") style.fontSize = std::max(1.0, toNumber(value, style.fontSize));
  else if (lowerKey == "font-family") style.fontFamily = value;
  else if (lowerKey == "stroke-dasharray") style.dashArray = lower(trim(value)) == "none" ? std::vector<double>() : parseNumberList(value);
  else if (lowerKey == "stroke-linecap") style.lineCap = parseCap(value);
  else if (lowerKey == "stroke-linejoin") style.lineJoin = parseJoin(value);
  else if (lowerKey == "font") applyFontShorthand(style, value);
}

Style computedStyle(const ParseState& state, const XmlNode& node, const Style& parent) {
  Style style = parent;
  const std::array<const char*, 12> attrs = {{
      "fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity",
      "font-size", "font-family", "font", "stroke-dasharray", "stroke-linecap", "stroke-linejoin",
  }};
  for (const char* key : attrs) {
    if (auto value = attr(node, key)) applyProperty(style, key, *value, state);
  }
  for (const CssRule& rule : state.cssRules) {
    if (!selectorMatches(rule, node)) continue;
    for (const auto& [key, value] : rule.props) applyProperty(style, key, value, state);
  }
  if (auto inlineStyle = attr(node, "style")) {
    for (const auto& [key, value] : parseStyleDeclarations(*inlineStyle)) {
      applyProperty(style, key, value, state);
    }
  }
  return style;
}

void collectStylesAndDefs(ParseState& state, const XmlNode& node) {
  if (auto id = attr(node, "id")) {
    state.nodesById[*id] = &node;
  }

  if (lower(node.name) == "style") {
    const std::vector<CssRule> rules = parseCssRules(node.text);
    state.cssRules.insert(state.cssRules.end(), rules.begin(), rules.end());
  }

  const std::string name = lower(node.name);
  if (name == "lineargradient" || name == "radialgradient") {
    auto id = attr(node, "id");
    if (id) {
      GradientDef gradient;
      gradient.id = *id;
      gradient.radial = name == "radialgradient";
      gradient.percentUnits = attr(node, "gradientUnits").value_or("objectBoundingBox") != "userSpaceOnUse";
      gradient.x1 = toNumber(attr(node, "x1").value_or("0%"), 0.0, 1.0);
      gradient.y1 = toNumber(attr(node, "y1").value_or("0%"), 0.0, 1.0);
      gradient.x2 = toNumber(attr(node, "x2").value_or("100%"), 1.0, 1.0);
      gradient.y2 = toNumber(attr(node, "y2").value_or("0%"), 0.0, 1.0);
      gradient.cx = toNumber(attr(node, "cx").value_or("50%"), 0.5, 1.0);
      gradient.cy = toNumber(attr(node, "cy").value_or("50%"), 0.5, 1.0);
      gradient.r = toNumber(attr(node, "r").value_or("50%"), 0.5, 1.0);

      for (const XmlNode& child : node.children) {
        if (lower(child.name) != "stop") continue;
        std::map<std::string, std::string> props;
        if (auto styleAttr = attr(child, "style")) props = parseStyleDeclarations(*styleAttr);
        if (auto color = attr(child, "stop-color")) props["stop-color"] = *color;
        if (auto opacity = attr(child, "stop-opacity")) props["stop-opacity"] = *opacity;
        const double offset = toNumber(attr(child, "offset").value_or("0"), 0.0, 1.0);
        const BLRgba32 color = parseColor(props.count("stop-color") ? props["stop-color"] : "#000").value_or(BLRgba32(0xFF000000u));
        const double opacity = props.count("stop-opacity") ? toNumber(props["stop-opacity"], 1.0) : 1.0;
        const uint32_t value = color.value;
        const uint8_t alpha = clampByte(static_cast<double>((value >> 24) & 0xFFu) * opacity);
        gradient.stops.push_back(GradientStop{std::clamp(offset, 0.0, 1.0),
                                              BLRgba32((uint32_t(alpha) << 24) | (value & 0x00FFFFFFu))});
      }
      state.document.gradients()[*id] = gradient;
    }
  } else if (name == "pattern") {
    auto id = attr(node, "id");
    if (id) {
      PatternDef pattern;
      pattern.id = *id;
      pattern.x = toNumber(attr(node, "x").value_or("0"));
      pattern.y = toNumber(attr(node, "y").value_or("0"));
      pattern.width = toNumber(attr(node, "width").value_or("0"));
      pattern.height = toNumber(attr(node, "height").value_or("0"));
      pattern.rawNode = node;
      state.document.patterns()[*id] = pattern;
    }
  }

  for (const XmlNode& child : node.children) {
    collectStylesAndDefs(state, child);
  }
}

BLMatrix2D parseTransformString(const std::string& text) {
  BLMatrix2D matrix = BLMatrix2D::make_identity();
  size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    const size_t open = text.find('(', pos);
    if (open == std::string::npos) break;
    const size_t close = text.find(')', open + 1);
    if (close == std::string::npos) break;
    const std::string op = lower(trim(text.substr(pos, open - pos)));
    const std::vector<double> args = parseNumberList(text.substr(open + 1, close - open - 1));
    if (op == "translate" && !args.empty()) matrix.transform(BLMatrix2D::make_translation(args[0], args.size() > 1 ? args[1] : 0.0));
    else if (op == "scale" && !args.empty()) matrix.transform(BLMatrix2D::make_scaling(args[0], args.size() > 1 ? args[1] : args[0]));
    else if (op == "rotate" && !args.empty()) {
      const double radians = args[0] * 3.14159265358979323846 / 180.0;
      matrix.transform(args.size() >= 3 ? BLMatrix2D::make_rotation(radians, args[1], args[2]) : BLMatrix2D::make_rotation(radians));
    } else if (op == "matrix" && args.size() >= 6) {
      matrix.transform(BLMatrix2D(args[0], args[1], args[2], args[3], args[4], args[5]));
    }
    pos = close + 1;
  }
  return matrix;
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
      if (isCommand(peek())) cmd = data_[pos_++];
      if (!cmd) break;

      const bool relative = std::islower(static_cast<unsigned char>(cmd)) != 0;
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
        if (!readNumber(x1) || !readNumber(y1) || !readNumber(x2) || !readNumber(y2) ||
            !readNumber(nx) || !readNumber(ny)) break;
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
        if (!readNumber(rx) || !readNumber(ry) || !readNumber(rot) ||
            !readNumber(largeArc) || !readNumber(sweep) || !readNumber(nx) || !readNumber(ny)) break;
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

std::vector<BLPoint> parsePoints(const std::string& text) {
  std::vector<BLPoint> points;
  const std::vector<double> numbers = parseNumberList(text);
  for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
    points.push_back(BLPoint(numbers[i], numbers[i + 1]));
  }
  return points;
}

BLPath makePolylinePath(const std::vector<BLPoint>& points, bool close) {
  BLPath path;
  if (points.empty()) return path;
  path.move_to(points.front().x, points.front().y);
  for (size_t i = 1; i < points.size(); ++i) {
    path.line_to(points[i].x, points[i].y);
  }
  if (close) path.close();
  return path;
}

std::string fontPathFor(const Style& style, const std::vector<std::string>& fontPaths) {
  std::string fontPath = fontPaths.empty() ? std::string() : fontPaths.front();
  if (lower(style.fontFamily).find("mono") != std::string::npos && fontPaths.size() >= 3) fontPath = fontPaths[2];
  else if (lower(style.fontFamily).find("serif") != std::string::npos && fontPaths.size() >= 2) fontPath = fontPaths[1];
  return fontPath;
}

Node makePathNode(ParseState& state,
                  const XmlNode& xmlNode,
                  const Style& style,
                  BLPath path,
                  const BLMatrix2D& transform) {
  Node node;
  node.id = attr(xmlNode, "id").value_or(state.document.createNodeId("path"));
  node.type = NodeType::Path;
  node.transform = transform;
  node.style = style;
  node.path = std::move(path);
  node.commands = SvgDocument::pathToCommands(node.path);
  node.pointEditable = true;
  return node;
}

std::vector<Node> convertNode(ParseState& state,
                              const XmlNode& node,
                              const Style& inheritedStyle,
                              const BLMatrix2D& extraTransform);

std::vector<Node> convertTextNode(ParseState& state,
                                  const XmlNode& node,
                                  const Style& inheritedStyle,
                                  const BLMatrix2D& transform) {
  std::vector<Node> nodes;
  const Style style = computedStyle(state, node, inheritedStyle);
  const std::string fontPath = fontPathFor(style, state.fontPaths);
  BLFontFace face;
  if (fontPath.empty() || face.create_from_file(fontPath.c_str()) != BL_SUCCESS) return nodes;
  BLFont font;
  if (font.create_from_face(face, static_cast<float>(style.fontSize)) != BL_SUCCESS) return nodes;

  auto appendRun = [&](const XmlNode& textNode, const Style& runStyle, const std::string& text) {
    if (text.empty()) return;
    const double x = toNumber(attr(textNode, "x").value_or(attr(node, "x").value_or("0")));
    const double y = toNumber(attr(textNode, "y").value_or(attr(node, "y").value_or("0")));
    BLGlyphBuffer glyphs;
    glyphs.set_utf8_text(text.c_str(), text.size());
    font.shape(glyphs);
    BLPath outline;
    font.get_glyph_run_outlines(glyphs.glyph_run(), BLMatrix2D::make_translation(x, y), outline);
    if (outline.size() == 0) return;
    nodes.push_back(makePathNode(state, textNode, runStyle, std::move(outline), transform));
  };

  appendRun(node, style, node.text);
  for (const XmlNode& child : node.children) {
    if (lower(child.name) != "tspan") continue;
    appendRun(child, computedStyle(state, child, style), child.text);
  }
  return nodes;
}

std::vector<Node> convertNode(ParseState& state,
                              const XmlNode& node,
                              const Style& inheritedStyle,
                              const BLMatrix2D& extraTransform) {
  const std::string name = lower(node.name);
  if (name == "defs" || name == "style" || name == "lineargradient" || name == "radialgradient" || name == "pattern") {
    return {};
  }

  const Style style = computedStyle(state, node, inheritedStyle);
  BLMatrix2D transform = extraTransform;
  if (auto transformAttr = attr(node, "transform")) {
    transform = SvgDocument::compose(transform, parseTransformString(*transformAttr));
  }

  if (name == "svg") {
    std::vector<Node> nodes;
    for (const XmlNode& child : node.children) {
      std::vector<Node> converted = convertNode(state, child, style, transform);
      nodes.insert(nodes.end(),
                   std::make_move_iterator(converted.begin()),
                   std::make_move_iterator(converted.end()));
    }
    return nodes;
  }

  if (name == "g") {
    Node group;
    group.id = attr(node, "id").value_or(state.document.createNodeId("group"));
    group.type = NodeType::Group;
    group.transform = transform;
    for (const XmlNode& child : node.children) {
      std::vector<Node> converted = convertNode(state, child, style, BLMatrix2D::make_identity());
      group.children.insert(group.children.end(),
                            std::make_move_iterator(converted.begin()),
                            std::make_move_iterator(converted.end()));
    }
    if (group.children.empty()) return {};
    return {std::move(group)};
  }

  if (name == "path") {
    return {makePathNode(state,
                         node,
                         style,
                         PathParser(attr(node, "d").value_or("")).parse(),
                         transform)};
  }

  if (name == "rect") {
    const double x = toNumber(attr(node, "x").value_or("0"));
    const double y = toNumber(attr(node, "y").value_or("0"));
    const double w = toNumber(attr(node, "width").value_or("0"));
    const double h = toNumber(attr(node, "height").value_or("0"));
    const double rx = toNumber(attr(node, "rx").value_or("0"));
    const double ry = toNumber(attr(node, "ry").value_or(std::to_string(rx)));
    if (w <= 0.0 || h <= 0.0) return {};
    BLPath path;
    if (rx > 0.0 || ry > 0.0) path.add_round_rect(BLRoundRect(x, y, w, h, rx, ry));
    else path.add_rect(BLRect(x, y, w, h));
    return {makePathNode(state, node, style, std::move(path), transform)};
  }

  if (name == "circle") {
    BLPath path;
    path.add_circle(BLCircle(toNumber(attr(node, "cx").value_or("0")),
                             toNumber(attr(node, "cy").value_or("0")),
                             toNumber(attr(node, "r").value_or("0"))));
    return {makePathNode(state, node, style, std::move(path), transform)};
  }

  if (name == "ellipse") {
    BLPath path;
    path.add_ellipse(BLEllipse(toNumber(attr(node, "cx").value_or("0")),
                               toNumber(attr(node, "cy").value_or("0")),
                               toNumber(attr(node, "rx").value_or("0")),
                               toNumber(attr(node, "ry").value_or("0"))));
    return {makePathNode(state, node, style, std::move(path), transform)};
  }

  if (name == "line") {
    BLPath path;
    path.move_to(toNumber(attr(node, "x1").value_or("0")), toNumber(attr(node, "y1").value_or("0")));
    path.line_to(toNumber(attr(node, "x2").value_or("0")), toNumber(attr(node, "y2").value_or("0")));
    return {makePathNode(state, node, style, std::move(path), transform)};
  }

  if (name == "polyline" || name == "polygon") {
    return {makePathNode(state,
                         node,
                         style,
                         makePolylinePath(parsePoints(attr(node, "points").value_or("")), name == "polygon"),
                         transform)};
  }

  if (name == "text") {
    return convertTextNode(state, node, inheritedStyle, transform);
  }

  if (name == "image") {
    Node imageNode;
    imageNode.id = attr(node, "id").value_or(state.document.createNodeId("image"));
    imageNode.type = NodeType::Image;
    imageNode.transform = transform;
    imageNode.style = style;
    imageNode.image.href = attr(node, "href").value_or(attr(node, "xlink:href").value_or(""));
    imageNode.image.x = toNumber(attr(node, "x").value_or("0"));
    imageNode.image.y = toNumber(attr(node, "y").value_or("0"));
    imageNode.image.width = toNumber(attr(node, "width").value_or("0"));
    imageNode.image.height = toNumber(attr(node, "height").value_or("0"));
    return imageNode.image.href.empty() ? std::vector<Node>() : std::vector<Node>{std::move(imageNode)};
  }

  if (name == "use") {
    const std::string href = attr(node, "href").value_or(attr(node, "xlink:href").value_or(""));
    if (href.empty() || href[0] != '#') return {};
    auto it = state.nodesById.find(href.substr(1));
    if (it == state.nodesById.end()) return {};
    BLMatrix2D useTransform = transform;
    useTransform = SvgDocument::compose(useTransform,
                                        BLMatrix2D::make_translation(toNumber(attr(node, "x").value_or("0")),
                                                                     toNumber(attr(node, "y").value_or("0"))));
    return convertNode(state, *it->second, style, useTransform);
  }

  return {};
}

bool parseSvgDocument(const std::string& text, ParseState& state) {
  const XmlNode root = XmlParser(text).parse();
  if (lower(root.name) != "svg") return false;

  state.document.resetToA4Landscape();
  state.fontPaths = discoverFontPaths();

  double width = toNumber(attr(root, "width").value_or("297"), 297.0);
  double height = toNumber(attr(root, "height").value_or("210"), 210.0);
  double viewBoxX = 0.0;
  double viewBoxY = 0.0;
  if (auto viewBox = attr(root, "viewBox")) {
    const std::vector<double> values = parseNumberList(*viewBox);
    if (values.size() == 4) {
      viewBoxX = values[0];
      viewBoxY = values[1];
      width = values[2];
      height = values[3];
    }
  }
  state.document.setCanvas(viewBoxX, viewBoxY, width, height);

  XmlNode rootCopy = root;
  collectStylesAndDefs(state, rootCopy);

  Style rootStyle;
  rootStyle.fill = Paint{PaintKind::Color, BLRgba32(0xFF000000u), {}};
  rootStyle.stroke = Paint{PaintKind::None, BLRgba32(0), {}};
  state.document.root().children = convertNode(state, rootCopy, rootStyle, BLMatrix2D::make_identity());
  return true;
}

std::string escapeXml(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&apos;"; break;
      default: escaped.push_back(c); break;
    }
  }
  return escaped;
}

std::string hexColor(uint32_t value) {
  std::ostringstream out;
  out << '#'
      << std::hex
      << std::uppercase
      << std::setfill('0')
      << std::setw(2) << ((value >> 16) & 0xFFu)
      << std::setw(2) << ((value >> 8) & 0xFFu)
      << std::setw(2) << (value & 0xFFu);
  return out.str();
}

void writeIndent(std::ostringstream& out, int depth) {
  for (int i = 0; i < depth; ++i) out << "  ";
}

void writeGradient(std::ostringstream& out, const GradientDef& gradient, int depth) {
  writeIndent(out, depth);
  out << '<' << (gradient.radial ? "radialGradient" : "linearGradient")
      << " id=\"" << escapeXml(gradient.id) << '"';
  if (!gradient.radial) {
    out << " x1=\"" << gradient.x1 * 100.0 << "%\""
        << " y1=\"" << gradient.y1 * 100.0 << "%\""
        << " x2=\"" << gradient.x2 * 100.0 << "%\""
        << " y2=\"" << gradient.y2 * 100.0 << "%\"";
  } else {
    out << " cx=\"" << gradient.cx * 100.0 << "%\""
        << " cy=\"" << gradient.cy * 100.0 << "%\""
        << " r=\"" << gradient.r * 100.0 << "%\"";
  }
  out << ">\n";
  for (const GradientStop& stop : gradient.stops) {
    const uint32_t value = stop.color.value;
    const double alpha = static_cast<double>((value >> 24) & 0xFFu) / 255.0;
    writeIndent(out, depth + 1);
    out << "<stop offset=\"" << stop.offset * 100.0 << "%\" stop-color=\"" << hexColor(value)
        << "\" stop-opacity=\"" << alpha << "\"/>\n";
  }
  writeIndent(out, depth);
  out << "</" << (gradient.radial ? "radialGradient" : "linearGradient") << ">\n";
}

void writeXmlNode(std::ostringstream& out, const XmlNode& node, int depth) {
  writeIndent(out, depth);
  out << '<' << node.name;
  for (const auto& [key, value] : node.attrs) {
    out << ' ' << key << "=\"" << escapeXml(value) << '"';
  }
  if (node.children.empty() && node.text.empty()) {
    out << "/>\n";
    return;
  }
  out << '>';
  if (!node.text.empty()) out << escapeXml(node.text);
  if (!node.children.empty()) out << '\n';
  for (const XmlNode& child : node.children) {
    writeXmlNode(out, child, depth + 1);
  }
  if (!node.children.empty()) writeIndent(out, depth);
  out << "</" << node.name << ">\n";
}

std::string paintToSvg(const Paint& paint) {
  switch (paint.kind) {
    case PaintKind::None:
      return "none";
    case PaintKind::Color:
      return hexColor(paint.color.value);
    case PaintKind::GradientRef:
    case PaintKind::PatternRef:
      return "url(#" + paint.refId + ")";
  }
  return "none";
}

std::string pathData(const std::vector<PathCommand>& commands) {
  std::ostringstream out;
  bool first = true;
  for (const PathCommand& command : commands) {
    if (!first) out << ' ';
    first = false;
    switch (command.type) {
      case PathCommand::Type::MoveTo:
        out << 'M' << command.p1.x << ' ' << command.p1.y;
        break;
      case PathCommand::Type::LineTo:
        out << 'L' << command.p1.x << ' ' << command.p1.y;
        break;
      case PathCommand::Type::QuadTo:
        out << 'Q' << command.p1.x << ' ' << command.p1.y << ' ' << command.p2.x << ' ' << command.p2.y;
        break;
      case PathCommand::Type::CubicTo:
        out << 'C' << command.p1.x << ' ' << command.p1.y << ' '
            << command.p2.x << ' ' << command.p2.y << ' '
            << command.p3.x << ' ' << command.p3.y;
        break;
      case PathCommand::Type::Close:
        out << 'Z';
        break;
    }
  }
  return out.str();
}

void writeNode(std::ostringstream& out, const Node& node, int depth) {
  if (node.type == NodeType::Group) {
    writeIndent(out, depth);
    out << "<g";
    if (!SvgDocument::isIdentity(node.transform)) {
      out << " transform=\"" << escapeXml(SvgDocument::matrixToSvg(node.transform)) << '"';
    }
    out << ">\n";
    for (const Node& child : node.children) writeNode(out, child, depth + 1);
    writeIndent(out, depth);
    out << "</g>\n";
    return;
  }

  if (node.type == NodeType::Path) {
    const double fillAlpha = static_cast<double>((node.style.fill.color.value >> 24) & 0xFFu) / 255.0;
    const double strokeAlpha = static_cast<double>((node.style.stroke.color.value >> 24) & 0xFFu) / 255.0;
    writeIndent(out, depth);
    out << "<path d=\"" << escapeXml(pathData(node.commands)) << '"';
    if (!SvgDocument::isIdentity(node.transform)) {
      out << " transform=\"" << escapeXml(SvgDocument::matrixToSvg(node.transform)) << '"';
    }
    out << " fill=\"" << paintToSvg(node.style.fill) << '"'
        << " stroke=\"" << paintToSvg(node.style.stroke) << '"'
        << " stroke-width=\"" << node.style.strokeWidth << '"';
    if (node.style.opacity != 1.0) out << " opacity=\"" << node.style.opacity << '"';
    if (node.style.fill.kind == PaintKind::Color && fillAlpha != 1.0) out << " fill-opacity=\"" << fillAlpha * node.style.fillOpacity << '"';
    else if (node.style.fillOpacity != 1.0) out << " fill-opacity=\"" << node.style.fillOpacity << '"';
    if (node.style.stroke.kind == PaintKind::Color && strokeAlpha != 1.0) out << " stroke-opacity=\"" << strokeAlpha * node.style.strokeOpacity << '"';
    else if (node.style.strokeOpacity != 1.0) out << " stroke-opacity=\"" << node.style.strokeOpacity << '"';
    if (!node.style.dashArray.empty()) {
      out << " stroke-dasharray=\"";
      for (size_t i = 0; i < node.style.dashArray.size(); ++i) {
        if (i) out << ',';
        out << node.style.dashArray[i];
      }
      out << '"';
    }
    if (node.style.lineCap == BL_STROKE_CAP_ROUND) out << " stroke-linecap=\"round\"";
    else if (node.style.lineCap == BL_STROKE_CAP_SQUARE) out << " stroke-linecap=\"square\"";
    if (node.style.lineJoin == BL_STROKE_JOIN_ROUND) out << " stroke-linejoin=\"round\"";
    else if (node.style.lineJoin == BL_STROKE_JOIN_BEVEL) out << " stroke-linejoin=\"bevel\"";
    out << "/>\n";
    return;
  }

  writeIndent(out, depth);
  out << "<image href=\"" << escapeXml(node.image.href) << '"';
  if (!SvgDocument::isIdentity(node.transform)) {
    out << " transform=\"" << escapeXml(SvgDocument::matrixToSvg(node.transform)) << '"';
  }
  out << " x=\"" << node.image.x << "\" y=\"" << node.image.y
      << "\" width=\"" << node.image.width << "\" height=\"" << node.image.height << '"';
  if (node.style.opacity != 1.0) out << " opacity=\"" << node.style.opacity << '"';
  out << "/>\n";
}

}  // namespace

bool loadSvgDocument(const std::string& path, SvgDocument& document) {
  const std::string text = readFile(path);
  if (text.empty()) return false;

  ParseState state;
  if (!parseSvgDocument(text, state)) return false;
  state.document.setSourcePath(path);
  document = std::move(state.document);
  return true;
}

bool saveSvgDocument(const SvgDocument& document, const std::string& path) {
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\""
      << " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
      << " width=\"" << document.width() << "\" height=\"" << document.height() << "\""
      << " viewBox=\"" << document.viewBoxX() << ' ' << document.viewBoxY() << ' '
      << document.width() << ' ' << document.height() << "\">\n";

  if (!document.gradients().empty() || !document.patterns().empty()) {
    out << "  <defs>\n";
    for (const auto& [id, gradient] : document.gradients()) {
      (void)id;
      writeGradient(out, gradient, 2);
    }
    for (const auto& [id, pattern] : document.patterns()) {
      (void)id;
      writeXmlNode(out, pattern.rawNode, 2);
    }
    out << "  </defs>\n";
  }

  for (const Node& child : document.root().children) {
    writeNode(out, child, 1);
  }
  out << "</svg>\n";

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file << out.str();
  return file.good();
}

}  // namespace SvgEditor
