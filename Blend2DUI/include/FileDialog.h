#pragma once

#include <blend2d/blend2d.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Blend2DUI {

class SceneRenderer;

enum class UI_FileDialogMode {
  Open,
  Save
};

enum class UI_FileDialogResult {
  None,
  Accepted,
  Cancelled
};

enum class UI_FileDialogView {
  LargeIcons,
  MediumIcons,
  SmallIcons,
  List,
  Details
};

struct UI_FileTypeFilter {
  std::string label;
  std::string pattern;
};

struct UI_FileDialogOptions {
  UI_FileDialogMode mode = UI_FileDialogMode::Open;
  std::string title;
  std::filesystem::path startPath;
  std::vector<UI_FileTypeFilter> filters;
  std::string defaultFileName;
};

struct UI_FileDialogEntry {
  std::filesystem::path path;
  std::string name;
  std::string type;
  uintmax_t size = 0;
  bool directory = false;
};

struct UI_FileDialogScanJob {
  std::atomic<bool> done{false};
  std::vector<UI_FileDialogEntry> entries;
  std::mutex mutex;
};

struct UI_FileDialogTrashRecord {
  std::filesystem::path originalPath;
  std::filesystem::path trashPath;
};

struct UI_FileDialogState {
  bool initialized = false;
  bool typeComboOpen = false;
  UI_FileDialogView view = UI_FileDialogView::MediumIcons;
  std::filesystem::path currentPath;
  std::filesystem::path pendingPath;
  std::string pathText;
  std::string filename;
  std::string renameText;
  std::filesystem::path renamePath;
  std::string lastClickPath;
  std::filesystem::path selectionAnchorPath;
  std::vector<std::filesystem::path> selectedPaths;
  std::vector<std::filesystem::path> clipboardPaths;
  std::vector<UI_FileDialogTrashRecord> lastTrashDelete;
  size_t filterIndex = 0;
  double quickScroll = 0.0;
  double fileScroll = 0.0;
  double fileScrollbarDragOffsetY = 0.0;
  double dialogDragOffsetX = 0.0;
  double dialogDragOffsetY = 0.0;
  double dialogResizeStartMouseX = 0.0;
  double dialogResizeStartMouseY = 0.0;
  double dialogResizeStartW = 0.0;
  double dialogResizeStartH = 0.0;
  double selectionStartX = 0.0;
  double selectionStartY = 0.0;
  double contextMenuX = 0.0;
  double contextMenuY = 0.0;
  double lastClickSeconds = -1.0;
  BLRect dialogRect;
  std::vector<UI_FileDialogEntry> entries;
  std::filesystem::path cachedPath;
  std::string cachedPattern;
  std::shared_ptr<UI_FileDialogScanJob> scanJob;
  bool dialogRectInitialized = false;
  bool hasPendingPath = false;
  bool renaming = false;
  bool draggingDialog = false;
  bool resizingDialog = false;
  bool draggingFileScrollbar = false;
  bool draggingSelection = false;
  bool contextMenuOpen = false;
  bool scanning = false;
  bool scanComplete = false;
};

UI_FileDialogResult showDialog(SceneRenderer& renderer,
                               const std::string& id,
                               const UI_FileDialogOptions& options,
                               std::string& selectedFilePath);

UI_FileDialogResult renderFileDialog(SceneRenderer& renderer,
                                     const std::string& id,
                                     bool& showFileDialog,
                                     bool openedFileDialogThisFrame,
                                     const UI_FileDialogOptions& options,
                                     std::string& selectedFilePath);

}  // namespace Blend2DUI
