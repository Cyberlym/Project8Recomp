#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <cerrno>
#include <array>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unistd.h>

#include "android_fd_game_image_reader.h"
#include "android_game_install.h"
#include "common/supported_dumps.h"
#include "common/xex_identity.h"
#include "common/xdvdfs_reader.h"

namespace {

constexpr char kLogTag[] = "Project8Image";
constexpr size_t kExportChunkSize = 64 * 1024;

class ScopedFd {
 public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() { if (fd_ >= 0) close(fd_); }
  bool valid() const { return fd_ >= 0; }
  int Release() { const int fd = fd_; fd_ = -1; return fd; }

 private:
  int fd_;
};

const char* ReadStatusName(thps::image::ReadStatus status) {
  using thps::image::ReadStatus;
  switch (status) {
    case ReadStatus::kOk: return "ok";
    case ReadStatus::kEof: return "eof";
    case ReadStatus::kInvalidRange: return "invalid_range";
    case ReadStatus::kOverflow: return "overflow";
    case ReadStatus::kIoError: return "io_error";
    case ReadStatus::kNotSeekable: return "not_seekable";
  }
  return "unknown";
}

const char* XdvdfsStatusName(thps::image::XdvdfsStatus status) {
  using thps::image::XdvdfsStatus;
  switch (status) {
    case XdvdfsStatus::kOk: return "ok";
    case XdvdfsStatus::kNotXdvdfs: return "not_xdvdfs";
    case XdvdfsStatus::kReadError: return "read_error";
    case XdvdfsStatus::kTruncated: return "truncated";
    case XdvdfsStatus::kInvalidMetadata: return "invalid_metadata";
    case XdvdfsStatus::kLimitExceeded: return "limit_exceeded";
    case XdvdfsStatus::kPathNotFound: return "not_found";
    case XdvdfsStatus::kNotDirectory: return "not_directory";
    case XdvdfsStatus::kNotFile: return "not_file";
  }
  return "unknown";
}

jobject MakeResult(JNIEnv* env, jint state, jlong size, jlong root_sector,
                   jboolean default_xex_found, const char* build, const char* reason) {
  jclass result_class = env->FindClass("com/cyberlym/project8probe/GameImageValidation");
  if (!result_class) return nullptr;
  jmethodID constructor = env->GetMethodID(result_class, "<init>", "(IJJZLjava/lang/String;Ljava/lang/String;)V");
  if (!constructor) return nullptr;
  jstring message = env->NewStringUTF(reason);
  jstring build_string = env->NewStringUTF(build);
  if (!message || !build_string) return nullptr;
  jobject result = env->NewObject(result_class, constructor, state, size, root_sector,
                                  default_xex_found, build_string, message);
  env->DeleteLocalRef(message);
  env->DeleteLocalRef(build_string);
  return result;
}

jobject MakeExportResult(JNIEnv* env, jboolean success, const char* reason) {
  jclass result_class = env->FindClass("com/cyberlym/project8probe/XexExportResult");
  if (!result_class) return nullptr;
  jmethodID constructor = env->GetMethodID(result_class, "<init>", "(ZLjava/lang/String;)V");
  if (!constructor) return nullptr;
  jstring message = env->NewStringUTF(reason);
  if (!message) return nullptr;
  jobject result = env->NewObject(result_class, constructor, success, message);
  env->DeleteLocalRef(message);
  return result;
}

jobject MakeInstallResult(JNIEnv* env, const thps::android::GameInstallResult& result) {
  jclass result_class = env->FindClass("com/cyberlym/project8probe/GameInstallResult");
  if (!result_class) return nullptr;
  jmethodID constructor = env->GetMethodID(result_class, "<init>", "(ZJJLjava/lang/String;)V");
  if (!constructor) return nullptr;
  jstring message = env->NewStringUTF(result.message.c_str());
  if (!message) return nullptr;
  jobject value = env->NewObject(result_class, constructor, result.success,
                                 static_cast<jlong>(result.copied_bytes),
                                 static_cast<jlong>(result.total_bytes), message);
  env->DeleteLocalRef(message);
  return value;
}

bool WriteAll(int fd, const uint8_t* bytes, size_t size) {
  while (size > 0) {
    const ssize_t written = write(fd, bytes, size);
    if (written > 0) {
      bytes += written;
      size -= static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    return false;
  }
  return true;
}

bool IsSupported(const thps::image::XexIdentity& identity, std::string* expected_sha256) {
  const std::string sha256 = thps::image::Sha256Hex(identity.sha256);
  for (const auto& dump : thps::identify::kSupportedDumps) {
    if (sha256 == dump.sha256 && identity.size == dump.xex_size) {
      *expected_sha256 = sha256;
      return true;
    }
  }
  return false;
}

}  // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_cyberlym_project8probe_LauncherActivity_nativeValidateGameImage(
    JNIEnv* env, jobject, jint detached_fd) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_IMAGE_OPEN stage=fd-reader reason=begin");
  std::unique_ptr<thps::image::AndroidFdGameImageReader> image;
  const thps::image::ReadStatus open_status =
      thps::image::AndroidFdGameImageReader::Adopt(detached_fd, &image);
  if (open_status != thps::image::ReadStatus::kOk) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag,
                        "P8_IMAGE_OPEN stage=fd-reader reason=%s", ReadStatusName(open_status));
    return MakeResult(env, 1, 0, 0, JNI_FALSE, "",
                      open_status == thps::image::ReadStatus::kNotSeekable
                          ? "This storage provider does not support random access."
                          : "The selected file could not be opened.");
  }

  const uint64_t size = image->GetSize();
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_IMAGE_SIZE bytes=%llu",
                      static_cast<unsigned long long>(size));
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_IMAGE_SEEKABLE value=true");

  thps::image::XdvdfsReader xdvdfs;
  const thps::image::XdvdfsStatus detect_status = xdvdfs.Open(*image);
  __android_log_print(detect_status == thps::image::XdvdfsStatus::kOk ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                      kLogTag, "P8_XDVDFS_DETECT stage=open reason=%s",
                      XdvdfsStatusName(detect_status));
  if (detect_status != thps::image::XdvdfsStatus::kOk) {
    return MakeResult(env, 1, static_cast<jlong>(size), 0, JNI_FALSE, "",
                      "The selected file is not a supported game image.");
  }

  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XDVDFS_ROOT sector=%llu",
                      static_cast<unsigned long long>(xdvdfs.root_sector()));
  thps::image::XdvdfsEntry default_xex;
  const thps::image::XdvdfsStatus default_status =
      xdvdfs.FindFile("default.xex", &default_xex);
  __android_log_print(default_status == thps::image::XdvdfsStatus::kOk ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                      kLogTag, "P8_DEFAULT_XEX stage=find reason=%s",
                      XdvdfsStatusName(default_status));
  if (default_status != thps::image::XdvdfsStatus::kOk) {
    return MakeResult(env, 2, static_cast<jlong>(size),
                      static_cast<jlong>(xdvdfs.root_sector()), JNI_FALSE, "",
                      "Game image detected, but default.xex was not found.");
  }
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_FOUND value=true");
  thps::image::XexIdentity identity;
  const thps::image::XexIdentityStatus identity_status =
      thps::image::IdentifyXex(*image, xdvdfs, default_xex, &identity);
  if (identity_status != thps::image::XexIdentityStatus::kOk) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_COMPATIBILITY value=UNKNOWN_UNVERIFIED");
    return MakeResult(env, 4, static_cast<jlong>(size), static_cast<jlong>(xdvdfs.root_sector()),
                      JNI_TRUE, "Unverified", "Game image detected. Executable could not be identified.");
  }
  const std::string sha256 = thps::image::Sha256Hex(identity.sha256);
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_SIZE bytes=%llu", static_cast<unsigned long long>(identity.size));
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_SHA256 value=%s", sha256.c_str());
  if (identity.has_execution_info) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_TITLE_ID value=0x%08X", identity.title_id);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_MEDIA_ID value=0x%08X", identity.media_id);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_VERSION value=0x%08X", identity.version);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_BASE_VERSION value=0x%08X", identity.base_version);
  }
  for (const auto& dump : thps::identify::kSupportedDumps) {
    if (sha256 == dump.sha256) {
      __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_COMPATIBILITY value=SUPPORTED_MATCH");
      return MakeResult(env, 0, static_cast<jlong>(size), static_cast<jlong>(xdvdfs.root_sector()), JNI_TRUE, "Supported", "Game image detected. Executable identified.");
    }
  }
  bool known_title = false;
  if (identity.has_execution_info) {
    char observed[11];
    std::snprintf(observed, sizeof(observed), "0x%08X", identity.title_id);
    for (const auto title : thps::identify::kSupportedTitleIds) known_title |= title == observed;
  }
  const char* compatibility = known_title ? "KNOWN_DIFFERENT_BUILD" : "UNKNOWN_UNVERIFIED";
  __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_COMPATIBILITY value=%s", compatibility);
  return MakeResult(env, known_title ? 3 : 4, static_cast<jlong>(size), static_cast<jlong>(xdvdfs.root_sector()), JNI_TRUE, "Unverified", "Game image detected. Executable identified.");
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cyberlym_project8probe_LauncherActivity_nativeInstallGameImage(
    JNIEnv* env, jobject activity, jint detached_fd, jstring staging_path) {
  ScopedFd fd_guard(detached_fd);
  if (!staging_path) return MakeInstallResult(env, {false, 0, 0, "The installation folder is unavailable."});
  const char* chars = env->GetStringUTFChars(staging_path, nullptr);
  if (!chars) return MakeInstallResult(env, {false, 0, 0, "The installation folder is unavailable."});
  const std::filesystem::path staging(chars);
  env->ReleaseStringUTFChars(staging_path, chars);
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID progress_method = activity_class
      ? env->GetMethodID(activity_class, "onNativeInstallProgress", "(JJLjava/lang/String;)V")
      : nullptr;
  auto progress = [env, activity, progress_method](uint64_t copied, uint64_t total,
                                                     const std::string& file) {
    if (!progress_method) return;
    jstring name = env->NewStringUTF(file.c_str());
    if (!name) return;
    env->CallVoidMethod(activity, progress_method, static_cast<jlong>(copied),
                        static_cast<jlong>(total), name);
    env->DeleteLocalRef(name);
    if (env->ExceptionCheck()) env->ExceptionClear();
  };
  // Adopt transfers close ownership to AndroidFdGameImageReader. Release this
  // JNI guard only after the string and callback setup above have succeeded.
  const auto result = thps::android::InstallGameFromFd(fd_guard.Release(), staging,
                                                        std::move(progress));
  return MakeInstallResult(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cyberlym_project8probe_LauncherActivity_nativeValidateInstalledGame(
    JNIEnv* env, jobject, jstring game_path) {
  if (!game_path) return MakeInstallResult(env, {false, 0, 0, "The installed game files are incomplete."});
  const char* chars = env->GetStringUTFChars(game_path, nullptr);
  if (!chars) return MakeInstallResult(env, {false, 0, 0, "The installed game files are incomplete."});
  const auto result = thps::android::ValidateInstalledGame(std::filesystem::path(chars));
  env->ReleaseStringUTFChars(game_path, chars);
  return MakeInstallResult(env, result);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_cyberlym_project8probe_LauncherActivity_nativeExportDefaultXex(
    JNIEnv* env, jobject, jint input_fd, jint output_fd) {
  ScopedFd destination(output_fd);
  if (!destination.valid()) {
    return MakeExportResult(env, JNI_FALSE, "The destination file could not be opened.");
  }
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_EXPORT_START value=true");
  std::unique_ptr<thps::image::AndroidFdGameImageReader> image;
  if (thps::image::AndroidFdGameImageReader::Adopt(input_fd, &image) !=
      thps::image::ReadStatus::kOk) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_EXPORT_RESULT value=failed");
    return MakeExportResult(env, JNI_FALSE, "The selected game image could not be opened.");
  }
  thps::image::XdvdfsReader xdvdfs;
  thps::image::XdvdfsEntry source;
  if (xdvdfs.Open(*image) != thps::image::XdvdfsStatus::kOk ||
      xdvdfs.FindFile("default.xex", &source) != thps::image::XdvdfsStatus::kOk) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_EXPORT_RESULT value=failed");
    return MakeExportResult(env, JNI_FALSE, "The selected game image is no longer available.");
  }
  thps::image::XexIdentity identity;
  if (thps::image::IdentifyXex(*image, xdvdfs, source, &identity) !=
      thps::image::XexIdentityStatus::kOk) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_EXPORT_RESULT value=failed");
    return MakeExportResult(env, JNI_FALSE, "The executable could not be verified.");
  }
  std::string expected_sha256;
  if (!IsSupported(identity, &expected_sha256)) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_EXPORT_RESULT value=refused");
    return MakeExportResult(env, JNI_FALSE, "Only the supported game build can be exported.");
  }

  std::array<uint8_t, kExportChunkSize> chunk{};
  thps::image::Sha256Hasher hasher;
  uint64_t copied = 0;
  while (copied < source.size) {
    const size_t count = static_cast<size_t>(std::min<uint64_t>(chunk.size(), source.size - copied));
    const thps::image::ReadResult read =
        xdvdfs.ReadFileRange(source, copied, std::span(chunk.data(), count));
    if (!read.ok() || read.bytes_read != count || !WriteAll(output_fd, chunk.data(), count)) {
      __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_EXPORT_RESULT value=failed");
      return MakeExportResult(env, JNI_FALSE, "The executable could not be exported.");
    }
    hasher.Update(chunk.data(), count);
    copied += count;
  }
  const std::string exported_sha256 = thps::image::Sha256Hex(hasher.Final());
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_EXPORT_SIZE bytes=%llu",
                      static_cast<unsigned long long>(copied));
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_EXPORT_SHA256 value=%s",
                      exported_sha256.c_str());
  if (copied != identity.size || exported_sha256 != expected_sha256) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "P8_XEX_EXPORT_RESULT value=failed");
    return MakeExportResult(env, JNI_FALSE, "The exported executable did not verify.");
  }
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "P8_XEX_EXPORT_RESULT value=success");
  return MakeExportResult(env, JNI_TRUE, "Executable exported and verified.");
}
