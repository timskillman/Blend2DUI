#include "ShapedTextCache.h"

#include "FontManager.h"

namespace Blend2DUI {

UI_ShapedTextCache::UI_ShapedTextCache(size_t maxEntries)
    : maxEntries_(maxEntries == 0 ? 1 : maxEntries) {}

const UI_ShapedText* UI_ShapedTextCache::get(UI_ButtonResources& resources,
                                             const UI_ButtonStyle& style,
                                             std::string_view text) {
  if (text.empty()) return nullptr;

  const std::string key = makeKey(resources, style, text);
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    touch(it);
    return it->second.shaped.get();
  }

  auto shaped = std::make_unique<UI_ShapedText>();
  shaped->font = FontManager::loadFont(resources, style);
  if (!shaped->font.is_valid()) return nullptr;

  shaped->glyphs.set_utf8_text(text.data(), text.size());
  shaped->font.shape(shaped->glyphs);
  shaped->font.get_text_metrics(shaped->glyphs, shaped->textMetrics);
  shaped->fontMetrics = shaped->font.metrics();

  lru_.push_front(key);
  Entry entry;
  entry.shaped = std::move(shaped);
  entry.lruIt = lru_.begin();
  auto [inserted, ok] = entries_.emplace(key, std::move(entry));
  (void)ok;
  trim();
  return inserted->second.shaped.get();
}

void UI_ShapedTextCache::clear() {
  entries_.clear();
  lru_.clear();
}

std::string UI_ShapedTextCache::makeKey(const UI_ButtonResources& resources,
                                        const UI_ButtonStyle& style,
                                        std::string_view text) const {
  std::string key;
  key.reserve(resources.assetBasePath.size() + style.font.size() + text.size() + 48);
  key.append(resources.assetBasePath);
  key.push_back('\n');
  key.append(style.font);
  key.push_back('\n');
  key.append(std::to_string(style.fontSize));
  key.push_back('\n');
  key.push_back(style.bold ? '1' : '0');
  key.push_back('\n');
  key.push_back(style.italic ? '1' : '0');
  key.push_back('\n');
  key.append(text.data(), text.size());
  return key;
}

void UI_ShapedTextCache::touch(std::unordered_map<std::string, Entry>::iterator it) {
  lru_.splice(lru_.begin(), lru_, it->second.lruIt);
  it->second.lruIt = lru_.begin();
}

void UI_ShapedTextCache::trim() {
  while (entries_.size() > maxEntries_ && !lru_.empty()) {
    entries_.erase(lru_.back());
    lru_.pop_back();
  }
}

}  // namespace Blend2DUI
