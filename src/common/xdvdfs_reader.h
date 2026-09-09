// Bounded, read-only XDVDFS directory reader.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/game_image_reader.h"

namespace thps::image {

enum class XdvdfsStatus {
  kOk,
  kNotXdvdfs,
  kReadError,
  kTruncated,
  kInvalidMetadata,
  kLimitExceeded,
  kPathNotFound,
  kNotDirectory,
  kNotFile,
};

struct XdvdfsEntry {
  std::string name;
  bool is_directory = false;
  uint64_t offset = 0;
  uint64_t size = 0;
};

class XdvdfsReader {
 public:
  // Opens only the layouts explicitly used by the existing DiscImageDevice.
  // They are compatibility candidates, not a claim that every Xbox image has
  // one of these offsets.
  XdvdfsStatus Open(GameImageReader& image);

  XdvdfsStatus ListDirectory(std::string_view path, std::vector<XdvdfsEntry>* entries) const;
  XdvdfsStatus FindFile(std::string_view path, XdvdfsEntry* entry) const;
  ReadResult ReadFileRange(const XdvdfsEntry& file, uint64_t file_offset,
                           std::span<uint8_t> buffer) const;

  uint64_t game_offset() const { return game_offset_; }
  uint64_t root_sector() const { return root_sector_; }

 private:
  struct Directory {
    uint64_t offset = 0;
    uint64_t size = 0;
  };
  struct RawEntry {
    uint16_t left = 0;
    uint16_t right = 0;
    uint32_t sector = 0;
    uint32_t length = 0;
    bool is_directory = false;
    std::string name;
  };

  static constexpr uint64_t kSectorSize = 2048;
  static constexpr uint64_t kFilesystemSector = 32;
  static constexpr uint64_t kMaxDirectoryBytes = 32ull * 1024 * 1024;
  static constexpr size_t kMaxDirectoryEntries = 8192;
  static constexpr size_t kMaxPathDepth = 64;

  XdvdfsStatus ReadDirectoryEntry(const Directory& directory, uint16_t ordinal,
                                  RawEntry* entry) const;
  XdvdfsStatus ListDirectory(const Directory& directory, std::vector<XdvdfsEntry>* entries) const;
  XdvdfsStatus FindChild(const Directory& directory, std::string_view name,
                         RawEntry* entry) const;
  XdvdfsStatus ResolveDirectory(std::string_view path, Directory* directory) const;
  XdvdfsStatus EntryToDirectory(const RawEntry& entry, Directory* directory) const;
  XdvdfsStatus EntryToFile(const RawEntry& entry, XdvdfsEntry* file) const;

  GameImageReader* image_ = nullptr;
  uint64_t game_offset_ = 0;
  uint64_t root_sector_ = 0;
  Directory root_{};
};

}  // namespace thps::image
