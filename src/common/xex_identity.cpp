#include "common/xex_identity.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace thps::image {
namespace {
constexpr uint32_t kXex2Magic = 0x58455832;
constexpr uint32_t kExecutionInfoKey = 0x00040006;
constexpr size_t kFixedHeaderSize = 0x18;
constexpr size_t kOptionalHeaderSize = 8;
constexpr size_t kExecutionInfoSize = 0x18;
constexpr size_t kMaxHeaderSize = 1024 * 1024;
constexpr size_t kChunkSize = 64 * 1024;

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint32_t RotR(uint32_t value, uint32_t bits) { return (value >> bits) | (value << (32 - bits)); }
}  // namespace

Sha256Hasher::Sha256Hasher()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256Hasher::Update(const uint8_t* data, size_t size) {
    bit_count_ += uint64_t(size) * 8;
    while (size) {
      const size_t count = std::min(size, block_.size() - block_size_);
      std::memcpy(block_.data() + block_size_, data, count);
      block_size_ += count; data += count; size -= count;
      if (block_size_ == block_.size()) { Transform(); block_size_ = 0; }
    }
}

std::array<uint8_t, 32> Sha256Hasher::Final() {
    const uint64_t bits = bit_count_;
    const uint8_t one = 0x80;
    Update(&one, 1);
    const uint8_t zero = 0;
    while (block_size_ != 56) Update(&zero, 1);
    std::array<uint8_t, 8> length{};
    for (size_t i = 0; i < length.size(); ++i) length[7 - i] = uint8_t(bits >> (i * 8));
    Update(length.data(), length.size());
    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < state_.size(); ++i) {
      out[i * 4] = uint8_t(state_[i] >> 24); out[i * 4 + 1] = uint8_t(state_[i] >> 16);
      out[i * 4 + 2] = uint8_t(state_[i] >> 8); out[i * 4 + 3] = uint8_t(state_[i]);
    }
    return out;
}

void Sha256Hasher::Transform() {
    static constexpr uint32_t k[] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i) w[i] = Be32(block_.data() + i * 4);
    for (size_t i = 16; i < 64; ++i) w[i] = (RotR(w[i-15],7)^RotR(w[i-15],18)^(w[i-15]>>3)) + w[i-16] + (RotR(w[i-2],17)^RotR(w[i-2],19)^(w[i-2]>>10)) + w[i-7];
    uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for (size_t i = 0; i < 64; ++i) { const uint32_t s1=RotR(e,6)^RotR(e,11)^RotR(e,25); const uint32_t ch=(e&f)^((~e)&g); const uint32_t t1=h+s1+ch+k[i]+w[i]; const uint32_t s0=RotR(a,2)^RotR(a,13)^RotR(a,22); const uint32_t maj=(a&b)^(a&c)^(b&c); h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+s0+maj; }
    state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
}

namespace {
bool ReadFile(const XdvdfsReader& xdvdfs, const XdvdfsEntry& file, uint64_t offset,
              std::span<uint8_t> out) {
  const ReadResult result = xdvdfs.ReadFileRange(file, offset, out);
  return result.status == ReadStatus::kOk && result.bytes_read == out.size();
}
}  // namespace

XexIdentityStatus IdentifyXex(GameImageReader&, const XdvdfsReader& xdvdfs,
                              const XdvdfsEntry& file, XexIdentity* identity) {
  if (!identity || file.is_directory) return XexIdentityStatus::kInvalidXex;
  identity->size = file.size;
  Sha256Hasher hasher;
  std::vector<uint8_t> chunk(std::min<uint64_t>(kChunkSize, file.size));
  for (uint64_t offset = 0; offset < file.size;) {
    const size_t count = static_cast<size_t>(std::min<uint64_t>(chunk.size(), file.size - offset));
    if (!ReadFile(xdvdfs, file, offset, std::span(chunk.data(), count))) return XexIdentityStatus::kReadError;
    hasher.Update(chunk.data(), count); offset += count;
  }
  identity->sha256 = hasher.Final();
  if (file.size < kFixedHeaderSize) return XexIdentityStatus::kInvalidXex;
  std::array<uint8_t, kFixedHeaderSize> fixed{};
  if (!ReadFile(xdvdfs, file, 0, fixed)) return XexIdentityStatus::kReadError;
  const uint32_t header_size = Be32(fixed.data() + 8), header_count = Be32(fixed.data() + 20);
  if (Be32(fixed.data()) != kXex2Magic || header_size < kFixedHeaderSize || header_size > file.size || header_size > kMaxHeaderSize || header_count > (header_size - kFixedHeaderSize) / kOptionalHeaderSize) return XexIdentityStatus::kInvalidXex;
  std::vector<uint8_t> headers(header_size - kFixedHeaderSize);
  if (!headers.empty() && !ReadFile(xdvdfs, file, kFixedHeaderSize, headers)) return XexIdentityStatus::kReadError;
  for (uint32_t i = 0; i < header_count; ++i) {
    const uint8_t* entry = headers.data() + i * kOptionalHeaderSize;
    if (Be32(entry) != kExecutionInfoKey) continue;
    const uint32_t offset = Be32(entry + 4);
    if (offset > file.size || kExecutionInfoSize > file.size - offset) return XexIdentityStatus::kInvalidXex;
    std::array<uint8_t, kExecutionInfoSize> info{};
    if (!ReadFile(xdvdfs, file, offset, info)) return XexIdentityStatus::kReadError;
    identity->media_id = Be32(info.data()); identity->version = Be32(info.data()+4);
    identity->base_version = Be32(info.data()+8); identity->title_id = Be32(info.data()+12);
    identity->has_execution_info = true; break;
  }
  return XexIdentityStatus::kOk;
}

std::string Sha256Hex(const std::array<uint8_t, 32>& digest) {
  static constexpr char kHex[] = "0123456789abcdef"; std::string out(64, '0');
  for (size_t i = 0; i < digest.size(); ++i) { out[i*2]=kHex[digest[i]>>4]; out[i*2+1]=kHex[digest[i]&15]; }
  return out;
}
}  // namespace thps::image
