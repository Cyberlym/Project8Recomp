#include "common/xdvdfs_reader.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_set>

namespace thps::image {

namespace {

constexpr std::array<uint64_t, 5> kKnownLayoutCandidates = {
    0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000,
};
constexpr std::string_view kMagic = "MICROSOFT*XBOX*MEDIA";

bool Add(uint64_t left, uint64_t right, uint64_t* result) {
  if (right > std::numeric_limits<uint64_t>::max() - left) {
    return false;
  }
  *result = left + right;
  return true;
}

bool Multiply(uint64_t left, uint64_t right, uint64_t* result) {
  if (left && right > std::numeric_limits<uint64_t>::max() / left) {
    return false;
  }
  *result = left * right;
  return true;
}

uint16_t LoadLe16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8);
}

uint32_t LoadLe32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

bool RangeWithin(uint64_t offset, uint64_t size, uint64_t limit) {
  uint64_t end = 0;
  return Add(offset, size, &end) && end <= limit;
}

XdvdfsStatus MapRead(ReadStatus status) {
  return status == ReadStatus::kEof ? XdvdfsStatus::kTruncated : XdvdfsStatus::kReadError;
}

bool IsSafeComponent(const std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("\\/") == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

}  // namespace

XdvdfsStatus XdvdfsReader::Open(GameImageReader& image) {
  image_ = nullptr;
  root_ = {};
  game_offset_ = 0;
  root_sector_ = 0;

  for (const uint64_t candidate : kKnownLayoutCandidates) {
    uint64_t header_offset = 0;
    uint64_t sector_offset = 0;
    if (!Multiply(kFilesystemSector, kSectorSize, &sector_offset) ||
        !Add(candidate, sector_offset, &header_offset) ||
        !RangeWithin(header_offset, kMagic.size() + 8, image.GetSize())) {
      continue;
    }

    std::array<uint8_t, kMagic.size() + 8> header{};
    const ReadStatus read = ReadExact(image, header_offset, header);
    if (read != ReadStatus::kOk) {
      return MapRead(read);
    }
    if (std::string_view(reinterpret_cast<const char*>(header.data()), kMagic.size()) != kMagic) {
      continue;
    }

    const uint64_t root_sector = LoadLe32(header.data() + 20);
    const uint64_t root_size = LoadLe32(header.data() + 24);
    uint64_t root_sector_offset = 0;
    uint64_t root_offset = 0;
    if (root_size < 14 || root_size > kMaxDirectoryBytes ||
        !Multiply(root_sector, kSectorSize, &root_sector_offset) ||
        !Add(candidate, root_sector_offset, &root_offset) ||
        !RangeWithin(root_offset, root_size, image.GetSize())) {
      return XdvdfsStatus::kInvalidMetadata;
    }

    image_ = &image;
    game_offset_ = candidate;
    root_sector_ = root_sector;
    root_ = {root_offset, root_size};
    RawEntry first_entry;
    const XdvdfsStatus first_status = ReadDirectoryEntry(root_, 0, &first_entry);
    if (first_status != XdvdfsStatus::kOk) {
      image_ = nullptr;
      root_ = {};
      return first_status;
    }
    return XdvdfsStatus::kOk;
  }
  return XdvdfsStatus::kNotXdvdfs;
}

XdvdfsStatus XdvdfsReader::ReadDirectoryEntry(const Directory& directory, uint16_t ordinal,
                                               RawEntry* entry) const {
  if (!image_ || !entry) {
    return XdvdfsStatus::kInvalidMetadata;
  }
  uint64_t ordinal_offset = 0;
  uint64_t entry_offset = 0;
  if (!Multiply(ordinal, 4, &ordinal_offset) || !Add(directory.offset, ordinal_offset, &entry_offset) ||
      !RangeWithin(ordinal_offset, 14, directory.size) ||
      !RangeWithin(entry_offset, 14, image_->GetSize())) {
    return XdvdfsStatus::kInvalidMetadata;
  }

  std::array<uint8_t, 14> header{};
  const ReadStatus read = ReadExact(*image_, entry_offset, header);
  if (read != ReadStatus::kOk) {
    return MapRead(read);
  }
  const uint8_t name_size = header[13];
  uint64_t full_size = 0;
  if (!Add(14, name_size, &full_size) || !RangeWithin(ordinal_offset, full_size, directory.size) ||
      !RangeWithin(entry_offset, full_size, image_->GetSize())) {
    return XdvdfsStatus::kInvalidMetadata;
  }

  std::string name(name_size, '\0');
  if (name_size) {
    const ReadStatus name_read = ReadExact(
        *image_, entry_offset + 14,
        std::span<uint8_t>(reinterpret_cast<uint8_t*>(name.data()), name.size()));
    if (name_read != ReadStatus::kOk) {
      return MapRead(name_read);
    }
  }
  if (!IsSafeComponent(name)) {
    return XdvdfsStatus::kInvalidMetadata;
  }

  entry->left = LoadLe16(header.data());
  entry->right = LoadLe16(header.data() + 2);
  entry->sector = LoadLe32(header.data() + 4);
  entry->length = LoadLe32(header.data() + 8);
  entry->is_directory = (header[12] & 0x10) != 0;
  entry->name = std::move(name);
  return XdvdfsStatus::kOk;
}

XdvdfsStatus XdvdfsReader::EntryToDirectory(const RawEntry& entry, Directory* directory) const {
  if (!entry.is_directory || !directory) {
    return XdvdfsStatus::kNotDirectory;
  }
  if (entry.length == 0) {
    *directory = {};
    return XdvdfsStatus::kOk;
  }
  uint64_t sector_offset = 0;
  uint64_t offset = 0;
  if (entry.length > kMaxDirectoryBytes || !Multiply(entry.sector, kSectorSize, &sector_offset) ||
      !Add(game_offset_, sector_offset, &offset) ||
      !RangeWithin(offset, entry.length, image_->GetSize())) {
    return XdvdfsStatus::kInvalidMetadata;
  }
  *directory = {offset, entry.length};
  return XdvdfsStatus::kOk;
}

XdvdfsStatus XdvdfsReader::EntryToFile(const RawEntry& entry, XdvdfsEntry* file) const {
  if (entry.is_directory || !file) {
    return XdvdfsStatus::kNotFile;
  }
  uint64_t sector_offset = 0;
  uint64_t offset = 0;
  if (!Multiply(entry.sector, kSectorSize, &sector_offset) || !Add(game_offset_, sector_offset, &offset) ||
      !RangeWithin(offset, entry.length, image_->GetSize())) {
    return XdvdfsStatus::kInvalidMetadata;
  }
  *file = {entry.name, false, offset, entry.length};
  return XdvdfsStatus::kOk;
}

XdvdfsStatus XdvdfsReader::ListDirectory(const Directory& directory,
                                          std::vector<XdvdfsEntry>* entries) const {
  if (!entries) {
    return XdvdfsStatus::kInvalidMetadata;
  }
  entries->clear();
  if (directory.size == 0) {
    return XdvdfsStatus::kOk;
  }

  std::vector<uint16_t> pending = {0};
  std::unordered_set<uint16_t> visited;
  visited.reserve(128);
  while (!pending.empty()) {
    const uint16_t ordinal = pending.back();
    pending.pop_back();
    if (!visited.insert(ordinal).second) {
      return XdvdfsStatus::kInvalidMetadata;
    }
    if (visited.size() > kMaxDirectoryEntries) {
      return XdvdfsStatus::kLimitExceeded;
    }

    RawEntry raw;
    XdvdfsStatus status = ReadDirectoryEntry(directory, ordinal, &raw);
    if (status != XdvdfsStatus::kOk) {
      return status;
    }
    XdvdfsEntry entry{raw.name, raw.is_directory, 0, raw.length};
    if (!raw.is_directory) {
      status = EntryToFile(raw, &entry);
      if (status != XdvdfsStatus::kOk) {
        return status;
      }
    } else if (raw.length != 0) {
      Directory child;
      status = EntryToDirectory(raw, &child);
      if (status != XdvdfsStatus::kOk) {
        return status;
      }
      entry.offset = child.offset;
    }
    entries->push_back(std::move(entry));
    if (raw.left) pending.push_back(raw.left);
    if (raw.right) pending.push_back(raw.right);
  }
  return XdvdfsStatus::kOk;
}

XdvdfsStatus XdvdfsReader::FindChild(const Directory& directory, std::string_view name,
                                     RawEntry* entry) const {
  if (!entry) return XdvdfsStatus::kInvalidMetadata;
  std::vector<uint16_t> pending = {0};
  std::unordered_set<uint16_t> visited;
  while (!pending.empty()) {
    const uint16_t ordinal = pending.back();
    pending.pop_back();
    if (!visited.insert(ordinal).second || visited.size() > kMaxDirectoryEntries) {
      return XdvdfsStatus::kInvalidMetadata;
    }
    RawEntry raw;
    const XdvdfsStatus status = ReadDirectoryEntry(directory, ordinal, &raw);
    if (status != XdvdfsStatus::kOk) return status;
    if (raw.name == name) {
      *entry = std::move(raw);
      return XdvdfsStatus::kOk;
    }
    if (raw.left) pending.push_back(raw.left);
    if (raw.right) pending.push_back(raw.right);
  }
  return XdvdfsStatus::kPathNotFound;
}

XdvdfsStatus XdvdfsReader::ResolveDirectory(std::string_view path, Directory* directory) const {
  if (!image_ || !directory) return XdvdfsStatus::kInvalidMetadata;
  Directory current = root_;
  size_t depth = 0;
  while (!path.empty()) {
    const size_t separator = path.find_first_of("\\/");
    const std::string_view component = path.substr(0, separator);
    if (component.empty() || component == "." || component == ".." || ++depth > kMaxPathDepth) {
      return XdvdfsStatus::kInvalidMetadata;
    }
    RawEntry child;
    const XdvdfsStatus status = FindChild(current, component, &child);
    if (status != XdvdfsStatus::kOk) return status;
    const XdvdfsStatus directory_status = EntryToDirectory(child, &current);
    if (directory_status != XdvdfsStatus::kOk) return directory_status;
    if (separator == std::string_view::npos) break;
    path.remove_prefix(separator + 1);
  }
  *directory = current;
  return XdvdfsStatus::kOk;
}

XdvdfsStatus XdvdfsReader::ListDirectory(std::string_view path,
                                          std::vector<XdvdfsEntry>* entries) const {
  Directory directory;
  const XdvdfsStatus status = ResolveDirectory(path, &directory);
  return status == XdvdfsStatus::kOk ? ListDirectory(directory, entries) : status;
}

XdvdfsStatus XdvdfsReader::FindFile(std::string_view path, XdvdfsEntry* entry) const {
  const size_t separator = path.find_last_of("\\/");
  const std::string_view directory_path = separator == std::string_view::npos ? "" : path.substr(0, separator);
  const std::string_view name = separator == std::string_view::npos ? path : path.substr(separator + 1);
  if (name.empty() || name == "." || name == "..") return XdvdfsStatus::kPathNotFound;

  Directory directory;
  const XdvdfsStatus status = ResolveDirectory(directory_path, &directory);
  if (status != XdvdfsStatus::kOk) return status;
  RawEntry raw;
  const XdvdfsStatus child_status = FindChild(directory, name, &raw);
  return child_status == XdvdfsStatus::kOk ? EntryToFile(raw, entry) : child_status;
}

ReadResult XdvdfsReader::ReadFileRange(const XdvdfsEntry& file, uint64_t file_offset,
                                       std::span<uint8_t> buffer) const {
  if (!image_ || file.is_directory || file_offset > file.size) {
    return {ReadStatus::kInvalidRange, 0};
  }
  if (buffer.empty()) return {ReadStatus::kOk, 0};
  uint64_t absolute = 0;
  if (!Add(file.offset, file_offset, &absolute)) return {ReadStatus::kOverflow, 0};
  const uint64_t available = file.size - file_offset;
  const size_t readable = static_cast<size_t>(std::min<uint64_t>(available, buffer.size()));
  const ReadResult result = image_->ReadAt(absolute, buffer.first(readable));
  if (!result.ok()) return result;
  return {readable == buffer.size() ? ReadStatus::kOk : ReadStatus::kEof, result.bytes_read};
}

}  // namespace thps::image
