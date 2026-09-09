#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/tests/memory_game_image_reader.h"
#include "common/xdvdfs_reader.h"

namespace {

using thps::image::GameImageReader;
using thps::image::ReadResult;
using thps::image::ReadStatus;
using thps::image::XdvdfsEntry;
using thps::image::XdvdfsReader;
using thps::image::XdvdfsStatus;
using thps::image::test::MemoryGameImageReader;

constexpr uint64_t kSectorSize = 2048;
constexpr uint32_t kHeaderSector = 32;
constexpr uint32_t kRootSector = 33;
constexpr uint32_t kDataSector = 34;
constexpr uint32_t kDefaultSector = 35;
constexpr uint32_t kHelloSector = 36;
constexpr uint32_t kTestSector = 37;
constexpr std::string_view kMagic = "MICROSOFT*XBOX*MEDIA";
constexpr std::string_view kDefault = "SYNTHETIC-NOT-A-REAL-XEX";
constexpr std::string_view kHello = "Project8 Stage6C test";
constexpr std::array<uint8_t, 8> kTestBytes = {0x00, 0x17, 0x42, 0xA5,
                                                 0x5A, 0x99, 0xCC, 0xFF};

[[nodiscard]] size_t Offset(uint64_t sector, uint64_t offset = 0) {
  return static_cast<size_t>(sector * kSectorSize + offset);
}

void WriteLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void WriteEntry(std::vector<uint8_t>& bytes, uint32_t sector, uint16_t ordinal,
                uint16_t left, uint16_t right, uint32_t start_sector,
                uint32_t length, std::string_view name, bool directory) {
  const size_t offset = Offset(sector, static_cast<uint64_t>(ordinal) * 4);
  WriteLe32(bytes, offset, static_cast<uint32_t>(left) | (uint32_t(right) << 16));
  WriteLe32(bytes, offset + 4, start_sector);
  WriteLe32(bytes, offset + 8, length);
  bytes[offset + 12] = directory ? 0x10 : 0x00;
  bytes[offset + 13] = static_cast<uint8_t>(name.size());
  std::copy(name.begin(), name.end(), bytes.begin() + offset + 14);
}

[[nodiscard]] std::vector<uint8_t> MakeFixture() {
  std::vector<uint8_t> bytes(40 * kSectorSize, 0);
  const size_t header = Offset(kHeaderSector);
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin() + header);
  WriteLe32(bytes, header + 20, kRootSector);
  WriteLe32(bytes, header + 24, 128);

  WriteEntry(bytes, kRootSector, 0, 8, 16, kHelloSector,
             static_cast<uint32_t>(kHello.size()), "hello.txt", false);
  WriteEntry(bytes, kRootSector, 8, 0, 0, kDefaultSector,
             static_cast<uint32_t>(kDefault.size()), "default.xex", false);
  WriteEntry(bytes, kRootSector, 16, 0, 0, kDataSector, 64, "data", true);
  WriteEntry(bytes, kDataSector, 0, 0, 0, kTestSector,
             static_cast<uint32_t>(kTestBytes.size()), "test.bin", false);

  std::copy(kDefault.begin(), kDefault.end(), bytes.begin() + Offset(kDefaultSector));
  std::copy(kHello.begin(), kHello.end(), bytes.begin() + Offset(kHelloSector));
  std::copy(kTestBytes.begin(), kTestBytes.end(), bytes.begin() + Offset(kTestSector));
  return bytes;
}

[[nodiscard]] std::vector<uint8_t> MakeDepthImage(size_t depth) {
  std::vector<uint8_t> bytes((kRootSector + depth + 2) * kSectorSize, 0);
  const size_t header = Offset(kHeaderSector);
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin() + header);
  WriteLe32(bytes, header + 20, kRootSector);
  WriteLe32(bytes, header + 24, 32);
  for (size_t index = 0; index < depth; ++index) {
    const uint32_t sector = kRootSector + static_cast<uint32_t>(index);
    WriteEntry(bytes, sector, 0, 0, 0, sector + 1, 32, "d", true);
  }
  return bytes;
}

[[nodiscard]] std::vector<uint8_t> MakeEntryLimitImage() {
  constexpr uint16_t kEntries = 8193;
  constexpr uint16_t kLastOrdinal = (kEntries - 1) * 4;
  const uint32_t directory_size = static_cast<uint32_t>(kLastOrdinal) * 4 + 15;
  std::vector<uint8_t> bytes(Offset(kRootSector) + directory_size + kSectorSize, 0);
  const size_t header = Offset(kHeaderSector);
  std::copy(kMagic.begin(), kMagic.end(), bytes.begin() + header);
  WriteLe32(bytes, header + 20, kRootSector);
  WriteLe32(bytes, header + 24, directory_size);
  for (uint16_t index = 0; index < kEntries; ++index) {
    const uint16_t ordinal = index * 4;
    const uint16_t left = index + 1 < kEntries ? ordinal + 4 : 0;
    WriteEntry(bytes, kRootSector, ordinal, left, 0, kHelloSector, 1, "x", false);
  }
  return bytes;
}

class FaultReader final : public GameImageReader {
 public:
  FaultReader(std::vector<uint8_t> bytes, ReadStatus status)
      : bytes_(std::move(bytes)), status_(status) {}

  uint64_t GetSize() const override { return bytes_.size(); }
  ReadResult ReadAt(uint64_t, std::span<uint8_t>) override { return {status_, 0}; }

 private:
  std::vector<uint8_t> bytes_;
  ReadStatus status_;
};

struct TestContext {
  int failures = 0;

  void Expect(bool condition, std::string_view message) {
    if (!condition) {
      ++failures;
      std::cerr << "FAIL: " << message << '\n';
    }
  }
};

void RunValidTests(TestContext& test) {
  MemoryGameImageReader image(MakeFixture());
  XdvdfsReader reader;
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "valid image opens");
  test.Expect(reader.root_sector() == kRootSector, "root sector is correct");

  std::vector<XdvdfsEntry> root;
  test.Expect(reader.ListDirectory("", &root) == XdvdfsStatus::kOk,
              "root directory lists");
  test.Expect(root.size() == 3, "root contains three entries");

  XdvdfsEntry entry;
  test.Expect(reader.FindFile("default.xex", &entry) == XdvdfsStatus::kOk,
              "default.xex resolves");
  test.Expect(entry.size == kDefault.size(), "default.xex size is correct");
  test.Expect(reader.FindFile("hello.txt", &entry) == XdvdfsStatus::kOk,
              "hello.txt resolves");
  test.Expect(reader.FindFile("data/test.bin", &entry) == XdvdfsStatus::kOk,
              "nested file resolves");
  test.Expect(entry.size == kTestBytes.size(), "nested file size is correct");
  test.Expect(reader.FindFile("missing.bin", &entry) == XdvdfsStatus::kPathNotFound,
              "missing file is rejected");
  test.Expect(reader.FindFile("../hello.txt", &entry) == XdvdfsStatus::kInvalidMetadata,
              "unsafe path is rejected");

  std::vector<XdvdfsEntry> data;
  test.Expect(reader.ListDirectory("data", &data) == XdvdfsStatus::kOk,
              "subdirectory lists");
  test.Expect(data.size() == 1 && data.front().name == "test.bin",
              "subdirectory content is correct");

  std::vector<uint8_t> output(kDefault.size());
  test.Expect(reader.FindFile("default.xex", &entry) == XdvdfsStatus::kOk,
              "default file re-resolves");
  test.Expect(reader.ReadFileRange(entry, 0, output).status == ReadStatus::kOk,
              "full file read succeeds");
  test.Expect(std::equal(output.begin(), output.end(), kDefault.begin(), kDefault.end()),
              "full file bytes match fixture");

  output.assign(3, 0);
  test.Expect(reader.FindFile("data/test.bin", &entry) == XdvdfsStatus::kOk,
              "nested file re-resolves");
  test.Expect(reader.ReadFileRange(entry, 2, output).status == ReadStatus::kOk,
              "partial range read succeeds");
  test.Expect(std::equal(output.begin(), output.end(), kTestBytes.begin() + 2),
              "partial range bytes match fixture");

  output.assign(1, 0);
  test.Expect(reader.FindFile("hello.txt", &entry) == XdvdfsStatus::kOk,
              "hello file re-resolves");
  test.Expect(reader.ReadFileRange(entry, 0, output).status == ReadStatus::kOk &&
                  output.front() == static_cast<uint8_t>(kHello.front()),
              "first byte read succeeds");
  test.Expect(reader.ReadFileRange(entry, entry.size - 1, output).status == ReadStatus::kOk &&
                  output.front() == static_cast<uint8_t>(kHello.back()),
              "last byte read succeeds");
  test.Expect(reader.ReadFileRange(entry, entry.size, output).status == ReadStatus::kEof,
              "read past file end returns EOF");

  std::array<uint8_t, 1> byte{};
  test.Expect(image.ReadAt(std::numeric_limits<uint64_t>::max(), byte).status ==
                  ReadStatus::kOverflow,
              "reader rejects offset overflow");
}

void RunCorruptionTests(TestContext& test) {
  const auto expect_open = [&test](std::vector<uint8_t> bytes, XdvdfsStatus expected,
                                   std::string_view label) {
    MemoryGameImageReader image(std::move(bytes));
    XdvdfsReader reader;
    test.Expect(reader.Open(image) == expected, label);
  };

  expect_open({}, XdvdfsStatus::kNotXdvdfs, "empty image is rejected");
  expect_open(std::vector<uint8_t>(Offset(kHeaderSector), 0), XdvdfsStatus::kNotXdvdfs,
              "truncated header is rejected");
  auto bytes = MakeFixture();
  bytes[Offset(kHeaderSector)] ^= 0xFF;
  expect_open(std::move(bytes), XdvdfsStatus::kNotXdvdfs, "invalid magic is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kHeaderSector) + 20, std::numeric_limits<uint32_t>::max());
  expect_open(std::move(bytes), XdvdfsStatus::kInvalidMetadata,
              "root sector outside image is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kHeaderSector) + 24, 33 * 1024 * 1024);
  expect_open(std::move(bytes), XdvdfsStatus::kInvalidMetadata,
              "oversized root directory is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kHeaderSector) + 24, 14);
  expect_open(std::move(bytes), XdvdfsStatus::kInvalidMetadata,
              "truncated directory entry is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kRootSector) + 4, std::numeric_limits<uint32_t>::max());
  MemoryGameImageReader image(std::move(bytes));
  XdvdfsReader reader;
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "file range fixture opens");
  XdvdfsEntry entry;
  test.Expect(reader.FindFile("hello.txt", &entry) == XdvdfsStatus::kInvalidMetadata,
              "file beyond EOF is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kRootSector, 16 * 4 + 4), std::numeric_limits<uint32_t>::max());
  image = MemoryGameImageReader(std::move(bytes));
  reader = XdvdfsReader();
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "bad child fixture opens");
  std::vector<XdvdfsEntry> entries;
  test.Expect(reader.ListDirectory("data", &entries) == XdvdfsStatus::kInvalidMetadata,
              "invalid child offset is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kRootSector, 8 * 4), 8);
  image = MemoryGameImageReader(std::move(bytes));
  reader = XdvdfsReader();
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "cycle fixture opens");
  test.Expect(reader.ListDirectory("", &entries) == XdvdfsStatus::kInvalidMetadata,
              "directory entry cycle is rejected");

  bytes = MakeFixture();
  WriteLe32(bytes, Offset(kRootSector, 16 * 4 + 8), 33 * 1024 * 1024);
  image = MemoryGameImageReader(std::move(bytes));
  reader = XdvdfsReader();
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "large allocation fixture opens");
  test.Expect(reader.ListDirectory("", &entries) == XdvdfsStatus::kInvalidMetadata,
              "oversized child directory is rejected");

  image = MemoryGameImageReader(MakeDepthImage(65));
  reader = XdvdfsReader();
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "deep fixture opens");
  std::string deep_path;
  for (size_t index = 0; index < 65; ++index) {
    if (!deep_path.empty()) {
      deep_path += '/';
    }
    deep_path += "d";
  }
  test.Expect(reader.ListDirectory(deep_path, &entries) == XdvdfsStatus::kInvalidMetadata,
              "path depth limit is enforced");

  image = MemoryGameImageReader(MakeEntryLimitImage());
  reader = XdvdfsReader();
  test.Expect(reader.Open(image) == XdvdfsStatus::kOk, "large entry fixture opens");
  test.Expect(reader.ListDirectory("", &entries) == XdvdfsStatus::kLimitExceeded,
              "directory entry count limit is enforced");

  XdvdfsEntry overflow_entry{"fake", false, std::numeric_limits<uint64_t>::max(), 2};
  std::array<uint8_t, 1> output{};
  test.Expect(reader.ReadFileRange(overflow_entry, 1, output).status == ReadStatus::kOverflow,
              "file offset overflow is rejected");

  FaultReader eof(MakeFixture(), ReadStatus::kEof);
  reader = XdvdfsReader();
  test.Expect(reader.Open(eof) == XdvdfsStatus::kTruncated,
              "short read is reported as truncation");
  FaultReader io_failure(MakeFixture(), ReadStatus::kIoError);
  reader = XdvdfsReader();
  test.Expect(reader.Open(io_failure) == XdvdfsStatus::kReadError,
              "I/O failure is propagated");
  FaultReader non_seekable(MakeFixture(), ReadStatus::kNotSeekable);
  reader = XdvdfsReader();
  test.Expect(reader.Open(non_seekable) == XdvdfsStatus::kReadError,
              "non-seekable source is rejected");
}

}  // namespace

int main(int argc, char** argv) {
  TestContext test;
  const std::string_view mode = argc > 1 ? argv[1] : "all";
  if (mode == "valid" || mode == "all") {
    RunValidTests(test);
  }
  if (mode == "corruption" || mode == "all") {
    RunCorruptionTests(test);
  }
  if (mode != "valid" && mode != "corruption" && mode != "all") {
    std::cerr << "Unknown test mode\n";
    return EXIT_FAILURE;
  }
  if (test.failures != 0) {
    std::cerr << test.failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
