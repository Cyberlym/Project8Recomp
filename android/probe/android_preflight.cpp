#include "android_preflight.h"

#include <SDL3/SDL_filesystem.h>
#include <android/sharedmem.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

void RecordFailure(AndroidPreflightResult& result, const char* operation) {
  if (result.failure_operation.empty()) {
    result.failure_operation = operation;
    result.failure_errno = errno;
  }
}

}  // namespace

AndroidPreflightResult RunAndroidPreflight() {
  AndroidPreflightResult result;

#if defined(__aarch64__)
  struct utsname system_name {};
  result.aarch64 = uname(&system_name) == 0 &&
                   (std::strcmp(system_name.machine, "aarch64") == 0 ||
                    std::strcmp(system_name.machine, "arm64") == 0);
#endif

  result.page_size = sysconf(_SC_PAGESIZE);
  if (result.page_size <= 0) {
    RecordFailure(result, "sysconf(_SC_PAGESIZE)");
  } else {
    void* mapping = mmap(nullptr, static_cast<size_t>(result.page_size),
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
      RecordFailure(result, "mmap(RW)");
    } else {
      auto* bytes = static_cast<volatile unsigned char*>(mapping);
      bytes[0] = 0x5A;
      bytes[result.page_size - 1] = 0xA5;
      result.mmap_basic = bytes[0] == 0x5A &&
                          bytes[result.page_size - 1] == 0xA5;
      result.mprotect_read =
          mprotect(mapping, static_cast<size_t>(result.page_size), PROT_READ) == 0;
      if (!result.mprotect_read) {
        RecordFailure(result, "mprotect(R)");
      }
      result.mprotect_rx =
          mprotect(mapping, static_cast<size_t>(result.page_size),
                   PROT_READ | PROT_EXEC) == 0;
      if (!result.mprotect_rx) {
        RecordFailure(result, "mprotect(RX)");
      }
      result.mprotect_rw_restore =
          mprotect(mapping, static_cast<size_t>(result.page_size),
                   PROT_READ | PROT_WRITE) == 0;
      if (!result.mprotect_rw_restore) {
        RecordFailure(result, "mprotect(RW restore)");
      }
      munmap(mapping, static_cast<size_t>(result.page_size));
    }

    int shared_fd =
        ASharedMemory_create("project8-stage5-preflight", result.page_size);
    if (shared_fd < 0) {
      RecordFailure(result, "ASharedMemory_create");
    } else {
      void* shared_mapping =
          mmap(nullptr, static_cast<size_t>(result.page_size),
               PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);
      if (shared_mapping == MAP_FAILED) {
        RecordFailure(result, "mmap(ASharedMemory)");
      } else {
        auto* shared_bytes = static_cast<volatile unsigned char*>(shared_mapping);
        shared_bytes[0] = 0xC3;
        result.shared_memory = shared_bytes[0] == 0xC3;
        munmap(shared_mapping, static_cast<size_t>(result.page_size));
      }
      close(shared_fd);
    }
  }

  std::mutex mutex;
  std::condition_variable condition;
  bool worker_done = false;
  int worker_value = 0;
  std::thread worker([&] {
    {
      std::lock_guard<std::mutex> lock(mutex);
      worker_value = 0x5B3;
      worker_done = true;
    }
    condition.notify_one();
  });
  {
    std::unique_lock<std::mutex> lock(mutex);
    result.mutex_condition = condition.wait_for(
        lock, std::chrono::seconds(2), [&] { return worker_done; });
  }
  worker.join();
  result.threads = worker_value == 0x5B3;

  struct timespec first {};
  struct timespec second {};
  result.monotonic_clock = clock_gettime(CLOCK_MONOTONIC, &first) == 0 &&
                           clock_gettime(CLOCK_MONOTONIC, &second) == 0 &&
                           (second.tv_sec > first.tv_sec ||
                            (second.tv_sec == first.tv_sec &&
                             second.tv_nsec >= first.tv_nsec));
  if (!result.monotonic_clock) {
    RecordFailure(result, "clock_gettime(CLOCK_MONOTONIC)");
  }

  char* pref_path = SDL_GetPrefPath("Cyberlym", "Project8VulkanProbe");
  if (pref_path) {
    const std::string test_path = std::string(pref_path) + "stage5-preflight.tmp";
    SDL_free(pref_path);
    FILE* file = std::fopen(test_path.c_str(), "w+b");
    if (file) {
      constexpr char kPayload[] = "Project8 Stage 5";
      char readback[sizeof(kPayload)] = {};
      result.private_filesystem =
          std::fwrite(kPayload, 1, sizeof(kPayload), file) == sizeof(kPayload) &&
          std::fseek(file, 0, SEEK_SET) == 0 &&
          std::fread(readback, 1, sizeof(readback), file) == sizeof(readback) &&
          std::memcmp(kPayload, readback, sizeof(kPayload)) == 0;
      std::fclose(file);
      std::remove(test_path.c_str());
    } else {
      RecordFailure(result, "fopen(app private file)");
    }
  } else {
    RecordFailure(result, "SDL_GetPrefPath");
  }

  return result;
}
