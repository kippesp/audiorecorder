// device.cpp -- Audio input device enumeration and resolution.

#include "device.h"
#include "cf_util.h"
#include "util.h"

#include <CoreServices/CoreServices.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

// Caps display length and replaces control characters and bytes outside the
// printable-ASCII range so a hostile or oddly-named device cannot inject
// terminal escape sequences into stderr output. Multi-byte UTF-8 names lose
// fidelity here; that tradeoff is intentional.
static std::string cfToDisplay(CFStringRef a_cfstr)
{
  constexpr size_t k_max_device_name_len = 256;
  std::string result = cfToString(a_cfstr);
  if (result.size() > k_max_device_name_len)
    result.resize(k_max_device_name_len);
  for (char& c : result)
  {
    auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc > 0x7E)
      c = '_';
  }
  return result;
}

static AudioDeviceID getDefaultInputDevice()
{
  AudioDeviceID dev_id = kAudioObjectUnknown;
  AudioObjectPropertyAddress prop = {kAudioHardwarePropertyDefaultInputDevice,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(dev_id);
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &size,
                             &dev_id);
  return dev_id;
}

std::vector<AudioDevice> getInputDevices()
{
  std::vector<AudioDevice> result;

  AudioObjectPropertyAddress prop = {kAudioHardwarePropertyDevices,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0,
                                     nullptr, &size) != noErr)
    return result;

  std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr,
                                 &size, ids.data()) != noErr)
    return result;

  for (auto dev_id : ids)
  {
    prop = {kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyDataSize(dev_id, &prop, 0, nullptr, &size) !=
        noErr)
      continue;

    std::vector<uint8_t> cfg_buf(size);
    auto* buf_list = reinterpret_cast<AudioBufferList*>(cfg_buf.data());
    if (AudioObjectGetPropertyData(dev_id, &prop, 0, nullptr, &size,
                                   buf_list) != noErr)
      continue;

    uint32_t in_ch = 0;
    for (UInt32 i = 0; i < buf_list->mNumberBuffers; i++)
      in_ch += buf_list->mBuffers[i].mNumberChannels;
    if (in_ch == 0)
      continue;

    AudioDevice dev {};
    dev.id = dev_id;
    dev.input_channels = in_ch;

    CFStringRef cf_name = nullptr;
    prop = {kAudioDevicePropertyDeviceNameCFString,
            kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    size = sizeof(cf_name);
    if (AudioObjectGetPropertyData(dev_id, &prop, 0, nullptr, &size,
                                   &cf_name) == noErr &&
        cf_name)
    {
      dev.name = cfToDisplay(cf_name);
      CFRelease(cf_name);
    }

    // UID is persistent across reboots; usable as a stable CLI selector.
    CFStringRef cf_uid = nullptr;
    prop = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain};
    size = sizeof(cf_uid);
    if (AudioObjectGetPropertyData(dev_id, &prop, 0, nullptr, &size, &cf_uid) ==
            noErr &&
        cf_uid)
    {
      dev.uid = cfToDisplay(cf_uid);
      CFRelease(cf_uid);
    }

    prop = {kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    size = sizeof(dev.sample_rate);
    if (AudioObjectGetPropertyData(dev_id, &prop, 0, nullptr, &size,
                                   &dev.sample_rate) != noErr)
      printErr("Warning: could not query sample rate for device '{}'.\n",
               dev.name);

    result.push_back(std::move(dev));
  }
  return result;
}

void printDeviceList(const std::vector<AudioDevice>& a_devices)
{
  if (a_devices.empty())
  {
    printErr("No input devices found.\n");
    return;
  }
  printErr("Input devices:\n");
  // TODO: replace with std::views::enumerate (C++23, P2164) once libc++ ships
  // it, or pull in a stand-in from an alternate ranges library tracking the
  // std.
  for (size_t i = 0; i < a_devices.size(); i++)
  {
    auto& d = a_devices[i];
    printErr("  {}) {:<35s}  UID: {:<30s}  {:.0f} Hz  {} ch\n", i + 1, d.name,
             d.uid, d.sample_rate, d.input_channels);
  }
}

std::expected<AudioDevice, std::string> resolveSelectedDevice(
    const std::optional<std::string>& a_selector,
    const std::vector<AudioDevice>& a_devices)
{
  if (!a_selector)
  {
    AudioDeviceID default_id = getDefaultInputDevice();
    if (default_id == kAudioObjectUnknown)
      return std::unexpected(
          std::string("Error: no default input device found.\n"));

    auto it = std::ranges::find_if(
        a_devices,
        [default_id](const AudioDevice& d) { return d.id == default_id; });
    if (it == a_devices.end())
      return std::unexpected(
          std::string("Error: default input device not available.\n"));
    return *it;
  }

  const std::string& selector = *a_selector;

  // 1-based index has priority so a name that happens to be all-digits never
  // shadows the index form printed by --list-devices.
  size_t idx = 0;
  const char* sel_end = selector.data() + selector.size();
  auto [ptr, status] = std::from_chars(selector.data(), sel_end, idx);
  if (status == std::errc {} && ptr == sel_end && idx >= 1 &&
      idx <= a_devices.size())
    return a_devices[idx - 1];

  if (auto it = std::ranges::find_if(
          a_devices,
          [&selector](const AudioDevice& d) { return d.uid == selector; });
      it != a_devices.end())
    return *it;

  if (auto it = std::ranges::find_if(
          a_devices,
          [&selector](const AudioDevice& d) { return d.name == selector; });
      it != a_devices.end())
    return *it;

  return std::unexpected(
      std::format("Error: no input device matches '{}'.\n"
                  "Use --list-devices to see available devices.\n",
                  selector));
}
