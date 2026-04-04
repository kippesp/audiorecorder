// signal_handler.h -- Signal registration for graceful shutdown.

#pragma once

#include <atomic>

// Register SIGINT/SIGTERM to set a_stop_flag to true.
// The flag must outlive the signal registration.
void installSignalHandler(std::atomic<bool>& a_stop_flag);
