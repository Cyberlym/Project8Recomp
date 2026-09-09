#pragma once

#include <memory>

#include "common/game_image_reader.h"

namespace thps::image {

// Owns the detached descriptor received from a SAF ParcelFileDescriptor.
class AndroidFdGameImageReader final : public GameImageReader {
 public:
  static ReadStatus Adopt(int owned_fd, std::unique_ptr<AndroidFdGameImageReader>* out_reader);
  ~AndroidFdGameImageReader() override;

  uint64_t GetSize() const override { return size_; }
  ReadResult ReadAt(uint64_t offset, std::span<uint8_t> buffer) override;

 private:
  AndroidFdGameImageReader(int owned_fd, uint64_t size) : fd_(owned_fd), size_(size) {}

  int fd_ = -1;
  uint64_t size_ = 0;
};

}  // namespace thps::image
