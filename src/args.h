// args.h -- CLI argument parsing and usage display.

#pragma once

#include <optional>
#include <string>

struct Args {
  std::optional<std::string> output_path;
  std::string device_selector;
  bool list_devices = false;
  bool monitor = false;
  bool test = false;
  std::optional<int> max_duration_min;
  bool quiet = false;
  bool verbose = false;
};

Args parseArgs(int a_argc, char* a_argv[]);
