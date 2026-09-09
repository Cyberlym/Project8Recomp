// Random-access source for a user-supplied game image.
//
// Parsers consume this interface instead of paths so desktop files, Android
// file descriptors and in-memory test data have the same bounded-read contract.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>

namespace thps::image {

enum class ReadStatus {
  kOk,
  kEof,
  kInvalidRange,
  kOverflow,
  kIoError,
  kNotSeekable,
};

struct ReadResult {
  ReadStatus status = ReadStatus::kIoError;
  size_t bytes_read = 0;

  bool ok() const { return status == ReadStatus::kOk; }
};

class GameImageReader {
 public:
  virtual ~GameImageReader() = default;

  virtual uint64_t GetSize() const = 0;

  // A successful read always fills buffer. A read reaching EOF may return the
  // available prefix with kEof; parsers must use ReadExact for metadata.
  virtual ReadResult ReadAt(uint64_t offset, std::span<uint8_t> buffer) = 0;
};

// Converts short reads into an error for headers, directory metadata and names.
ReadStatus ReadExact(GameImageReader& reader, uint64_t offset, std::span<uint8_t> buffer);

class HostGameImageReader final : public GameImageReader {
 public:
  static ReadStatus Open(const std::filesystem::path& path,
                         std::unique_ptr<HostGameImageReader>* out_reader);

  uint64_t GetSize() const override { return size_; }
  ReadResult ReadAt(uint64_t offset, std::span<uint8_t> buffer) override;

 private:
  HostGameImageReader(std::ifstream stream, uint64_t size)
      : stream_(std::move(stream)), size_(size) {}

  std::ifstream stream_;
  uint64_t size_ = 0;
};

}  // namespace thps::image
