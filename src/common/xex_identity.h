// Read-only identity of an XEX stored inside an image; it never loads a module.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "common/game_image_reader.h"
#include "common/xdvdfs_reader.h"

namespace thps::image {

enum class XexIdentityStatus { kOk, kReadError, kInvalidXex };

// Incremental so an image file can be verified while it is copied without
// retaining its contents in memory.
class Sha256Hasher {
 public:
  Sha256Hasher();
  void Update(const uint8_t* data, size_t size);
  std::array<uint8_t, 32> Final();

 private:
  void Transform();
  std::array<uint32_t, 8> state_;
  std::array<uint8_t, 64> block_{};
  size_t block_size_ = 0;
  uint64_t bit_count_ = 0;
};

struct XexIdentity {
  uint64_t size = 0;
  std::array<uint8_t, 32> sha256{};
  bool has_execution_info = false;
  uint32_t media_id = 0;
  uint32_t version = 0;
  uint32_t base_version = 0;
  uint32_t title_id = 0;
};

// Streams the file through bounded ranges. The optional-header layout and key
// match ReXGlue's xex2_info.h; unlike XexModule this does not require guest
// memory or execute/load the image.
XexIdentityStatus IdentifyXex(GameImageReader& image, const XdvdfsReader& xdvdfs,
                              const XdvdfsEntry& file, XexIdentity* identity);
std::string Sha256Hex(const std::array<uint8_t, 32>& digest);

}  // namespace thps::image
