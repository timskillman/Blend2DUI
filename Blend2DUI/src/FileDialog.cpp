#include "SceneRenderer.h"
#include "FontManager.h"
#include "Utility.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#ifndef _WIN32
#include <dirent.h>
#endif

namespace Blend2DUI {
namespace {

BLRect insetRect(const BLRect& rect, double x, double y) {
  return BLRect(rect.x + x, rect.y + y, std::max(0.0, rect.w - x * 2.0), std::max(0.0, rect.h - y * 2.0));
}

std::string pathTextForUi(const std::filesystem::path& path) {
  return path.generic_string();
}

std::string extensionType(const std::filesystem::path& path, bool directory) {
  if (directory) return "Folder";
  std::string ext = path.extension().string();
  if (ext.empty()) return "File";
  if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return ext + " file";
}

std::filesystem::path defaultDialogStartPath() {
  namespace fs = std::filesystem;
  const char* home = std::getenv("HOME");
#ifdef _WIN32
  if (!home) home = std::getenv("USERPROFILE");
#endif
  fs::path start = home ? fs::path(home) : fs::current_path();
  const fs::path documents = start / "Documents";
  if (fs::is_directory(documents)) start = documents;
  return start;
}

BLRect defaultDialogRect(const SceneRenderer& renderer) {
  const double width = static_cast<double>(renderer.width());
  const double height = static_cast<double>(renderer.height());
  const double dialogW = std::min(820.0, std::max(560.0, width - 70.0));
  const double dialogH = std::min(560.0, std::max(430.0, height - 56.0));
  return BLRect((width - dialogW) * 0.5, (height - dialogH) * 0.5, dialogW, dialogH);
}

bool patternMatches(const std::filesystem::path& path, const std::string& pattern) {
  if (pattern.empty() || pattern == "*.*" || pattern == "*") return true;
  const std::string name = lower(path.filename().string());
  std::stringstream patterns(pattern);
  for (std::string part; patterns >> part;) {
    part.erase(std::remove(part.begin(), part.end(), ';'), part.end());
    part.erase(std::remove(part.begin(), part.end(), ','), part.end());
    part = lower(part);
    if (part == "*.*" || part == "*") return true;
    if (part.size() > 1 && part[0] == '*' && part[1] == '.') {
      if (name.size() >= part.size() - 1 && name.substr(name.size() - (part.size() - 1)) == part.substr(1)) return true;
    } else if (name == part) {
      return true;
    }
  }
  return false;
}

std::string formatSize(uintmax_t size) {
  if (size < 1024) return std::to_string(size) + (size == 1 ? " byte" : " bytes");
  const char* suffixes[] = {"KB", "MB", "GB"};
  double value = static_cast<double>(size);
  value /= 1024.0;
  size_t suffix = 0;
  while (value >= 1024.0 && suffix + 1 < 3) {
    value /= 1024.0;
    ++suffix;
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(value >= 10.0 ? 0 : 1) << value << " " << suffixes[suffix];
  return out.str();
}

void populateEntryMetadata(UI_FileDialogEntry& entry) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const bool hintedDirectory = entry.directory;
  const bool actualDirectory = fs::is_directory(entry.path, ec);
  entry.directory = hintedDirectory || (!ec && actualDirectory);
  entry.type = extensionType(entry.path, entry.directory);
  entry.size = 0;
  if (!entry.directory) {
    entry.size = fs::file_size(entry.path, ec);
    if (ec) entry.size = 0;
  }
}

bool entryPassesFilter(const UI_FileDialogEntry& entry, const std::string& pattern) {
  return entry.directory || patternMatches(entry.path, pattern);
}

void ensureDirectoriesIncluded(const std::filesystem::path& path,
                               const std::shared_ptr<UI_FileDialogScanJob>& job) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
  const fs::directory_iterator end;
  std::vector<UI_FileDialogEntry> directories;

  while (!ec && it != end) {
    const fs::directory_entry item = *it;
    UI_FileDialogEntry entry;
    entry.path = item.path();
    entry.name = item.path().filename().string();
    entry.directory = false;
    populateEntryMetadata(entry);
    if (entry.directory) directories.push_back(std::move(entry));
    it.increment(ec);
  }

  if (directories.empty()) return;

  std::lock_guard<std::mutex> lock(job->mutex);
  for (UI_FileDialogEntry& directory : directories) {
    const auto existing = std::find_if(job->entries.begin(), job->entries.end(), [&](const UI_FileDialogEntry& entry) {
      return entry.path == directory.path;
    });
    if (existing == job->entries.end()) job->entries.push_back(std::move(directory));
  }
}

bool rectsIntersect(const BLRect& a, const BLRect& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

BLRect normalizedRect(double x0, double y0, double x1, double y1) {
  const double x = std::min(x0, x1);
  const double y = std::min(y0, y1);
  return BLRect(x, y, std::abs(x1 - x0), std::abs(y1 - y0));
}

bool isSelected(const UI_FileDialogState& state, const std::filesystem::path& path) {
  return std::find(state.selectedPaths.begin(), state.selectedPaths.end(), path) != state.selectedPaths.end();
}

void selectOnly(UI_FileDialogState& state, const std::filesystem::path& path) {
  state.selectedPaths.clear();
  state.selectedPaths.push_back(path);
  state.selectionAnchorPath = path;
  state.filename = path.filename().string();
}

void toggleSelected(UI_FileDialogState& state, const std::filesystem::path& path) {
  auto it = std::find(state.selectedPaths.begin(), state.selectedPaths.end(), path);
  if (it == state.selectedPaths.end()) {
    state.selectedPaths.push_back(path);
    state.filename = path.filename().string();
  } else {
    state.selectedPaths.erase(it);
    if (state.filename == path.filename().string()) state.filename.clear();
  }
  state.selectionAnchorPath = path;
}

void selectRange(UI_FileDialogState& state, const std::filesystem::path& path) {
  if (state.selectionAnchorPath.empty()) {
    selectOnly(state, path);
    return;
  }
  auto anchorIt = std::find_if(state.entries.begin(), state.entries.end(), [&](const UI_FileDialogEntry& entry) {
    return entry.path == state.selectionAnchorPath;
  });
  auto targetIt = std::find_if(state.entries.begin(), state.entries.end(), [&](const UI_FileDialogEntry& entry) {
    return entry.path == path;
  });
  if (anchorIt == state.entries.end() || targetIt == state.entries.end()) {
    selectOnly(state, path);
    return;
  }
  size_t anchor = static_cast<size_t>(std::distance(state.entries.begin(), anchorIt));
  size_t target = static_cast<size_t>(std::distance(state.entries.begin(), targetIt));
  if (target < anchor) std::swap(anchor, target);
  state.selectedPaths.clear();
  for (size_t i = anchor; i <= target; ++i) state.selectedPaths.push_back(state.entries[i].path);
  state.filename = path.filename().string();
}

std::filesystem::path uniquePath(const std::filesystem::path& requested) {
  namespace fs = std::filesystem;
  if (!fs::exists(requested)) return requested;
  const fs::path parent = requested.parent_path();
  const std::string stem = requested.stem().string();
  const std::string ext = requested.extension().string();
  for (int i = 2; i < 10000; ++i) {
    fs::path candidate = parent / (stem + " " + std::to_string(i) + ext);
    if (!fs::exists(candidate)) return candidate;
  }
  return parent / (stem + " copy" + ext);
}

std::filesystem::path trashFilesPath() {
  namespace fs = std::filesystem;
#ifdef _WIN32
  const char* profile = std::getenv("USERPROFILE");
  fs::path base = profile ? fs::path(profile) : fs::current_path();
  return base / "Trash";
#else
  const char* home = std::getenv("HOME");
  fs::path base = home ? fs::path(home) : fs::current_path();
  return base / ".local" / "share" / "Trash" / "files";
#endif
}

void invalidateEntries(UI_FileDialogState& state) {
  state.cachedPath.clear();
  state.scanJob.reset();
  state.scanning = false;
  state.scanComplete = false;
}

void copySelectedTo(UI_FileDialogState& state, const std::filesystem::path& destinationDir) {
  namespace fs = std::filesystem;
  for (const fs::path& source : state.clipboardPaths) {
    std::error_code ec;
    if (!fs::exists(source, ec)) continue;
    const fs::path destination = uniquePath(destinationDir / source.filename());
    if (fs::is_directory(source, ec)) {
      fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    } else {
      fs::copy_file(source, destination, fs::copy_options::none, ec);
    }
  }
  invalidateEntries(state);
}

void deleteSelectionToTrash(UI_FileDialogState& state) {
  namespace fs = std::filesystem;
  if (state.selectedPaths.empty()) return;
  const fs::path trashDir = trashFilesPath();
  std::error_code ec;
  fs::create_directories(trashDir, ec);
  if (ec) return;

  state.lastTrashDelete.clear();
  for (const fs::path& source : state.selectedPaths) {
    if (!fs::exists(source, ec)) continue;
    const fs::path trashed = uniquePath(trashDir / source.filename());
    fs::rename(source, trashed, ec);
    if (!ec) state.lastTrashDelete.push_back(UI_FileDialogTrashRecord{source, trashed});
  }
  state.selectedPaths.clear();
  state.filename.clear();
  invalidateEntries(state);
}

void undoTrashDelete(UI_FileDialogState& state) {
  namespace fs = std::filesystem;
  if (state.lastTrashDelete.empty()) return;
  std::vector<fs::path> restored;
  for (auto it = state.lastTrashDelete.rbegin(); it != state.lastTrashDelete.rend(); ++it) {
    std::error_code ec;
    fs::create_directories(it->originalPath.parent_path(), ec);
    fs::path target = uniquePath(it->originalPath);
    fs::rename(it->trashPath, target, ec);
    if (!ec) restored.push_back(target);
  }
  state.selectedPaths = restored;
  if (!restored.empty()) state.filename = restored.back().filename().string();
  state.lastTrashDelete.clear();
  invalidateEntries(state);
}

std::string formatFileTime(const std::filesystem::path& path) {
  namespace fs = std::filesystem;
  try {
    const auto fileTime = fs::last_write_time(path);
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(systemTime);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char buffer[24] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
    return buffer;
  } catch (...) {
    return {};
  }
}

void drawText(BLContext& ctx,
              UI_ButtonResources& resources,
              const UI_ButtonStyleDefinition& styleDef,
              const BLRect& rect,
              const std::string& text,
              uint32_t color = 0xFF111827u,
              bool center = false) {
  if (text.empty()) return;
  const UI_ButtonStyle& style = styleDef.style();

  BLFont font = FontManager::loadFont(resources, style);
  if (!font.is_valid()) return;

  BLGlyphBuffer glyphs;
  BLTextMetrics metrics;
  BLFontMetrics fontMetrics = font.metrics();
  glyphs.set_utf8_text(text.data(), text.size());
  font.shape(glyphs);
  font.get_text_metrics(glyphs, metrics);
  double x = rect.x;
  if (center) x += std::max(0.0, (rect.w - metrics.advance.x) * 0.5);
  const double y = rect.y + std::max(0.0, (rect.h - (fontMetrics.ascent + fontMetrics.descent)) * 0.5) + fontMetrics.ascent;
  ctx.set_fill_style(BLRgba32(color));
  ctx.fill_glyph_run(BLPoint(x, y), font, glyphs.glyph_run());
}

std::vector<std::string> wrapLabel(const std::string& text, size_t maxCharsPerLine) {
  std::vector<std::string> lines;
  std::string current;
  std::stringstream stream(text);
  std::string word;
  bool truncated = false;

  auto pushCurrent = [&]() {
    if (!current.empty() && lines.size() < 3) {
      lines.push_back(current);
      current.clear();
    }
  };

  while (stream >> word) {
    while (word.size() > maxCharsPerLine) {
      if (!current.empty()) pushCurrent();
      if (lines.size() >= 3) {
        truncated = true;
        break;
      }
      lines.push_back(word.substr(0, maxCharsPerLine));
      word.erase(0, maxCharsPerLine);
    }
    if (lines.size() >= 3) {
      truncated = true;
      break;
    }
    if (current.empty()) current = word;
    else if (current.size() + 1 + word.size() <= maxCharsPerLine) current += " " + word;
    else {
      pushCurrent();
      current = word;
    }
  }
  pushCurrent();

  if (lines.empty()) lines.push_back(text.substr(0, maxCharsPerLine));
  if (truncated && !lines.empty()) {
    std::string& last = lines.back();
    const size_t maxLast = maxCharsPerLine > 3 ? maxCharsPerLine - 3 : maxCharsPerLine;
    if (last.size() > maxLast) last.resize(maxLast);
    last += "...";
  }
  return lines;
}

void drawWrappedLabel(BLContext& ctx,
                      UI_ButtonResources& resources,
                      const UI_ButtonStyleDefinition& style,
                      const BLRect& rect,
                      const std::string& text) {
  const size_t maxChars = std::max<size_t>(6, static_cast<size_t>(rect.w / 7.0));
  const std::vector<std::string> lines = wrapLabel(text, maxChars);
  const double lineH = std::min(15.0, rect.h / 3.0);
  double y = rect.y;
  for (const std::string& line : lines) {
    drawText(ctx, resources, style, BLRect(rect.x, y, rect.w, lineH), line, 0xFF0F172Au, true);
    y += lineH;
  }
}

void drawFolderIcon(BLContext& ctx, const BLRect& rect) {
  ctx.set_fill_style(BLRgba32(0xFFFFC857u));
  ctx.fill_round_rect(BLRoundRect(rect.x, rect.y + rect.h * 0.24, rect.w, rect.h * 0.72, 5.0));
  ctx.fill_round_rect(BLRoundRect(rect.x + rect.w * 0.08, rect.y + rect.h * 0.08, rect.w * 0.42, rect.h * 0.28, 4.0));
  ctx.set_stroke_style(BLRgba32(0xFFD99B20u));
  ctx.set_stroke_width(1.2);
  ctx.stroke_round_rect(BLRoundRect(rect.x, rect.y + rect.h * 0.24, rect.w, rect.h * 0.72, 5.0));
}

void drawFileIcon(BLContext& ctx, const BLRect& rect) {
  BLPath path;
  path.move_to(rect.x, rect.y);
  path.line_to(rect.x + rect.w * 0.68, rect.y);
  path.line_to(rect.x + rect.w, rect.y + rect.h * 0.32);
  path.line_to(rect.x + rect.w, rect.y + rect.h);
  path.line_to(rect.x, rect.y + rect.h);
  path.close();
  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_path(path);
  ctx.set_stroke_style(BLRgba32(0xFF94A3B8u));
  ctx.set_stroke_width(1.2);
  ctx.stroke_path(path);
  ctx.set_fill_style(BLRgba32(0xFFE2E8F0u));
  BLPath fold;
  fold.move_to(rect.x + rect.w * 0.68, rect.y);
  fold.line_to(rect.x + rect.w, rect.y + rect.h * 0.32);
  fold.line_to(rect.x + rect.w * 0.68, rect.y + rect.h * 0.32);
  fold.close();
  ctx.fill_path(fold);
}

enum class QuickLinkIcon {
  Home,
  Desktop,
  Documents,
  Downloads,
  Pictures,
  Music,
  Videos
};

struct QuickLink {
  std::string label;
  std::filesystem::path path;
  QuickLinkIcon icon = QuickLinkIcon::Documents;
};

void drawQuickLinkIcon(BLContext& ctx, const BLRect& rect, QuickLinkIcon icon) {
  switch (icon) {
    case QuickLinkIcon::Home: {
      BLPath roof;
      roof.move_to(rect.x + rect.w * 0.08, rect.y + rect.h * 0.48);
      roof.line_to(rect.x + rect.w * 0.5, rect.y + rect.h * 0.12);
      roof.line_to(rect.x + rect.w * 0.92, rect.y + rect.h * 0.48);
      roof.close();
      ctx.set_fill_style(BLRgba32(0xFF38BDF8u));
      ctx.fill_path(roof);
      ctx.set_fill_style(BLRgba32(0xFFE0F2FEu));
      ctx.fill_round_rect(BLRoundRect(rect.x + rect.w * 0.22, rect.y + rect.h * 0.44, rect.w * 0.56, rect.h * 0.44, 2.0));
      break;
    }
    case QuickLinkIcon::Desktop:
      ctx.set_fill_style(BLRgba32(0xFFE0F2FEu));
      ctx.fill_round_rect(BLRoundRect(rect.x + 1.0, rect.y + 2.0, rect.w - 2.0, rect.h * 0.62, 2.0));
      ctx.set_stroke_style(BLRgba32(0xFF0284C7u));
      ctx.set_stroke_width(1.1);
      ctx.stroke_round_rect(BLRoundRect(rect.x + 1.0, rect.y + 2.0, rect.w - 2.0, rect.h * 0.62, 2.0));
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.5, rect.y + rect.h * 0.66, rect.x + rect.w * 0.5, rect.y + rect.h * 0.86));
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.28, rect.y + rect.h * 0.88, rect.x + rect.w * 0.72, rect.y + rect.h * 0.88));
      break;
    case QuickLinkIcon::Downloads:
      ctx.set_stroke_style(BLRgba32(0xFF2563EBu));
      ctx.set_stroke_width(2.0);
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.5, rect.y + rect.h * 0.15, rect.x + rect.w * 0.5, rect.y + rect.h * 0.62));
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.28, rect.y + rect.h * 0.44, rect.x + rect.w * 0.5, rect.y + rect.h * 0.66));
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.72, rect.y + rect.h * 0.44, rect.x + rect.w * 0.5, rect.y + rect.h * 0.66));
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.18, rect.y + rect.h * 0.84, rect.x + rect.w * 0.82, rect.y + rect.h * 0.84));
      break;
    case QuickLinkIcon::Pictures: {
      ctx.set_fill_style(BLRgba32(0xFFDCFCE7u));
      ctx.fill_round_rect(BLRoundRect(rect.x + 1.0, rect.y + 2.0, rect.w - 2.0, rect.h - 4.0, 2.5));
      ctx.set_fill_style(BLRgba32(0xFF22C55E));
      ctx.fill_circle(BLCircle(rect.x + rect.w * 0.72, rect.y + rect.h * 0.32, rect.w * 0.08));
      ctx.set_fill_style(BLRgba32(0xFF16A34Au));
      BLPath mountain;
      mountain.move_to(rect.x + rect.w * 0.14, rect.y + rect.h * 0.76);
      mountain.line_to(rect.x + rect.w * 0.38, rect.y + rect.h * 0.48);
      mountain.line_to(rect.x + rect.w * 0.54, rect.y + rect.h * 0.66);
      mountain.line_to(rect.x + rect.w * 0.66, rect.y + rect.h * 0.54);
      mountain.line_to(rect.x + rect.w * 0.88, rect.y + rect.h * 0.76);
      mountain.close();
      ctx.fill_path(mountain);
      break;
    }
    case QuickLinkIcon::Music:
      ctx.set_stroke_style(BLRgba32(0xFF9333EAu));
      ctx.set_stroke_width(2.0);
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.56, rect.y + rect.h * 0.2, rect.x + rect.w * 0.56, rect.y + rect.h * 0.72));
      ctx.stroke_line(BLLine(rect.x + rect.w * 0.56, rect.y + rect.h * 0.2, rect.x + rect.w * 0.82, rect.y + rect.h * 0.28));
      ctx.set_fill_style(BLRgba32(0xFFC084FCu));
      ctx.fill_ellipse(BLEllipse(rect.x + rect.w * 0.38, rect.y + rect.h * 0.73, rect.w * 0.16, rect.h * 0.11));
      break;
    case QuickLinkIcon::Videos:
      ctx.set_fill_style(BLRgba32(0xFFFEE2E2u));
      ctx.fill_round_rect(BLRoundRect(rect.x + 1.0, rect.y + 3.0, rect.w - 2.0, rect.h - 6.0, 2.5));
      ctx.set_fill_style(BLRgba32(0xFFEF4444u));
      {
        BLPath play;
        play.move_to(rect.x + rect.w * 0.4, rect.y + rect.h * 0.32);
        play.line_to(rect.x + rect.w * 0.4, rect.y + rect.h * 0.72);
        play.line_to(rect.x + rect.w * 0.72, rect.y + rect.h * 0.52);
        play.close();
        ctx.fill_path(play);
      }
      break;
    case QuickLinkIcon::Documents:
    default:
      drawFileIcon(ctx, rect);
      break;
  }
}

std::vector<QuickLink> quickLinks() {
  namespace fs = std::filesystem;
  std::vector<QuickLink> links;
  const fs::path home = [] {
#ifdef _WIN32
    const char* profile = std::getenv("USERPROFILE");
    return profile ? fs::path(profile) : fs::current_path();
#else
    const char* homeEnv = std::getenv("HOME");
    return homeEnv ? fs::path(homeEnv) : fs::current_path();
#endif
  }();
#if !defined(_WIN32) && !defined(__APPLE__)
  links.push_back(QuickLink{"Home", home, QuickLinkIcon::Home});
#endif
  links.push_back(QuickLink{"Desktop", home / "Desktop", QuickLinkIcon::Desktop});
  links.push_back(QuickLink{"Documents", home / "Documents", QuickLinkIcon::Documents});
  links.push_back(QuickLink{"Downloads", home / "Downloads", QuickLinkIcon::Downloads});
  links.push_back(QuickLink{"Pictures", home / "Pictures", QuickLinkIcon::Pictures});
  links.push_back(QuickLink{"Music", home / "Music", QuickLinkIcon::Music});
  links.push_back(QuickLink{"Videos", home / "Videos", QuickLinkIcon::Videos});
  return links;
}

void scanDirectory(const std::filesystem::path& path,
                   const std::string& pattern,
                   const std::shared_ptr<UI_FileDialogScanJob>& job) {
  namespace fs = std::filesystem;
  constexpr size_t kMaxVisibleEntries = 1500;
  constexpr size_t kPublishBatchSize = 24;
  std::vector<UI_FileDialogEntry> batch;
  batch.reserve(kPublishBatchSize);

  auto publishBatch = [&]() {
    if (batch.empty()) return;
    std::lock_guard<std::mutex> lock(job->mutex);
    const size_t remaining = kMaxVisibleEntries > job->entries.size() ? kMaxVisibleEntries - job->entries.size() : 0;
    const size_t count = std::min(remaining, batch.size());
    job->entries.insert(job->entries.end(), batch.begin(), batch.begin() + static_cast<std::ptrdiff_t>(count));
    batch.clear();
  };

#ifndef _WIN32
  DIR* dir = opendir(path.string().c_str());
  if (dir) {
    while (dirent* item = readdir(dir)) {
      const std::string name = item->d_name;
      if (name == "." || name == "..") continue;
      const fs::path itemPath = path / name;
      UI_FileDialogEntry entry;
      entry.path = itemPath;
      entry.name = name;
      entry.directory = item->d_type == DT_DIR;
      populateEntryMetadata(entry);
      if (entryPassesFilter(entry, pattern)) {
        batch.push_back(std::move(entry));
        if (batch.size() >= kPublishBatchSize) publishBatch();
      }
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->entries.size() >= kMaxVisibleEntries) break;
      }
    }
    closedir(dir);
    publishBatch();
    ensureDirectoriesIncluded(path, job);

    {
      std::lock_guard<std::mutex> lock(job->mutex);
      std::sort(job->entries.begin(), job->entries.end(), [](const UI_FileDialogEntry& a, const UI_FileDialogEntry& b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return lower(a.name) < lower(b.name);
      });
    }
    return;
  }
#endif

  std::error_code ec;
  fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
  const fs::directory_iterator end;
  size_t scanned = 0;
  while (!ec && it != end) {
    const fs::directory_entry item = *it;
    UI_FileDialogEntry entry;
    entry.path = item.path();
    entry.name = item.path().filename().string();
    populateEntryMetadata(entry);
    if (entryPassesFilter(entry, pattern)) {
      batch.push_back(std::move(entry));
      if (batch.size() >= kPublishBatchSize) publishBatch();
    }
    it.increment(ec);
    ++scanned;

    {
      std::lock_guard<std::mutex> lock(job->mutex);
      if (job->entries.size() >= kMaxVisibleEntries) break;
    }
  }
  publishBatch();
  ensureDirectoriesIncluded(path, job);

  {
    std::lock_guard<std::mutex> lock(job->mutex);
    std::sort(job->entries.begin(), job->entries.end(), [](const UI_FileDialogEntry& a, const UI_FileDialogEntry& b) {
      if (a.directory != b.directory) return a.directory > b.directory;
      return lower(a.name) < lower(b.name);
    });
  }
  (void)scanned;
}

bool commitRename(UI_FileDialogState& state) {
  namespace fs = std::filesystem;
  if (!state.renaming || state.renameText.empty()) return false;
  const fs::path target = state.renamePath.parent_path() / state.renameText;
  if (target == state.renamePath) {
    state.renaming = false;
    return false;
  }
  std::error_code ec;
  fs::rename(state.renamePath, target, ec);
  state.renaming = false;
  state.renameText.clear();
  state.renamePath.clear();
  state.cachedPath.clear();
  state.scanJob.reset();
  state.scanning = false;
  state.scanComplete = false;
  return !ec;
}

void cancelRename(UI_FileDialogState& state) {
  state.renaming = false;
  state.renameText.clear();
  state.renamePath.clear();
}

void refreshEntries(UI_FileDialogState& state, const UI_FileDialogOptions& options) {
  const std::string pattern = options.filters.empty() ? "*.*" : options.filters[std::min(state.filterIndex, options.filters.size() - 1)].pattern;
  if (state.cachedPath == state.currentPath && state.cachedPattern == pattern && state.scanComplete) return;

  if (state.cachedPath != state.currentPath || state.cachedPattern != pattern) {
    state.entries.clear();
    state.cachedPath = state.currentPath;
    state.cachedPattern = pattern;
    state.fileScroll = 0.0;
    state.scanComplete = false;
    state.scanning = true;

    auto job = std::make_shared<UI_FileDialogScanJob>();
    state.scanJob = job;
    const std::filesystem::path scanPath = state.currentPath;
    const std::string scanPattern = pattern;
    std::thread([job, scanPath, scanPattern]() {
      scanDirectory(scanPath, scanPattern, job);
      job->done.store(true, std::memory_order_release);
    }).detach();
    return;
  }

  if (state.scanJob) {
    std::lock_guard<std::mutex> lock(state.scanJob->mutex);
    state.entries = state.scanJob->entries;
  }

  if (state.scanJob && state.scanJob->done.load(std::memory_order_acquire)) {
    state.scanJob.reset();
    state.scanning = false;
    state.scanComplete = true;
  }
}

}  // namespace

bool SceneRenderer::pointerCapturedByModal(const std::string& id) const {
  return modalPointerCaptureActive_ &&
         !modalPointerCaptureIdPrefix_.empty() &&
         id.rfind(modalPointerCaptureIdPrefix_, 0) != 0;
}

#ifndef BLEND2DUI_FILE_DIALOG_TEST_ACCESS
UI_FileDialogResult SceneRenderer::UI_FileDialog(const std::string& id,
                                                      const BLRect& requestedRect,
                                                      const UI_FileDialogOptions& options,
                                                      std::string& selectedPath) {
  namespace fs = std::filesystem;
  if (!frameActive_) return UI_FileDialogResult::None;
  modalOverlayActive_ = true;
  nextModalPointerCaptureActive_ = true;
  nextModalPointerCaptureIdPrefix_ = id;
  auto finishDialog = [&](UI_FileDialogResult result) {
    if (result != UI_FileDialogResult::None) {
      nextModalPointerCaptureActive_ = false;
      nextModalPointerCaptureIdPrefix_.clear();
    }
    return result;
  };

  UI_FileDialogState& state = fileDialogStates_[id];
  if (!state.dialogRectInitialized) {
    state.dialogRect = requestedRect;
    state.dialogRectInitialized = true;
  }
  BLRect& rect = state.dialogRect;
  constexpr double kMinDialogW = 560.0;
  constexpr double kMinDialogH = 430.0;
  rect.w = std::max(kMinDialogW, rect.w);
  rect.h = std::max(kMinDialogH, rect.h);
  rect.x = std::max(0.0, std::min(rect.x, std::max(0.0, static_cast<double>(width_) - rect.w)));
  rect.y = std::max(0.0, std::min(rect.y, std::max(0.0, static_cast<double>(height_) - rect.h)));

  if (!state.initialized) {
    state.initialized = true;
    state.currentPath = options.startPath.empty() ? fs::current_path() : options.startPath;
    if (!fs::is_directory(state.currentPath)) state.currentPath = fs::current_path();
    state.pathText = pathTextForUi(state.currentPath);
    state.filterIndex = 0;
    state.filename = options.defaultFileName;
  }

  if (state.hasPendingPath) {
    state.hasPendingPath = false;
    if (fs::is_directory(state.pendingPath)) {
      cancelRename(state);
      state.selectedPaths.clear();
      state.selectionAnchorPath.clear();
      state.contextMenuOpen = false;
      state.currentPath = state.pendingPath;
      state.pathText = pathTextForUi(state.currentPath);
      state.cachedPath.clear();
      state.scanJob.reset();
      state.scanning = false;
      state.scanComplete = false;
    }
    state.pendingPath.clear();
  }

  const UI_ButtonStyleDefinition buttonStyle("FillColour:#F8FAFC, HoverColour:#E0F2FE, PressedColour:#BAE6FD, "
                                             "StrokeColour:#CBD5E1, StrokeWidth:1, TextColour:#0F172A, Corner:6, FontSize:13");
  const UI_ButtonStyleDefinition primaryStyle("FillColour:#2563EB, HoverColour:#3B82F6, PressedColour:#1D4ED8, "
                                              "StrokeColour:#1E40AF, StrokeWidth:1, TextColour:#FFFFFF, Corner:7, FontSize:14");
  const UI_ButtonStyleDefinition fieldStyle("FillColour:#FFFFFF, HoverColour:#F8FAFC, PressedColour:#FFFFFF, "
                                            "StrokeColour:#CBD5E1, StrokeWidth:1, TextColour:#0F172A, Corner:6, FontSize:14");
  const UI_ButtonStyleDefinition closeStyle("FillColour:#F8FAFC, HoverColour:#FEE2E2, PressedColour:#FECACA, "
                                            "StrokeColour:#CBD5E1, StrokeWidth:1, TextColour:#991B1B, Corner:6, FontSize:14");
  const UI_ButtonStyleDefinition smallText("FontSize:12, TextColour:#334155");
  const UI_ButtonStyleDefinition titleText("FontSize:17, TextColour:#0F172A");

  for (const UI_TextInputKeyEvent& event : keyEvents_) {
    const bool ctrl = (event.mod & SDL_KMOD_CTRL) != 0;
    if (ctrl && event.key == SDLK_Z) {
      undoTrashDelete(state);
      break;
    }
  }

  const BLRect titleBar(rect.x, rect.y, rect.w, 44.0);
  const BLRect initialCloseRect(rect.x + rect.w - 44.0, rect.y + 10.0, 28.0, 28.0);
  const BLRect resizeGrip(rect.x + rect.w - 18.0, rect.y + rect.h - 18.0, 18.0, 18.0);
  const std::string dragId = id + ".drag";
  const std::string resizeId = id + ".resize";
  if (mousePressed_ && contains(resizeGrip, mouseX_, mouseY_)) {
    activeButtonId_ = resizeId;
    state.resizingDialog = true;
    state.dialogResizeStartMouseX = mouseX_;
    state.dialogResizeStartMouseY = mouseY_;
    state.dialogResizeStartW = rect.w;
    state.dialogResizeStartH = rect.h;
    cancelRename(state);
  } else if (mousePressed_ && contains(titleBar, mouseX_, mouseY_) && !contains(initialCloseRect, mouseX_, mouseY_)) {
    activeButtonId_ = dragId;
    state.draggingDialog = true;
    state.dialogDragOffsetX = mouseX_ - rect.x;
    state.dialogDragOffsetY = mouseY_ - rect.y;
    cancelRename(state);
  }
  if (state.draggingDialog && mouseDown_) {
    rect.x = mouseX_ - state.dialogDragOffsetX;
    rect.y = mouseY_ - state.dialogDragOffsetY;
  }
  if (state.resizingDialog && mouseDown_) {
    rect.w = std::max(kMinDialogW, state.dialogResizeStartW + mouseX_ - state.dialogResizeStartMouseX);
    rect.h = std::max(kMinDialogH, state.dialogResizeStartH + mouseY_ - state.dialogResizeStartMouseY);
  }
  if (mouseReleased_) {
    if (state.draggingDialog || state.resizingDialog) activeButtonId_.clear();
    state.draggingDialog = false;
    state.resizingDialog = false;
  }
  rect.w = std::min(rect.w, static_cast<double>(width_));
  rect.h = std::min(rect.h, static_cast<double>(height_));
  rect.x = std::max(0.0, std::min(rect.x, std::max(0.0, static_cast<double>(width_) - rect.w)));
  rect.y = std::max(0.0, std::min(rect.y, std::max(0.0, static_cast<double>(height_) - rect.h)));
  const BLRect closeRect(rect.x + rect.w - 44.0, rect.y + 10.0, 28.0, 28.0);

  refreshEntries(state, options);

  BLContext& ctx = context_;
  ctx.set_fill_style(BLRgba32(0x66000000u));
  ctx.fill_all();
  ctx.set_fill_style(BLRgba32(0xFFF8FAFCu));
  ctx.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, 8.0));
  ctx.set_stroke_style(BLRgba32(0xFF94A3B8u));
  ctx.set_stroke_width(1.0);
  ctx.stroke_round_rect(BLRoundRect(rect.x + 0.5, rect.y + 0.5, rect.w - 1.0, rect.h - 1.0, 8.0));

  drawText(ctx, buttonResources_, titleText, BLRect(rect.x + 18, rect.y + 12, rect.w - 72, 28),
           options.title.empty() ? (options.mode == UI_FileDialogMode::Save ? "Save File" : "Open File") : options.title);
  if (UI_Button(id + ".close", closeRect, closeStyle, UI_ButtonContent("x", "Close")) == UI_ButtonActionPressed) {
    cancelRename(state);
    return finishDialog(UI_FileDialogResult::Cancelled);
  }

  const double margin = 18.0;
  const double top = rect.y + 50.0;
  const double controlsY = rect.y + rect.h - 118.0;
  const double bodyBottom = controlsY - 12.0;
  const BLRect upRect(rect.x + margin, top, 34.0, 34.0);
  if (UI_Button(id + ".up", upRect, buttonStyle, UI_ButtonContent("^", "Go up one folder")) == UI_ButtonActionPressed) {
    const fs::path parent = state.currentPath.parent_path();
    if (!parent.empty() && parent != state.currentPath) {
      cancelRename(state);
      state.selectedPaths.clear();
      state.selectionAnchorPath.clear();
      state.pendingPath = parent;
      state.hasPendingPath = true;
    }
  }

  const BLRect pathRect(upRect.x + upRect.w + 8.0, top, rect.w - margin * 2.0 - upRect.w - 8.0, 34.0);
  UI_TextInputOptions pathOptions;
  pathOptions.placeholder = "Path";
  UI_TextInput(id + ".path", pathRect, pathOptions, state.pathText, fieldStyle);

  const bool pathEnter = focusedTextInputId_ == id + ".path" &&
                         std::any_of(keyEvents_.begin(), keyEvents_.end(), [](const UI_TextInputKeyEvent& event) {
                           return event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER;
                         });
  if (pathEnter) {
    fs::path requested(state.pathText);
    if (fs::is_directory(requested)) {
      cancelRename(state);
      state.selectedPaths.clear();
      state.selectionAnchorPath.clear();
      state.contextMenuOpen = false;
      state.currentPath = fs::canonical(requested);
      state.pathText = pathTextForUi(state.currentPath);
      state.cachedPath.clear();
    }
  }

  const BLRect newFolderRect(rect.x + margin, pathRect.y + pathRect.h + 8.0, 118.0, 30.0);
  if (UI_Button(id + ".new-folder", newFolderRect, buttonStyle, UI_ButtonContent("New folder")) == UI_ButtonActionPressed) {
    for (int i = 0; i < 100; ++i) {
      fs::path folder = state.currentPath / (i == 0 ? "New Folder" : "New Folder " + std::to_string(i + 1));
      std::error_code ec;
      if (fs::create_directory(folder, ec)) {
        cancelRename(state);
        state.filename = folder.filename().string();
        state.selectedPaths.clear();
        state.selectedPaths.push_back(folder);
        state.selectionAnchorPath = folder;
        state.cachedPath.clear();
        state.scanJob.reset();
        state.scanning = false;
        state.scanComplete = false;
        break;
      }
    }
  }
  const BLRect viewButton(rect.x + rect.w - margin - 34.0, newFolderRect.y, 34.0, 30.0);
  if (UI_Button(id + ".view", viewButton, buttonStyle, UI_ButtonContent("[]", "Change file view")) == UI_ButtonActionPressed) {
    state.view = static_cast<UI_FileDialogView>((static_cast<int>(state.view) + 1) % 5);
    state.fileScroll = 0.0;
  }

  const BLRect body(rect.x + margin,
                    newFolderRect.y + newFolderRect.h + 10.0,
                    rect.w - margin * 2.0,
                    std::max(110.0, bodyBottom - (newFolderRect.y + newFolderRect.h + 10.0)));
  const BLRect quickRect(body.x, body.y, 150.0, body.h);
  const BLRect filesRect(quickRect.x + quickRect.w + 12.0, body.y, body.w - quickRect.w - 12.0, body.h);

  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_round_rect(BLRoundRect(quickRect.x, quickRect.y, quickRect.w, quickRect.h, 7.0));
  ctx.fill_round_rect(BLRoundRect(filesRect.x, filesRect.y, filesRect.w, filesRect.h, 7.0));
  ctx.set_stroke_style(BLRgba32(0xFFE2E8F0u));
  ctx.stroke_round_rect(BLRoundRect(quickRect.x, quickRect.y, quickRect.w, quickRect.h, 7.0));
  ctx.stroke_round_rect(BLRoundRect(filesRect.x, filesRect.y, filesRect.w, filesRect.h, 7.0));

  if (contains(quickRect, mouseX_, mouseY_) && wheelY_ != 0.0) {
    state.quickScroll = std::max(0.0, state.quickScroll - wheelY_ * 28.0);
  }
  if (contains(filesRect, mouseX_, mouseY_) && wheelY_ != 0.0) {
    state.fileScroll = std::max(0.0, state.fileScroll - wheelY_ * 42.0);
  }

  ctx.save();
  ctx.clip_to_rect(quickRect);
  double linkY = quickRect.y + 8.0 - state.quickScroll;
  for (const QuickLink& link : quickLinks()) {
    const BLRect item(quickRect.x + 8.0, linkY, quickRect.w - 16.0, 28.0);
    if (contains(item, mouseX_, mouseY_)) {
      ctx.set_fill_style(BLRgba32(0xFFE0F2FEu));
      ctx.fill_round_rect(BLRoundRect(item.x, item.y, item.w, item.h, 5.0));
      if (mousePressed_) {
        cancelRename(state);
        state.selectedPaths.clear();
        state.selectionAnchorPath.clear();
        state.contextMenuOpen = false;
        state.pendingPath = link.path;
        state.hasPendingPath = true;
      }
    }
    drawQuickLinkIcon(ctx, BLRect(item.x + 7.0, item.y + 5.0, 18.0, 18.0), link.icon);
    drawText(ctx, buttonResources_, smallText, BLRect(item.x + 32.0, item.y, item.w - 36.0, item.h), link.label);
    linkY += 31.0;
  }
  ctx.restore();

  if (state.scanning) {
    drawText(ctx,
             buttonResources_,
             smallText,
             BLRect(filesRect.x + 12.0, filesRect.y + 10.0, filesRect.w - 24.0, 24.0),
             "Loading folder...",
             0xFF64748Bu);
  }

  const BLRect filesClip(filesRect.x + 8.0, filesRect.y + 8.0, filesRect.w - 16.0, filesRect.h - 16.0);
  ctx.save();
  ctx.clip_to_rect(filesClip);

  double y = filesClip.y - state.fileScroll;
  const bool iconView = state.view == UI_FileDialogView::LargeIcons ||
                        state.view == UI_FileDialogView::MediumIcons ||
                        state.view == UI_FileDialogView::SmallIcons;
  if (state.view == UI_FileDialogView::Details) {
    drawText(ctx, buttonResources_, smallText, BLRect(filesClip.x + 42, y, 220, 24), "Name", 0xFF64748Bu);
    drawText(ctx, buttonResources_, smallText, BLRect(filesClip.x + 270, y, 90, 24), "Size", 0xFF64748Bu);
    drawText(ctx, buttonResources_, smallText, BLRect(filesClip.x + 370, y, 150, 24), "Modified", 0xFF64748Bu);
    drawText(ctx, buttonResources_, smallText, BLRect(filesClip.x + 530, y, 120, 24), "Type", 0xFF64748Bu);
    y += 28.0;
  }

  const double tileW = state.view == UI_FileDialogView::LargeIcons ? 116.0 : state.view == UI_FileDialogView::MediumIcons ? 92.0 : 74.0;
  const double tileH = state.view == UI_FileDialogView::LargeIcons ? 116.0 : state.view == UI_FileDialogView::MediumIcons ? 104.0 : 88.0;
  const double iconSize = (state.view == UI_FileDialogView::LargeIcons ? 42.0 : state.view == UI_FileDialogView::MediumIcons ? 34.0 : 26.0) * 1.3;
  const size_t columns = iconView ? std::max<size_t>(1, static_cast<size_t>(filesClip.w / tileW)) : 1;
  const size_t rows = iconView ? (state.entries.size() + columns - 1) / columns : state.entries.size();
  const double contentHeight = (state.view == UI_FileDialogView::Details ? 28.0 : 0.0) +
                               static_cast<double>(rows) * (iconView ? tileH : 30.0);
  const double maxFileScroll = std::max(0.0, contentHeight - filesClip.h);
  state.fileScroll = std::min(state.fileScroll, maxFileScroll);

  const BLRect fileScrollbarTrack(filesRect.x + filesRect.w - 13.0, filesClip.y, 7.0, filesClip.h);
  const BLRect fileScrollbarHitRect(fileScrollbarTrack.x - 8.0,
                                    fileScrollbarTrack.y,
                                    fileScrollbarTrack.w + 14.0,
                                    fileScrollbarTrack.h);
  const bool showFileScrollbar = maxFileScroll > 1.0;
  const double fileThumbH = showFileScrollbar ? std::max(26.0, filesClip.h * filesClip.h / std::max(filesClip.h, contentHeight)) : filesClip.h;
  const double fileThumbY = showFileScrollbar ? fileScrollbarTrack.y + (fileScrollbarTrack.h - fileThumbH) * (state.fileScroll / maxFileScroll) : fileScrollbarTrack.y;
  const BLRect fileScrollbarThumb(fileScrollbarTrack.x, fileThumbY, fileScrollbarTrack.w, fileThumbH);

  const bool mouseInFileScrollbar = showFileScrollbar && contains(fileScrollbarHitRect, mouseX_, mouseY_);
  if (mousePressed_ && mouseInFileScrollbar) {
    activeButtonId_ = id + ".file-scrollbar";
    state.draggingFileScrollbar = true;
    state.draggingSelection = false;
    state.fileScrollbarDragOffsetY = contains(fileScrollbarThumb, mouseX_, mouseY_) ? mouseY_ - fileThumbY : fileThumbH * 0.5;
  }
  if (state.draggingFileScrollbar && mouseDown_) {
    const double top = std::max(fileScrollbarTrack.y, std::min(fileScrollbarTrack.y + fileScrollbarTrack.h - fileThumbH, mouseY_ - state.fileScrollbarDragOffsetY));
    const double t = (top - fileScrollbarTrack.y) / std::max(1.0, fileScrollbarTrack.h - fileThumbH);
    state.fileScroll = maxFileScroll * t;
  }
  if (mouseReleased_ && state.draggingFileScrollbar) {
    state.draggingFileScrollbar = false;
    activeButtonId_.clear();
  }
  y = filesClip.y - state.fileScroll;
  if (state.view == UI_FileDialogView::Details) y += 28.0;

  constexpr double contextMenuW = 150.0;
  constexpr double contextMenuH = 112.0;
  const double contextMenuX = std::min(state.contextMenuX, rect.x + rect.w - contextMenuW - 8.0);
  const double contextMenuY = std::min(state.contextMenuY, rect.y + rect.h - contextMenuH - 8.0);
  const BLRect activeContextMenuRect(contextMenuX, contextMenuY, contextMenuW, contextMenuH);
  const bool mouseInContextMenu = state.contextMenuOpen && contains(activeContextMenuRect, mouseX_, mouseY_);
  const bool clickedInFilesPanel = mousePressed_ && contains(filesClip, mouseX_, mouseY_) && !mouseInContextMenu && !mouseInFileScrollbar;
  const bool rightClickedInFilesPanel = rightMousePressed_ && contains(filesClip, mouseX_, mouseY_) && !mouseInContextMenu && !mouseInFileScrollbar;
  const SDL_Keymod mods = SDL_GetModState();
  const bool ctrlDown = (mods & SDL_KMOD_CTRL) != 0;
  const bool shiftDown = (mods & SDL_KMOD_SHIFT) != 0;
  const BLRect marqueeRect = normalizedRect(state.selectionStartX, state.selectionStartY, mouseX_, mouseY_);
  bool clickedAnyFileItem = false;
  bool rightClickedAnyFileItem = false;
  for (size_t i = 0; i < state.entries.size(); ++i) {
    const UI_FileDialogEntry& entry = state.entries[i];
    BLRect item;
    BLRect icon;
    BLRect nameRect;
    if (iconView) {
      const size_t col = i % columns;
      const size_t row = i / columns;
      item = BLRect(filesClip.x + col * tileW, y + row * tileH, tileW - 8.0, tileH - 8.0);
      icon = BLRect(item.x + (item.w - iconSize) * 0.5, item.y + 8.0, iconSize, iconSize);
      nameRect = BLRect(item.x + 4.0, icon.y + icon.h + 4.0, item.w - 8.0, 40.0);
    } else {
      item = BLRect(filesClip.x, y + i * 30.0, filesClip.w, 28.0);
      icon = BLRect(item.x + 8.0, item.y + 2.0, 25.0, 24.0);
      nameRect = BLRect(item.x + 42.0, item.y, state.view == UI_FileDialogView::Details ? 220.0 : item.w - 50.0, item.h);
    }
    if (item.y > filesClip.y + filesClip.h || item.y + item.h < filesClip.y) continue;

    const bool hovered = contains(item, mouseX_, mouseY_);
    const bool nameHovered = contains(nameRect, mouseX_, mouseY_);
    const bool iconHovered = contains(icon, mouseX_, mouseY_);
    if (state.draggingSelection && rectsIntersect(marqueeRect, item) && !isSelected(state, entry.path)) {
      state.selectedPaths.push_back(entry.path);
      state.filename = entry.name;
    }
    const bool selected = isSelected(state, entry.path);
    if (hovered || selected) {
      ctx.set_fill_style(BLRgba32(selected ? 0xFFD9EAFEu : 0xFFF1F5F9u));
      ctx.fill_round_rect(BLRoundRect(item.x, item.y, item.w, item.h, 6.0));
    }

    if (mousePressed_ && hovered && !mouseInContextMenu) {
      clickedAnyFileItem = true;
      const std::string clickKey = entry.path.string();
      const bool doubleClick = !ctrlDown &&
                               !shiftDown &&
                               state.lastClickPath == clickKey &&
                               state.lastClickSeconds >= 0.0 &&
                               frameSeconds_ - state.lastClickSeconds <= 0.45;
      state.lastClickPath = clickKey;
      state.lastClickSeconds = frameSeconds_;

      if (doubleClick && nameHovered) {
        state.renaming = true;
        state.renamePath = entry.path;
        state.renameText = entry.name;
      } else if (entry.directory && doubleClick && iconHovered) {
        cancelRename(state);
        state.selectedPaths.clear();
        state.selectionAnchorPath.clear();
        state.contextMenuOpen = false;
        state.currentPath = entry.path;
        state.pathText = pathTextForUi(state.currentPath);
        state.cachedPath.clear();
      } else {
        if (state.renaming && state.renamePath != entry.path) cancelRename(state);
        if (shiftDown) selectRange(state, entry.path);
        else if (ctrlDown) toggleSelected(state, entry.path);
        else selectOnly(state, entry.path);
      }
    }

    if (rightMousePressed_ && hovered && !mouseInContextMenu) {
      rightClickedAnyFileItem = true;
      if (!isSelected(state, entry.path)) selectOnly(state, entry.path);
      state.contextMenuOpen = true;
      state.contextMenuX = mouseX_;
      state.contextMenuY = mouseY_;
      cancelRename(state);
    }

    if (iconView) {
      entry.directory ? drawFolderIcon(ctx, icon) : drawFileIcon(ctx, icon);
      if (state.renaming && state.renamePath == entry.path) {
        UI_TextInputOptions renameOptions;
        renameOptions.placeholder = "Name";
        UI_TextInput(id + ".rename", nameRect, renameOptions, state.renameText, fieldStyle);
      } else {
        drawWrappedLabel(ctx, buttonResources_, smallText, nameRect, entry.name);
      }
    } else {
      entry.directory ? drawFolderIcon(ctx, icon) : drawFileIcon(ctx, icon);
      if (state.renaming && state.renamePath == entry.path) {
        UI_TextInputOptions renameOptions;
        renameOptions.placeholder = "Name";
        UI_TextInput(id + ".rename", nameRect, renameOptions, state.renameText, fieldStyle);
      } else {
        drawText(ctx, buttonResources_, smallText, nameRect, entry.name);
      }
      if (state.view == UI_FileDialogView::Details) {
        drawText(ctx, buttonResources_, smallText, BLRect(item.x + 270.0, item.y, 88.0, item.h), entry.directory ? "" : formatSize(entry.size), 0xFF475569u);
        drawText(ctx, buttonResources_, smallText, BLRect(item.x + 370.0, item.y, 150.0, item.h), formatFileTime(entry.path), 0xFF475569u);
        drawText(ctx, buttonResources_, smallText, BLRect(item.x + 530.0, item.y, 120.0, item.h), entry.type, 0xFF475569u);
      }
    }
  }
  ctx.restore();

  if (clickedInFilesPanel && !clickedAnyFileItem) {
    cancelRename(state);
    state.contextMenuOpen = false;
    if (!ctrlDown && !shiftDown) {
      state.selectedPaths.clear();
      state.filename.clear();
    }
    state.draggingSelection = true;
    state.selectionStartX = mouseX_;
    state.selectionStartY = mouseY_;
  }

  if (rightClickedInFilesPanel && !rightClickedAnyFileItem) {
    state.contextMenuOpen = true;
    state.contextMenuX = mouseX_;
    state.contextMenuY = mouseY_;
  }

  if (state.draggingSelection && mouseReleased_) {
    state.draggingSelection = false;
  }

  if (showFileScrollbar) {
    const double drawThumbY = fileScrollbarTrack.y + (fileScrollbarTrack.h - fileThumbH) * (state.fileScroll / maxFileScroll);
    const BLRect drawThumb(fileScrollbarTrack.x, drawThumbY, fileScrollbarTrack.w, fileThumbH);
    ctx.set_fill_style(BLRgba32(0xFFE2E8F0u));
    ctx.fill_round_rect(BLRoundRect(fileScrollbarTrack.x, fileScrollbarTrack.y, fileScrollbarTrack.w, fileScrollbarTrack.h, 3.5));
    ctx.set_fill_style(BLRgba32(state.draggingFileScrollbar ? 0xFF64748Bu : 0xFF94A3B8u));
    ctx.fill_round_rect(BLRoundRect(drawThumb.x, drawThumb.y, drawThumb.w, drawThumb.h, 3.5));
  }

  if (state.draggingSelection) {
    const BLRect drawMarquee = normalizedRect(state.selectionStartX, state.selectionStartY, mouseX_, mouseY_);
    ctx.set_fill_style(BLRgba32(0x332563EBu));
    ctx.fill_rect(drawMarquee);
    ctx.set_stroke_style(BLRgba32(0xFF2563EBu));
    ctx.set_stroke_width(1.0);
    ctx.stroke_rect(drawMarquee);
  }

  if (state.contextMenuOpen && mousePressed_ && !contains(activeContextMenuRect, mouseX_, mouseY_)) {
    state.contextMenuOpen = false;
  }
  if (state.contextMenuOpen) {
    const BLRect menuRect = activeContextMenuRect;
    ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
    ctx.fill_round_rect(BLRoundRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 6.0));
    ctx.set_stroke_style(BLRgba32(0xFF94A3B8u));
    ctx.set_stroke_width(1.0);
    ctx.stroke_round_rect(BLRoundRect(menuRect.x + 0.5, menuRect.y + 0.5, menuRect.w - 1.0, menuRect.h - 1.0, 6.0));

    const BLRect copyRect(menuRect.x + 4.0, menuRect.y + 4.0, menuRect.w - 8.0, 24.0);
    const BLRect pasteRect(menuRect.x + 4.0, menuRect.y + 30.0, menuRect.w - 8.0, 24.0);
    const BLRect deleteRect(menuRect.x + 4.0, menuRect.y + 56.0, menuRect.w - 8.0, 24.0);
    const BLRect undoRect(menuRect.x + 4.0, menuRect.y + 82.0, menuRect.w - 8.0, 24.0);

    if (UI_Button(id + ".menu-copy", copyRect, buttonStyle, UI_ButtonContent("Copy")) == UI_ButtonActionPressed) {
      state.clipboardPaths = state.selectedPaths;
      state.contextMenuOpen = false;
    }
    if (UI_Button(id + ".menu-paste", pasteRect, buttonStyle, UI_ButtonContent("Paste")) == UI_ButtonActionPressed) {
      copySelectedTo(state, state.currentPath);
      state.contextMenuOpen = false;
    }
    if (UI_Button(id + ".menu-delete", deleteRect, buttonStyle, UI_ButtonContent("Delete")) == UI_ButtonActionPressed) {
      deleteSelectionToTrash(state);
      state.contextMenuOpen = false;
    }
    if (UI_Button(id + ".menu-undo", undoRect, buttonStyle, UI_ButtonContent("Undo Delete")) == UI_ButtonActionPressed) {
      undoTrashDelete(state);
      state.contextMenuOpen = false;
    }
  }

  if (state.renaming) {
    for (const UI_TextInputKeyEvent& event : keyEvents_) {
      if (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER) {
        commitRename(state);
        break;
      }
      if (event.key == SDLK_ESCAPE) {
        cancelRename(state);
        break;
      }
    }
  }

  drawText(ctx, buttonResources_, smallText, BLRect(rect.x + margin, controlsY, 76.0, 32.0), "Filename:");
  UI_TextInputOptions fileNameOptions;
  fileNameOptions.placeholder = "File name";
  UI_TextInput(id + ".filename", BLRect(rect.x + margin + 82.0, controlsY, rect.w - margin * 2.0 - 82.0, 32.0), fileNameOptions, state.filename, fieldStyle);

  const double typeY = controlsY + 40.0;
  drawText(ctx, buttonResources_, smallText, BLRect(rect.x + margin, typeY, 76.0, 32.0), options.mode == UI_FileDialogMode::Save ? "Save as:" : "Load type:");
  const BLRect comboRect(rect.x + margin + 82.0, typeY, rect.w - margin * 2.0 - 82.0, 32.0);
  const std::string filterText = options.filters.empty()
                                     ? "All files (*.*)"
                                     : options.filters[std::min(state.filterIndex, options.filters.size() - 1)].label + " (" +
                                           options.filters[std::min(state.filterIndex, options.filters.size() - 1)].pattern + ")";
  const size_t filterCount = options.filters.empty() ? 1 : options.filters.size();
  const double dropdownRowH = 28.0;
  const BLRect dropdownRect(comboRect.x, comboRect.y + comboRect.h, comboRect.w, static_cast<double>(filterCount) * dropdownRowH);
  const bool comboClickOutside = state.typeComboOpen &&
                                 mousePressed_ &&
                                 !contains(comboRect, mouseX_, mouseY_) &&
                                 !contains(dropdownRect, mouseX_, mouseY_);
  if (UI_Button(id + ".type-combo", comboRect, buttonStyle, UI_ButtonContent("")) == UI_ButtonActionPressed) {
    state.typeComboOpen = !state.typeComboOpen;
  }
  drawText(ctx, buttonResources_, smallText, BLRect(comboRect.x + 10.0, comboRect.y, comboRect.w - 20.0, comboRect.h), filterText, 0xFF0F172Au);
  if (comboClickOutside) {
    state.typeComboOpen = false;
  }

  const double buttonsY = rect.y + rect.h - 42.0;
  const BLRect cancelRect(rect.x + rect.w - margin - 96.0, buttonsY, 96.0, 30.0);
  const BLRect acceptRect(cancelRect.x - 108.0, buttonsY, 96.0, 30.0);
  const bool dropdownOverFooter = state.typeComboOpen && contains(dropdownRect, mouseX_, mouseY_);
  if (!comboClickOutside && !dropdownOverFooter) {
    if (UI_Button(id + ".cancel", cancelRect, buttonStyle, UI_ButtonContent("Cancel")) == UI_ButtonActionPressed) {
      return finishDialog(UI_FileDialogResult::Cancelled);
    }
    if (UI_Button(id + ".accept", acceptRect, primaryStyle, UI_ButtonContent(options.mode == UI_FileDialogMode::Save ? "Save" : "Load")) == UI_ButtonActionPressed) {
      if (!state.filename.empty()) {
        selectedPath = pathTextForUi(state.currentPath / state.filename);
        return finishDialog(UI_FileDialogResult::Accepted);
      }
    }
  }

  if (state.typeComboOpen) {
    ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
    ctx.fill_round_rect(BLRoundRect(dropdownRect.x, dropdownRect.y, dropdownRect.w, dropdownRect.h, 6.0));
    ctx.set_stroke_style(BLRgba32(0xFF94A3B8u));
    ctx.set_stroke_width(1.0);
    ctx.stroke_round_rect(BLRoundRect(dropdownRect.x + 0.5, dropdownRect.y + 0.5, dropdownRect.w - 1.0, dropdownRect.h - 1.0, 6.0));

    for (size_t i = 0; i < filterCount; ++i) {
      const BLRect row(comboRect.x, comboRect.y + comboRect.h + static_cast<double>(i) * dropdownRowH, comboRect.w, dropdownRowH);
      const std::string label = options.filters.empty() ? "All files (*.*)" : options.filters[i].label + " (" + options.filters[i].pattern + ")";
      if (UI_Button(id + ".type." + std::to_string(i), row, buttonStyle, UI_ButtonContent("")) == UI_ButtonActionPressed) {
        state.filterIndex = i;
        state.entries.clear();
        state.cachedPath.clear();
        state.cachedPattern.clear();
        state.scanJob.reset();
        state.scanning = false;
        state.scanComplete = false;
        state.fileScroll = 0.0;
        state.typeComboOpen = false;
      }
      drawText(ctx, buttonResources_, smallText, BLRect(row.x + 10.0, row.y, row.w - 20.0, row.h), label, 0xFF0F172Au);
    }
  }

  const bool acceptEnter = focusedTextInputId_ == id + ".filename" &&
                           std::any_of(keyEvents_.begin(), keyEvents_.end(), [](const UI_TextInputKeyEvent& event) {
                             return event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER;
                           });
  if (acceptEnter && !state.filename.empty()) {
    selectedPath = pathTextForUi(state.currentPath / state.filename);
    return finishDialog(UI_FileDialogResult::Accepted);
  }

  ctx.set_stroke_style(BLRgba32(0xFF94A3B8u));
  ctx.set_stroke_width(1.2);
  for (int i = 0; i < 3; ++i) {
    const double offset = 5.0 + static_cast<double>(i) * 5.0;
    ctx.stroke_line(BLLine(rect.x + rect.w - offset, rect.y + rect.h - 3.0, rect.x + rect.w - 3.0, rect.y + rect.h - offset));
  }

  return finishDialog(UI_FileDialogResult::None);
}

UI_FileDialogResult showDialog(SceneRenderer& renderer,
                               const std::string& id,
                               const UI_FileDialogOptions& options,
                               std::string& selectedFilePath) {
  UI_FileDialogOptions resolvedOptions = options;
  if (resolvedOptions.startPath.empty()) {
    resolvedOptions.startPath = defaultDialogStartPath();
  }
  return renderer.UI_FileDialog(id, defaultDialogRect(renderer), resolvedOptions, selectedFilePath);
}

UI_FileDialogResult renderFileDialog(SceneRenderer& renderer,
                                     const std::string& id,
                                     bool& showFileDialog,
                                     bool openedFileDialogThisFrame,
                                     const UI_FileDialogOptions& options,
                                     std::string& selectedFilePath) {
  if (!showFileDialog || openedFileDialogThisFrame) return UI_FileDialogResult::None;

  const UI_FileDialogResult result = showDialog(renderer, id, options, selectedFilePath);
  if (result == UI_FileDialogResult::Accepted || result == UI_FileDialogResult::Cancelled) {
    showFileDialog = false;
  }
  return result;
}
#endif

}  // namespace Blend2DUI
