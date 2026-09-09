#include "android_fd_game_image_reader.h"

#include <algorithm>
#include <cerrno>
#include <limits>

#include <sys/types.h>
#include <unistd.h>

namespace thps::image {

namespace {

ReadStatus SeekStatus() {
  return errno == ESPIPE ? ReadStatus::kNotSeekable : ReadStatus::kIoError;
}

bool AddOverflows(uint64_t left, uint64_t right) {
  return right > std::numeric_limits<uint64_t>::max() - left;
}

}  // namespace

ReadStatus AndroidFdGameImageReader::Adopt(
    int owned_fd, std::unique_ptr<AndroidFdGameImageReader>* out_reader) {
  if (!out_reader || owned_fd < 0) {
    if (owned_fd >= 0) close(owned_fd);
    return ReadStatus::kInvalidRange;
  }
  out_reader->reset();
  errno = 0;
  const off_t end = lseek(owned_fd, 0, SEEK_END);
  if (end < 0) {
    const ReadStatus status = SeekStatus();
    close(owned_fd);
    return status;
  }
  if (lseek(owned_fd, 0, SEEK_SET) < 0) {
    const ReadStatus status = SeekStatus();
    close(owned_fd);
    return status;
  }
  *out_reader = std::unique_ptr<AndroidFdGameImageReader>(
      new AndroidFdGameImageReader(owned_fd, static_cast<uint64_t>(end)));
  return ReadStatus::kOk;
}

AndroidFdGameImageReader::~AndroidFdGameImageReader() {
  if (fd_ >= 0) close(fd_);
}

ReadResult AndroidFdGameImageReader::ReadAt(uint64_t offset, std::span<uint8_t> buffer) {
  if (buffer.empty()) {
    return {offset <= size_ ? ReadStatus::kOk : ReadStatus::kInvalidRange, 0};
  }
  if (AddOverflows(offset, static_cast<uint64_t>(buffer.size()))) {
    return {ReadStatus::kOverflow, 0};
  }
  if (offset > size_ || offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
    return {ReadStatus::kInvalidRange, 0};
  }

  const size_t wanted = static_cast<size_t>(
      std::min<uint64_t>(size_ - offset, static_cast<uint64_t>(buffer.size())));
  size_t done = 0;
  while (done < wanted) {
    const uint64_t current = offset + done;
    if (current > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
      return {ReadStatus::kInvalidRange, done};
    }
    const ssize_t read = pread(fd_, buffer.data() + done, wanted - done,
                               static_cast<off_t>(current));
    if (read > 0) {
      done += static_cast<size_t>(read);
      continue;
    }
    if (read == 0) return {ReadStatus::kEof, done};
    if (errno != EINTR) return {ReadStatus::kIoError, done};
  }
  return {wanted == buffer.size() ? ReadStatus::kOk : ReadStatus::kEof, done};
}

}  // namespace thps::image
