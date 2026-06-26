#pragma once

#include <blend2d/blend2d.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Blend2DUI {

std::string trim(std::string value);
std::string lower(std::string value);
std::string unquote(std::string value);
std::vector<std::string> splitTopLevel(std::string_view text);

double parseDouble(const std::string& value, double fallback);
size_t parseSizeT(const std::string& value, size_t fallback);
bool parseBool(const std::string& value, bool fallback);
uint32_t parseColour(const std::string& value, uint32_t fallback);
std::vector<uint32_t> parseGradientColours(const std::string& value);

bool contains(const BLRect& rect, double x, double y);
bool intersects(const BLRect& a, const BLRect& b);
BLRect intersection(const BLRect& a, const BLRect& b);
double clampCorner(double corner, const BLRect& rect);
std::filesystem::path resolveAssetPath(std::string_view assetBasePath, std::string_view path);

}  // namespace Blend2DUI
