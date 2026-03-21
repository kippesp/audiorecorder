// session.h -- Session lifecycle: setup, run, teardown.

#pragma once

#include "args.h"
#include "device.h"

#include <vector>

int runSession(const Args& a_args, const std::vector<AudioDevice>& a_devices);
