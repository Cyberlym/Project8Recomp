#pragma once

#include <cstddef>
#include <string>

struct AndroidPreflightResult {
  bool aarch64 = false;
  long page_size = -1;
  bool mmap_basic = false;
  bool mprotect_read = false;
  bool mprotect_rx = false;
  bool mprotect_rw_restore = false;
  bool shared_memory = false;
  bool threads = false;
  bool mutex_condition = false;
  bool monotonic_clock = false;
  bool private_filesystem = false;
  int failure_errno = 0;
  std::string failure_operation;
};

AndroidPreflightResult RunAndroidPreflight();
