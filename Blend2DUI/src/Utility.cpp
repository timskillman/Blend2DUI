#include "Utility.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace Blend2DUI {

std::string trim(std::string value) {
  const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
  if (begin >= end) return {};
  return std::string(begin, end);
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string unquote(std::string value) {
  value = trim(std::move(value));
  if (value.size() >= 2) {
    const char first = value.front();
    const char last = value.back();
    if ((first == '\'' && last == '\'') || (first == '"' && last == '"') || (first == '`' && last == '`')) {
      return value.substr(1, value.size() - 2);
    }
  }
  return value;
}

std::vector<std::string> splitTopLevel(std::string_view text) {
  std::vector<std::string> parts;
  std::string current;
  char quote = 0;
  int bracketDepth = 0;

  for (size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (quote) {
      current.push_back(ch);
      if (ch == quote) quote = 0;
      continue;
    }
    if (ch == '\'' || ch == '"' || ch == '`') {
      quote = ch;
      current.push_back(ch);
      continue;
    }
    if (ch == '[') ++bracketDepth;
    if (ch == ']') bracketDepth = std::max(0, bracketDepth - 1);

    const bool decimalPoint = ch == '.' && i > 0 && i + 1 < text.size() &&
                              std::isdigit(static_cast<unsigned char>(text[i - 1])) &&
                              std::isdigit(static_cast<unsigned char>(text[i + 1]));
    if ((ch == ',' || (ch == '.' && !decimalPoint)) && bracketDepth == 0) {
      if (!trim(current).empty()) parts.push_back(trim(current));
      current.clear();
      continue;
    }
    current.push_back(ch);
  }

  if (!trim(current).empty()) parts.push_back(trim(current));
  return parts;
}

double parseDouble(const std::string& value, double fallback) {
  try {
    size_t consumed = 0;
    const double parsed = std::stod(trim(unquote(value)), &consumed);
    return consumed > 0 ? parsed : fallback;
  } catch (...) {
    return fallback;
  }
}

size_t parseSizeT(const std::string& value, size_t fallback) {
  try {
    size_t consumed = 0;
    const unsigned long long parsed = std::stoull(trim(unquote(value)), &consumed);
    return consumed > 0 ? static_cast<size_t>(parsed) : fallback;
  } catch (...) {
    return fallback;
  }
}

bool parseBool(const std::string& value, bool fallback) {
  const std::string text = lower(unquote(value));
  if (text == "true" || text == "yes" || text == "on" || text == "1") return true;
  if (text == "false" || text == "no" || text == "off" || text == "0") return false;
  return fallback;
}

uint32_t parseColour(const std::string& value, uint32_t fallback) {
  std::string text = trim(unquote(value));
  if (!text.empty() && text.front() == '#') text.erase(text.begin());
  if (text.size() != 6 && text.size() != 8) return fallback;

  uint32_t parsed = 0;
  std::istringstream stream(text);
  stream >> std::hex >> parsed;
  if (!stream) return fallback;
  if (text.size() == 6) return 0xFF000000u | parsed;
  return parsed;
}

std::vector<uint32_t> parseGradientColours(const std::string& value) {
  const size_t begin = value.find('[');
  const size_t end = value.rfind(']');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) return {};

  std::vector<uint32_t> colours;
  for (const std::string& part : splitTopLevel(value.substr(begin + 1, end - begin - 1))) {
    colours.push_back(parseColour(part, 0xFFFFFFFFu));
  }
  return colours;
}

bool contains(const BLRect& rect, double x, double y) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

bool intersects(const BLRect& a, const BLRect& b) {
  return a.w > 0.0 && a.h > 0.0 && b.w > 0.0 && b.h > 0.0 &&
         a.x < b.x + b.w && a.x + a.w > b.x &&
         a.y < b.y + b.h && a.y + a.h > b.y;
}

BLRect intersection(const BLRect& a, const BLRect& b) {
  const double x0 = std::max(a.x, b.x);
  const double y0 = std::max(a.y, b.y);
  const double x1 = std::min(a.x + a.w, b.x + b.w);
  const double y1 = std::min(a.y + a.h, b.y + b.h);
  return BLRect(x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0));
}

double clampCorner(double corner, const BLRect& rect) {
  return std::max(0.0, std::min(corner, std::min(rect.w, rect.h) * 0.5));
}

}  // namespace Blend2DUI
