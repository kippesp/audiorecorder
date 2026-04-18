// args.h -- CLI argument parsing and usage display.

#pragma once

#include <optional>
#include <string>
#include <variant>

struct RecordingArgs {
  std::optional<std::string> output_path;
  std::optional<std::string> device_selector;
  bool list_devices = false;
  bool monitor = false;
  bool test = false;
  std::optional<int> max_duration_min;
  bool quiet = false;
  bool verbose = false;
  bool operator==(const RecordingArgs&) const = default;
};

struct ExtendArgs {
  std::string extend_caf_file;
  int pad_to_min = 0;
  std::string output_path;
};

using Args = std::variant<RecordingArgs, ExtendArgs>;

Args parseArgs(int a_argc, char* a_argv[]);
