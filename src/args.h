// args.h -- CLI argument parsing and usage display.

#pragma once

#include <string>

struct Args {
  std::string output_path_;
  std::string device_selector_;
  bool list_devices_ = false;
  bool mono_ = false;
  bool monitor_ = false;
  bool test_ = false;
  int max_duration_min_ = 0;
  bool quiet_ = false;
  bool verbose_ = false;
};

Args parseArgs(int a_argc, char* a_argv[]);
