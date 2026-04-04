// signal_handler.cpp -- Signal registration for graceful shutdown.

#include "signal_handler.h"

#include <csignal>

static std::atomic<bool>* s_stop_flag = nullptr;

static void handler(int)
{
  if (s_stop_flag)
    s_stop_flag->store(true, std::memory_order_relaxed);
}

void installSignalHandler(std::atomic<bool>& a_stop_flag)
{
  s_stop_flag = &a_stop_flag;
  signal(SIGINT, handler);
  signal(SIGTERM, handler);
}
