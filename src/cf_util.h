// cf_util.h -- CoreFoundation helpers, kept out of util.h so the broader
// utility header does not pull CF into every TU.

#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <string>

// UTF-8 conversion of a CFString. Two-pass (size, fill) is the canonical CF
// idiom for lossy substitution; CFStringGetCStringPtr's fast path returns
// nullptr for most CoreAudio-supplied strings.
inline std::string cfToString(CFStringRef a_cfstr)
{
  if (!a_cfstr)
    return {};
  CFIndex len = CFStringGetLength(a_cfstr);
  CFIndex buf_size = 0;
  CFStringGetBytes(a_cfstr, CFRangeMake(0, len), kCFStringEncodingUTF8, '?',
                   false, nullptr, 0, &buf_size);
  std::string result(static_cast<size_t>(buf_size), '\0');
  CFStringGetBytes(a_cfstr, CFRangeMake(0, len), kCFStringEncodingUTF8, '?',
                   false, reinterpret_cast<UInt8*>(result.data()), buf_size,
                   nullptr);
  return result;
}
