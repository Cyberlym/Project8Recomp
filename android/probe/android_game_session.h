#pragma once

#include <filesystem>

namespace thps::android {

// A gameplay launch names a completed app-private installation. The ISO is
// intentionally absent from this handoff: Runtime mounts the extracted tree
// through HostPathDevice and can therefore start after the SAF grant is gone.
bool PrepareGameplaySession(const std::filesystem::path& game_root);
bool HasPreparedGameplaySession();
std::filesystem::path GameplayGameRoot();

}  // namespace thps::android
