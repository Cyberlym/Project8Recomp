#include "common/game_image_reader.h"

#include <algorithm>
#include <limits>

namespace thps::image {

namespace {

bool AddOverflows(uint64_t left, uint64_t right) {
  return right > std::numeric_limits<uint64_t>::max() - left;
}

}  // namespace

ReadStatus ReadExact(GameImageReader& reader, uint64_t offset, std::span<uint8_t> buffer) {
  const ReadResult result = reader.ReadAt(offset, buffer);
  if (result.ok() && result.bytes_read == buffer.size()) {
    return ReadStatus::kOk;
  }
  return result.status == ReadStatus::kOk ? ReadStatus::kEof : result.status;
}

ReadStatus HostGameImageReader::Open(const std::filesystem::path& path,
                                     std::unique_ptr<HostGameImageReader>* out_reader) {
  if (!out_reader) {
    return ReadStatus::kInvalidRange;
  }
  out_reader->reset();

  std::error_code error;
  const uintmax_t host_size = std::filesystem::file_size(path, error);
  if (error || host_size > std::numeric_limits<uint64_t>::max()) {
    return ReadStatus::kIoError;
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return ReadStatus::kIoError;
  }
  *out_reader = std::unique_ptr<HostGameImageReader>(
      new HostGameImageReader(std::move(stream), static_cast<uint64_t>(host_size)));
  return ReadStatus::kOk;
}

ReadResult HostGameImageReader::ReadAt(uint64_t offset, std::span<uint8_t> buffer) {
  if (buffer.empty()) {
    return {offset <= size_ ? ReadStatus::kOk : ReadStatus::kInvalidRange, 0};
  }
  if (AddOverflows(offset, static_cast<uint64_t>(buffer.size()))) {
    return {ReadStatus::kOverflow, 0};
  }
  if (offset > size_) {
    return {ReadStatus::kInvalidRange, 0};
  }
  if (offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return {ReadStatus::kInvalidRange, 0};
  }

  const uint64_t available = size_ - offset;
  const size_t wanted = static_cast<size_t>(std::min<uint64_t>(available, buffer.size()));
  stream_.clear();
  stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!stream_) {
    return {ReadStatus::kNotSeekable, 0};
  }

  size_t done = 0;
  while (done < wanted) {
    const size_t chunk = std::min(wanted - done,
                                  static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));
    stream_.read(reinterpret_cast<char*>(buffer.data() + done), static_cast<std::streamsize>(chunk));
    const std::streamsize read = stream_.gcount();
    if (read > 0) {
      done += static_cast<size_t>(read);
    }
    if (read != static_cast<std::streamsize>(chunk)) {
      return {stream_.eof() ? ReadStatus::kEof : ReadStatus::kIoError, done};
    }
  }

  return {done == buffer.size() ? ReadStatus::kOk : ReadStatus::kEof, done};
}

}  // namespace thps::image
