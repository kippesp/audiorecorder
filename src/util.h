// util.h -- Shared utility helpers.

#pragma once

#include <cstdio>
#include <format>
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
