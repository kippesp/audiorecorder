// util.h -- Shared utility helpers.

#pragma once

#include <MacTypes.h>
#include <cctype>
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

inline std::string formatOSStatus(OSStatus a_status)
{
  char c[4] = {
      static_cast<char>((a_status >> 24) & 0xFF),
      static_cast<char>((a_status >> 16) & 0xFF),
      static_cast<char>((a_status >> 8) & 0xFF),
      static_cast<char>(a_status & 0xFF),
  };
  if (std::isprint(static_cast<unsigned char>(c[0])) &&
      std::isprint(static_cast<unsigned char>(c[1])) &&
      std::isprint(static_cast<unsigned char>(c[2])) &&
      std::isprint(static_cast<unsigned char>(c[3])))
    return std::format("'{}{}{}{}' ({})", c[0], c[1], c[2], c[3], a_status);
  return std::format("{}", a_status);
}
