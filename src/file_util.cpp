// file_util.cpp -- Path resolution and disk space utilities.

#include "file_util.h"
#include "util.h"

#include <chrono>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <format>

uint64_t getFreeBytes(const std::string& a_path)
{
  std::error_code ec;
  auto info = std::filesystem::space(a_path, ec);
  if (ec)
    return UINT64_MAX;
  return info.available;
}

static std::expected<std::string, std::string> expandHome(
    const std::string& a_path)
{
  if (a_path.starts_with("~/"))
  {
    const char* home = getenv("HOME");
    if (!home)
      return std::unexpected(
          std::string("Error: HOME environment variable is not set.\n"));
    return std::string(home) + a_path.substr(1);
  }
  return a_path;
}

std::string directoryOf(const std::string& a_path)
{
  auto parent = std::filesystem::path(a_path).parent_path();
  return parent.empty() ? "." : parent.string();
}

static std::string defaultOutputPath()
{
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  struct tm tm {};
  localtime_r(&tt, &tm);
  return std::format("Recording_{:04d}{:02d}{:02d}T{:02d}{:02d}{:02d}.caf",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                     tm.tm_min, tm.tm_sec);
}

std::expected<std::string, std::string> resolveOutputPath(
    const std::string& a_user_path)
{
  std::string path;
  if (a_user_path.empty())
  {
    path = defaultOutputPath();
  }
  else
  {
    auto result = expandHome(a_user_path);
    if (!result)
      return std::unexpected(std::move(result.error()));
    path = std::move(*result);
  }

  if (!path.ends_with(".caf"))
    path += ".caf";

  // Auto-increment to avoid overwriting existing files
  if (std::filesystem::exists(path))
  {
    std::filesystem::path p(path);
    auto parent = p.parent_path();
    auto stem = p.stem().string();
    auto ext = p.extension().string();

    bool found = false;
    for (int i = 1; i <= 9999; i++)
    {
      auto candidate = parent / std::format("{}_{}{}", stem, i, ext);
      if (!std::filesystem::exists(candidate))
      {
        path = candidate.string();
        found = true;
        break;
      }
    }
    if (!found)
      return std::unexpected(
          std::string("Error: too many existing files with same base name.\n"));
  }

  if (path.size() > PATH_MAX)
    return std::unexpected(
        std::string("Error: output path exceeds system path length limit.\n"));

  return path;
}

std::expected<void, std::string> checkDiskSpace(const std::string& a_dir,
                                                uint64_t a_bytes_per_sec,
                                                int a_max_duration_min,
                                                const char* a_ch_label)
{
  uint64_t free_bytes = getFreeBytes(a_dir);

  if (a_max_duration_min > 0)
  {
    uint64_t required =
        a_bytes_per_sec * 60 * static_cast<uint64_t>(a_max_duration_min);
    if (free_bytes < required)
    {
      return std::unexpected(std::format(
          "Error: {:.1f} GB free, but {:.1f} GB required for {} minutes "
          "at current settings / {}.\n",
          static_cast<double>(free_bytes) / 1e9,
          static_cast<double>(required) / 1e9, a_max_duration_min, a_ch_label));
    }
  }
  else if (free_bytes != UINT64_MAX)
  {
    constexpr uint64_t k_sec_per_day = 86400;
    uint64_t worst_case = a_bytes_per_sec * k_sec_per_day;
    if (free_bytes < worst_case)
    {
      double max_hours = static_cast<double>(free_bytes) /
                         static_cast<double>(a_bytes_per_sec) / 3600.0;
      printErr(
          "Warning: {:.1f} GB free. At current settings / {}, "
          "recording limited to ~{:.1f} hours.\n",
          static_cast<double>(free_bytes) / 1e9, a_ch_label, max_hours);
    }
  }
  return {};
}
