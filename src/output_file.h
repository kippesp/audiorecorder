// output_file.h -- CAF output file creation.

#pragma once

#include "recording_context.h"

#include <expected>
#include <string>

// Output file -- returns guard on success, OSStatus on failure.
std::expected<AudioFileGuard, OSStatus> createOutputFile(
    const std::string& a_path, Float64 a_sample_rate,
    uint32_t a_output_channels);
