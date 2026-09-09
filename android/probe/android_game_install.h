#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace thps::android {

struct GameInstallResult {
  bool success = false;
  uint64_t copied_bytes = 0;
  uint64_t total_bytes = 0;
  std::string message;
};

using GameInstallProgress = std::function<void(uint64_t copied, uint64_t total,
                                               const std::string& current_file)>;

// Takes ownership of the detached SAF descriptor. The caller supplies an empty
// staging directory; this function never treats it as a playable installation.
GameInstallResult InstallGameFromFd(int owned_fd, const std::filesystem::path& staging_root,
                                    GameInstallProgress progress);

// Checks the final game directory without requiring the source ISO. A matching
// default.xex hash is the supported-build proof used by the desktop dump gate.
GameInstallResult ValidateInstalledGame(const std::filesystem::path& game_root);

}  // namespace thps::android
