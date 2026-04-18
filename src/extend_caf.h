// extend_caf.h -- Extend mode: pad CAF recording via sparse hole.

#pragma once

#include "args.h"

#include <expected>
#include <string>

struct ExtendResult {
  std::string resolved_input;
  std::string resolved_output;
  int pad_to_minutes;
};

std::expected<ExtendResult, std::string> extendCafFile(
    const ExtendArgs& a_args);
