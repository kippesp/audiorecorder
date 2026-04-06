// args.h -- CLI argument parsing and usage display.

#pragma once

#include <optional>
#include <string>

struct Args {
  std::optional<std::string> output_path_;
  std::string device_selector_;
  bool list_devices_ = false;
  bool monitor_ = false;
  bool test_ = false;
  std::optional<int> max_duration_min_;
  bool quiet_ = false;
  bool verbose_ = false;
};

Args parseArgs(int a_argc, char* a_argv[]);
