// Test-only random-access reader. Test fixtures never enter a release APK.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "common/game_image_reader.h"

namespace thps::image::test {

class MemoryGameImageReader final : public GameImageReader {
 public:
  explicit MemoryGameImageReader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

  uint64_t GetSize() const override { return bytes_.size(); }

  ReadResult ReadAt(uint64_t offset, std::span<uint8_t> buffer) override {
    if (buffer.empty()) return {offset <= GetSize() ? ReadStatus::kOk : ReadStatus::kInvalidRange, 0};
    if (offset > std::numeric_limits<uint64_t>::max() - buffer.size()) {
      return {ReadStatus::kOverflow, 0};
    }
    if (offset > GetSize()) return {ReadStatus::kInvalidRange, 0};
    const size_t count = static_cast<size_t>(std::min<uint64_t>(GetSize() - offset, buffer.size()));
    std::copy_n(bytes_.data() + static_cast<size_t>(offset), count, buffer.data());
    return {count == buffer.size() ? ReadStatus::kOk : ReadStatus::kEof, count};
  }

 private:
  std::vector<uint8_t> bytes_;
};

}  // namespace thps::image::test
