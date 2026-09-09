#include "android_game_session.h"

#include <mutex>

#include "android_game_install.h"
#include "android_gameplay_log.h"

namespace thps::android {
namespace {

std::mutex g_session_mutex;
std::filesystem::path g_game_root;

}  // namespace

bool PrepareGameplaySession(const std::filesystem::path& game_root) {
  const auto validation = ValidateInstalledGame(game_root);
  if (!validation.success) {
    LogGameplayEvent("P8_GAME_SESSION_FAIL reason=installed_game_invalid");
    return false;
  }
  std::lock_guard<std::mutex> lock(g_session_mutex);
  g_game_root = game_root;
  LogGameplayEvent("P8_GAME_SESSION_READY source=installed_game");
  return true;
}

bool HasPreparedGameplaySession() {
  std::lock_guard<std::mutex> lock(g_session_mutex);
  return !g_game_root.empty();
}

std::filesystem::path GameplayGameRoot() {
  std::lock_guard<std::mutex> lock(g_session_mutex);
  return g_game_root;
}

}  // namespace thps::android
