#define BLEND2DUI_FILE_DIALOG_TEST_ACCESS
#include "../src/FileDialog.cpp"

#include <fstream>
#include <iostream>

int main() {
  namespace fs = std::filesystem;
  using namespace Blend2DUI;

#ifdef _WIN32
  if (pathTextForUi(fs::path("C:\\Users\\tskil\\Documents\\example.txt")) != "C:/Users/tskil/Documents/example.txt") {
    std::cerr << "Expected UI path formatting to use forward slashes\n";
    return 1;
  }
#endif

  const fs::path root = fs::temp_directory_path() / "blend2dui_filter_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "Sub Folder", ec);
  fs::create_directories(root / "Another Folder", ec);
  std::ofstream(root / "image.png").put('a');
  std::ofstream(root / "vector.svg").put('a');
  std::ofstream(root / "notes.txt").put('a');
  std::ofstream(root / "archive.zip").put('a');

  auto runFilter = [&](const std::string& pattern, bool zipExpected) {
    auto job = std::make_shared<UI_FileDialogScanJob>();
    scanDirectory(root, pattern, job);
    const bool hasSubFolder = std::any_of(job->entries.begin(), job->entries.end(), [&](const UI_FileDialogEntry& entry) {
      return entry.directory && entry.name == "Sub Folder";
    });
    const bool hasAnotherFolder = std::any_of(job->entries.begin(), job->entries.end(), [&](const UI_FileDialogEntry& entry) {
      return entry.directory && entry.name == "Another Folder";
    });
    if (!hasSubFolder || !hasAnotherFolder) {
      std::cerr << "Missing folders for filter " << pattern << "\n";
      for (const auto& entry : job->entries) {
        std::cerr << "  " << (entry.directory ? "dir " : "file") << entry.name << "\n";
      }
      return false;
    }
    const bool hasZip = std::any_of(job->entries.begin(), job->entries.end(), [&](const UI_FileDialogEntry& entry) {
      return !entry.directory && entry.name == "archive.zip";
    });
    if (hasZip != zipExpected) {
      std::cerr << "Unexpected zip visibility for filter " << pattern << "\n";
      return false;
    }
    return true;
  };

  const bool ok = runFilter("*.*", true) && runFilter("*.png", false) && runFilter("*.svg", false) && runFilter("*.txt", false);
  fs::remove_all(root, ec);
  return ok ? 0 : 1;
}
