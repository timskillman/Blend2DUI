#pragma once

#include "Blend2DUI/Button.h"

#include <blend2d/blend2d.h>

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Blend2DUI {

struct UI_ShapedText {
  BLFont font;
  BLGlyphBuffer glyphs;
  BLTextMetrics textMetrics;
  BLFontMetrics fontMetrics;
};

class UI_ShapedTextCache {
 public:
  explicit UI_ShapedTextCache(size_t maxEntries = 512);

  const UI_ShapedText* get(UI_ButtonResources& resources, const UI_ButtonStyle& style, std::string_view text);
  void clear();

 private:
  struct Entry {
    std::unique_ptr<UI_ShapedText> shaped;
    std::list<std::string>::iterator lruIt;
  };

  std::string makeKey(const UI_ButtonResources& resources, const UI_ButtonStyle& style, std::string_view text) const;
  void touch(std::unordered_map<std::string, Entry>::iterator it);
  void trim();

  size_t maxEntries_ = 512;
  std::unordered_map<std::string, Entry> entries_;
  std::list<std::string> lru_;
};

}  // namespace Blend2DUI
