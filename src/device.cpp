// device.cpp -- Audio input device enumeration and resolution.

#include "device.h"
#include "util.h"

#include <CoreServices/CoreServices.h>

#include <cstdlib>
#include <format>

static std::string cfToString(CFStringRef a_cfstr)
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
  for (char& c : result)
  {
    auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc > 0x7E)
      c = '_';
  }
  constexpr size_t k_max_device_name_len = 256;
  if (result.size() > k_max_device_name_len)
    result.resize(k_max_device_name_len);
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
    // Check input channel count
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

    // Name
    CFStringRef cf_name = nullptr;
    prop = {kAudioDevicePropertyDeviceNameCFString,
            kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    size = sizeof(cf_name);
    if (AudioObjectGetPropertyData(dev_id, &prop, 0, nullptr, &size,
                                   &cf_name) == noErr &&
        cf_name)
    {
      dev.name = cfToString(cf_name);
      CFRelease(cf_name);
    }

    // UID (persistent across reboots)
    CFStringRef cf_uid = nullptr;
    prop = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain};
    size = sizeof(cf_uid);
    if (AudioObjectGetPropertyData(dev_id, &prop, 0, nullptr, &size, &cf_uid) ==
            noErr &&
        cf_uid)
    {
      dev.uid = cfToString(cf_uid);
      CFRelease(cf_uid);
    }

    // Sample rate
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
  AudioDevice device {};

  if (!a_selector)
  {
    AudioDeviceID default_id = getDefaultInputDevice();
    if (default_id == kAudioObjectUnknown)
      return std::unexpected(
          std::string("Error: no default input device found.\n"));
    for (auto& d : a_devices)
    {
      if (d.id == default_id)
      {
        device = d;
        break;
      }
    }
    if (device.id == kAudioObjectUnknown)
      return std::unexpected(
          std::string("Error: default input device not available.\n"));
  }
  else
  {
    const std::string& selector = *a_selector;
    // Try numeric index first (1-based)
    char* end = nullptr;
    long idx = strtol(selector.c_str(), &end, 10);
    if (end != selector.c_str() && *end == '\0' && idx >= 1 &&
        idx <= static_cast<long>(a_devices.size()))
    {
      device = a_devices[static_cast<size_t>(idx - 1)];
    }
    // Then match UID
    if (device.id == kAudioObjectUnknown)
    {
      for (auto& d : a_devices)
      {
        if (d.uid == selector)
        {
          device = d;
          break;
        }
      }
    }
    // Then match name
    if (device.id == kAudioObjectUnknown)
    {
      for (auto& d : a_devices)
      {
        if (d.name == selector)
        {
          device = d;
          break;
        }
      }
    }
    if (device.id == kAudioObjectUnknown)
    {
      return std::unexpected(
          std::format("Error: no input device matches '{}'.\n"
                      "Use --list-devices to see available devices.\n",
                      selector));
    }
  }

  return device;
}
