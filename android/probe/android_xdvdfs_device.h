#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include <rex/filesystem/device.h>

#include "common/game_image_reader.h"
#include "common/xdvdfs_reader.h"

namespace thps::android {

// The device owns the reader, so every Entry/File remains valid until Runtime
// destroys the mounted device. The caller transfers one detached SAF FD into
// the reader; no descriptor borrowed from validation is retained.
class AndroidXdvdfsDevice final : public rex::filesystem::Device {
 public:
  AndroidXdvdfsDevice(std::string_view mount_path,
                      std::unique_ptr<thps::image::GameImageReader> image);
  ~AndroidXdvdfsDevice() override;

  bool Initialize() override;
  void Dump(rex::string::StringBuffer* string_buffer) override;
  rex::filesystem::Entry* ResolvePath(std::string_view path) override;
  thps::image::ReadResult ReadFile(const thps::image::XdvdfsEntry& file, uint64_t offset,
                                   std::span<uint8_t> buffer);
  thps::image::XdvdfsStatus ListDirectory(std::string_view path,
                                          std::vector<thps::image::XdvdfsEntry>* entries) const;

  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 255; }
  uint32_t total_allocation_units() const override { return 0; }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 2048; }

 private:
  std::string name_ = "XDVDFS";
  std::unique_ptr<thps::image::GameImageReader> image_;
  thps::image::XdvdfsReader xdvdfs_;
  std::unique_ptr<rex::filesystem::Entry> root_entry_;
};

}  // namespace thps::android
