#pragma once

struct FiberTestResult {
  bool backend = false;
  bool created = false;
  bool entered = false;
  bool yielded = false;
  bool resumed = false;
  bool returned = false;
  bool multiple_switches = false;
};

FiberTestResult RunFiberTest();
