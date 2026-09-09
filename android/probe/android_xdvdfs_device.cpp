#include "android_xdvdfs_device.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

namespace thps::android {
namespace {

using rex::filesystem::Entry;
using rex::filesystem::File;
using rex::X_STATUS;

bool HasWriteAccess(uint32_t access) {
  using rex::filesystem::FileAccess;
  return (access & (FileAccess::kGenericWrite | FileAccess::kFileWriteData |
                    FileAccess::kFileAppendData)) != 0;
}

class AndroidXdvdfsEntry;

class AndroidXdvdfsFile final : public File {
 public:
  AndroidXdvdfsFile(uint32_t access, AndroidXdvdfsEntry* entry);
  void Destroy() override { delete this; }
  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t offset, size_t* out_read) override;
  X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t* out_written) override {
    if (out_written) *out_written = 0;
    return X_STATUS_ACCESS_DENIED;
  }
  X_STATUS SetLength(size_t) override { return X_STATUS_ACCESS_DENIED; }
};

class AndroidXdvdfsEntry final : public Entry {
 public:
  AndroidXdvdfsEntry(rex::filesystem::Device* device, Entry* parent, std::string_view path,
                     const thps::image::XdvdfsEntry* source)
      : Entry(device, parent, path) {
    if (!source) {
      attributes_ = rex::filesystem::kFileAttributeDirectory |
                    rex::filesystem::kFileAttributeReadOnly;
      return;
    }
    source_ = *source;
    has_source_ = true;
    attributes_ = (source_.is_directory ? rex::filesystem::kFileAttributeDirectory
                                         : rex::filesystem::kFileAttributeNormal) |
                  rex::filesystem::kFileAttributeReadOnly;
    if (!source_.is_directory) {
      if (source_.size > std::numeric_limits<size_t>::max()) {
        invalid_size_ = true;
        return;
      }
      size_ = static_cast<size_t>(source_.size);
      allocation_size_ = size_;
    }
  }

  X_STATUS Open(uint32_t desired_access, File** out_file) override {
    if (!out_file || !has_source_) return X_STATUS_NO_SUCH_FILE;
    if (invalid_size_ || HasWriteAccess(desired_access)) return X_STATUS_ACCESS_DENIED;
    // NtCreateFile is also the guest's directory-handle operation. QueryDirectory
    // obtains the directory from File::entry(), so a read-only XDVDFS directory
    // needs a File object even though ReadSync must never read its byte range.
    *out_file = new AndroidXdvdfsFile(desired_access, this);
    return X_STATUS_SUCCESS;
  }

  const thps::image::XdvdfsEntry& source() const { return source_; }
  void AddChild(std::unique_ptr<Entry> child) { children_.push_back(std::move(child)); }

 private:
  thps::image::XdvdfsEntry source_;
  bool has_source_ = false;
  bool invalid_size_ = false;
};

AndroidXdvdfsFile::AndroidXdvdfsFile(uint32_t access, AndroidXdvdfsEntry* entry)
    : File(access, entry) {}

X_STATUS AndroidXdvdfsFile::ReadSync(std::span<uint8_t> buffer, size_t offset,
                                     size_t* out_read) {
  if (out_read) *out_read = 0;
  if (!(file_access_ & (rex::filesystem::FileAccess::kGenericRead |
                        rex::filesystem::FileAccess::kFileReadData))) {
    return X_STATUS_ACCESS_DENIED;
  }
  auto* entry = static_cast<AndroidXdvdfsEntry*>(entry_);
  if (entry->source().is_directory) return X_STATUS_FILE_IS_A_DIRECTORY;
  const auto result = static_cast<AndroidXdvdfsDevice*>(entry->device())->ReadFile(
      entry->source(), offset, buffer);
  if (out_read) *out_read = result.bytes_read;
  if (result.status == thps::image::ReadStatus::kOk) return X_STATUS_SUCCESS;
  return result.status == thps::image::ReadStatus::kEof ? X_STATUS_END_OF_FILE
                                                        : X_STATUS_UNSUCCESSFUL;
}

bool Populate(AndroidXdvdfsDevice* device, AndroidXdvdfsEntry* parent,
              std::string_view directory, size_t depth) {
  if (depth > 64) return false;
  std::vector<thps::image::XdvdfsEntry> children;
  if (device->ListDirectory(directory, &children) != thps::image::XdvdfsStatus::kOk) {
    return false;
  }
  for (const auto& source : children) {
    const std::string path = parent->path().empty() ? source.name : parent->path() + "\\" + source.name;
    auto child = std::make_unique<AndroidXdvdfsEntry>(device, parent, path, &source);
    auto* child_raw = child.get();
    parent->AddChild(std::move(child));
    if (source.is_directory && !Populate(device, child_raw, path, depth + 1)) return false;
  }
  return true;
}

}  // namespace

AndroidXdvdfsDevice::AndroidXdvdfsDevice(
    std::string_view mount_path, std::unique_ptr<thps::image::GameImageReader> image)
    : Device(mount_path), image_(std::move(image)) {}

AndroidXdvdfsDevice::~AndroidXdvdfsDevice() = default;

bool AndroidXdvdfsDevice::Initialize() {
  if (!image_ || xdvdfs_.Open(*image_) != thps::image::XdvdfsStatus::kOk) return false;
  auto root = std::make_unique<AndroidXdvdfsEntry>(this, nullptr, "", nullptr);
  if (!Populate(this, root.get(), "", 0)) return false;
  root_entry_ = std::move(root);
  return true;
}

thps::image::ReadResult AndroidXdvdfsDevice::ReadFile(
    const thps::image::XdvdfsEntry& file, uint64_t offset, std::span<uint8_t> buffer) {
  return xdvdfs_.ReadFileRange(file, offset, buffer);
}

thps::image::XdvdfsStatus AndroidXdvdfsDevice::ListDirectory(
    std::string_view path, std::vector<thps::image::XdvdfsEntry>* entries) const {
  return xdvdfs_.ListDirectory(path, entries);
}

void AndroidXdvdfsDevice::Dump(rex::string::StringBuffer* string_buffer) {
  auto lock = global_critical_region_.Acquire();
  if (root_entry_) root_entry_->Dump(string_buffer, 0);
}

rex::filesystem::Entry* AndroidXdvdfsDevice::ResolvePath(std::string_view path) {
  auto lock = global_critical_region_.Acquire();
  return root_entry_ ? root_entry_->ResolvePath(path) : nullptr;
}

}  // namespace thps::android
