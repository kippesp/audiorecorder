// util.h -- Shared utility helpers.

#pragma once

#include <MacTypes.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <utility>

inline void printErr(const char* a_msg)
{
  fputs(a_msg, stderr);
}

inline void printErr(const std::string& a_msg)
{
  fputs(a_msg.c_str(), stderr);
}

template <typename... Args>
void printErr(std::format_string<Args...> a_fmt, Args&&... a_args)
{
  fputs(std::format(a_fmt, std::forward<Args>(a_args)...).c_str(), stderr);
}

inline bool isPrintableFourCC(uint32_t a_code)
{
  for (int i = 0; i < 4; ++i)
  {
    char ch = static_cast<char>((a_code >> (24 - 8 * i)) & 0xFF);
    if (!std::isprint(static_cast<unsigned char>(ch)))
      return false;
  }
  return true;
}

inline std::string formatFourCC(uint32_t a_code)
{
  if (isPrintableFourCC(a_code))
    return std::format("'{}{}{}{}'", static_cast<char>((a_code >> 24) & 0xFF),
                       static_cast<char>((a_code >> 16) & 0xFF),
                       static_cast<char>((a_code >> 8) & 0xFF),
                       static_cast<char>(a_code & 0xFF));
  return std::format("0x{:08x}", a_code);
}

inline std::string formatOSStatus(OSStatus a_status)
{
  uint32_t code = static_cast<uint32_t>(a_status);
  if (isPrintableFourCC(code))
    return std::format("{} ({})", formatFourCC(code), a_status);
  return std::format("{}", a_status);
}
