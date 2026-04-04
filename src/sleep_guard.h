// sleep_guard.h -- RAII guard to prevent idle sleep during recording.

#pragma once

#include "util.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

class SleepGuard {
public:
  SleepGuard()
  {
    IOReturn ret = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
        CFSTR("Audio recording in progress"), &id_);
    if (ret != kIOReturnSuccess)
      printErr("Warning: could not prevent idle sleep.\n");
  }
  ~SleepGuard()
  {
    if (id_)
      IOPMAssertionRelease(id_);
  }

  SleepGuard(const SleepGuard&) = delete;
  SleepGuard& operator=(const SleepGuard&) = delete;

private:
  IOPMAssertionID id_ = 0;
};
