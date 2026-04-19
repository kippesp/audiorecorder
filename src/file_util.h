// file_util.h -- Path resolution and disk space utilities.

#pragma once

#include <cstdint>
#include <expected>
#include <string>

uint64_t getFreeBytes(const std::string& a_path);

std::expected<std::string, std::string> expandHome(const std::string& a_path);
std::string defaultOutputPath();
std::expected<std::string, std::string> resolveOutputPath(
    const std::string& a_user_path);
std::string directoryOf(const std::string& a_path);
std::expected<void, std::string> checkDiskSpace(const std::string& a_dir,
                                                uint64_t a_bytes_per_sec,
                                                int a_max_duration_min,
                                                const char* a_ch_label);
