#include "android_game_install.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <sys/statvfs.h>
#include <vector>

#include "android_fd_game_image_reader.h"
#include "android_gameplay_log.h"
#include "common/supported_dumps.h"
#include "common/xex_identity.h"
#include "common/xdvdfs_reader.h"

namespace thps::android {
namespace {

constexpr size_t kCopyChunkSize = 256 * 1024;
constexpr size_t kMaxTreeEntries = 500000;
constexpr uint64_t kInstallHeadroom = 128ull * 1024 * 1024;

struct SourceFile {
  std::string relative_path;
  thps::image::XdvdfsEntry entry;
};

bool IsSafeName(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("\\/") == std::string_view::npos;
}

bool IsSupported(const thps::image::XexIdentity& identity) {
  if (!identity.has_execution_info || identity.title_id != 0x415607DD) return false;
  const std::string digest = thps::image::Sha256Hex(identity.sha256);
  for (const auto& dump : thps::identify::kSupportedDumps) {
    if (identity.size == dump.xex_size && digest == dump.sha256) return true;
  }
  return false;
}

bool CollectFiles(const thps::image::XdvdfsReader& xdvdfs, std::string_view directory,
                  std::vector<std::string>* directories, std::vector<SourceFile>* files,
                  uint64_t* total, std::string* error) {
  std::vector<thps::image::XdvdfsEntry> entries;
  if (xdvdfs.ListDirectory(directory, &entries) != thps::image::XdvdfsStatus::kOk) {
    *error = "The game image directory tree could not be read.";
    return false;
  }
  for (const auto& entry : entries) {
    if (!IsSafeName(entry.name) || files->size() + directories->size() >= kMaxTreeEntries) {
      *error = "The game image contains an unsafe or excessively large file tree.";
      return false;
    }
    const std::string relative = directory.empty() ? entry.name :
        std::string(directory) + "/" + entry.name;
    if (entry.is_directory) {
      directories->push_back(relative);
      if (!CollectFiles(xdvdfs, relative, directories, files, total, error)) return false;
      continue;
    }
    if (entry.size > std::numeric_limits<uint64_t>::max() - *total) {
      *error = "The game image reports an invalid file size.";
      return false;
    }
    *total += entry.size;
    files->push_back({relative, entry});
  }
  return true;
}

bool HasSpace(const std::filesystem::path& root, uint64_t needed) {
  struct statvfs stat{};
  if (statvfs(root.c_str(), &stat) != 0) return false;
  const uint64_t available = uint64_t(stat.f_bavail) * uint64_t(stat.f_frsize);
  return available >= needed && available - needed >= kInstallHeadroom;
}

bool CopyFile(const thps::image::XdvdfsReader& xdvdfs, const SourceFile& source,
              const std::filesystem::path& root, uint64_t* copied, uint64_t total,
              const GameInstallProgress& progress, std::string* error) {
  const std::filesystem::path target = root / source.relative_path;
  std::error_code ec;
  // relative_path is formed solely from XDVDFS names accepted by IsSafeName,
  // so it cannot escape staging_root. Avoid canonicalizing a target that does
  // not exist yet (and avoid treating similarly prefixed sibling paths as it).
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec) { *error = "The installation directory could not be created."; return false; }
  std::ofstream output(target, std::ios::binary | std::ios::trunc);
  if (!output) { *error = "A game file could not be created."; return false; }
  std::array<uint8_t, kCopyChunkSize> buffer{};
  for (uint64_t offset = 0; offset < source.entry.size;) {
    const size_t count = static_cast<size_t>(std::min<uint64_t>(buffer.size(), source.entry.size - offset));
    const auto read = xdvdfs.ReadFileRange(source.entry, offset, std::span(buffer.data(), count));
    if (!read.ok() || read.bytes_read != count) { *error = "A game file could not be read from the image."; return false; }
    output.write(reinterpret_cast<const char*>(buffer.data()), count);
    if (!output) { *error = "The device stopped writing the game installation."; return false; }
    offset += count;
    *copied += count;
    if (progress) progress(*copied, total, source.relative_path);
  }
  return true;
}

bool HashFile(const std::filesystem::path& path, uint64_t* size, std::string* digest) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  thps::image::Sha256Hasher hasher;
  std::array<uint8_t, 64 * 1024> buffer{};
  uint64_t bytes = 0;
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    const std::streamsize count = input.gcount();
    if (count > 0) { hasher.Update(buffer.data(), static_cast<size_t>(count)); bytes += static_cast<uint64_t>(count); }
  }
  if (!input.eof()) return false;
  *size = bytes;
  *digest = thps::image::Sha256Hex(hasher.Final());
  return true;
}

}  // namespace

GameInstallResult ValidateInstalledGame(const std::filesystem::path& game_root) {
  GameInstallResult result;
  uint64_t size = 0;
  std::string digest;
  if (!std::filesystem::is_directory(game_root) ||
      !HashFile(game_root / "default.xex", &size, &digest)) {
    result.message = "The installed game files are incomplete.";
    return result;
  }
  for (const auto& dump : thps::identify::kSupportedDumps) {
    if (size == dump.xex_size && digest == dump.sha256) {
      result.success = true;
      result.message = "Tony Hawk's Project 8 is installed.";
      return result;
    }
  }
  result.message = "The installed game files do not match the supported game.";
  return result;
}

GameInstallResult InstallGameFromFd(int owned_fd, const std::filesystem::path& staging_root,
                                    GameInstallProgress progress) {
  GameInstallResult result;
  std::unique_ptr<thps::image::AndroidFdGameImageReader> image;
  if (thps::image::AndroidFdGameImageReader::Adopt(owned_fd, &image) != thps::image::ReadStatus::kOk) {
    result.message = "Android could not open the selected game image.";
    return result;
  }
  thps::image::XdvdfsReader xdvdfs;
  thps::image::XdvdfsEntry default_xex;
  thps::image::XexIdentity identity;
  if (xdvdfs.Open(*image) != thps::image::XdvdfsStatus::kOk ||
      xdvdfs.FindFile("default.xex", &default_xex) != thps::image::XdvdfsStatus::kOk ||
      thps::image::IdentifyXex(*image, xdvdfs, default_xex, &identity) != thps::image::XexIdentityStatus::kOk ||
      !IsSupported(identity)) {
    result.message = "The selected image is not the supported Tony Hawk's Project 8 disc.";
    return result;
  }
  std::vector<SourceFile> files;
  std::vector<std::string> directories;
  std::string error;
  if (!CollectFiles(xdvdfs, "", &directories, &files, &result.total_bytes, &error)) {
    result.message = error; return result;
  }
  const auto storage_root = staging_root.parent_path();
  if (!HasSpace(storage_root, result.total_bytes)) {
    result.message = "There is not enough free space to install the game.";
    return result;
  }
  LogGameplayEvent("P8_INSTALL_BEGIN files=%zu bytes=%llu", files.size(),
                   static_cast<unsigned long long>(result.total_bytes));
  for (const auto& directory : directories) {
    std::error_code ec;
    std::filesystem::create_directories(staging_root / directory, ec);
    if (ec) {
      result.message = "The installation directory could not be created.";
      return result;
    }
  }
  const auto started = std::chrono::steady_clock::now();
  auto last_progress = started;
  uint64_t last_reported = std::numeric_limits<uint64_t>::max();
  auto report_progress = [&](uint64_t copied, uint64_t total, const std::string& file) {
    if (!progress) return;
    const auto now = std::chrono::steady_clock::now();
    if (copied != total && copied == last_reported && now - last_progress < std::chrono::milliseconds(250)) return;
    if (copied != total && now - last_progress < std::chrono::milliseconds(250)) return;
    last_progress = now;
    last_reported = copied;
    progress(copied, total, file);
  };
  for (const auto& file : files) {
    if (!CopyFile(xdvdfs, file, staging_root, &result.copied_bytes, result.total_bytes,
                  report_progress, &error)) { result.message = error; return result; }
  }
  // The source XEX was validated for title ID and hash before copy. Verify the
  // extracted bytes again; a matching supported hash proves the final default.xex.
  const auto installed = ValidateInstalledGame(staging_root);
  if (!installed.success) { result.message = installed.message; return result; }
  result.success = true;
  result.message = installed.message;
  LogGameplayEvent("P8_INSTALL_READY bytes=%llu", static_cast<unsigned long long>(result.copied_bytes));
  return result;
}

}  // namespace thps::android
