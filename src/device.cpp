// device.cpp -- Audio input device enumeration and resolution.

#include "device.h"
#include "cf_util.h"
#include "util.h"

#include <CoreServices/CoreServices.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

namespace {

template <typename T>
std::expected<T, OSStatus> queryFixedProperty(
    AudioObjectID a_id, AudioObjectPropertySelector a_selector,
    AudioObjectPropertyScope a_scope = kAudioObjectPropertyScopeGlobal)
{
  AudioObjectPropertyAddress prop {a_selector, a_scope,
                                   kAudioObjectPropertyElementMain};
  T value {};
  UInt32 size = sizeof(value);
  if (OSStatus status =
          AudioObjectGetPropertyData(a_id, &prop, 0, nullptr, &size, &value);
      status != noErr)
    return std::unexpected(status);
  return value;
}

template <typename T>
std::expected<CFRef<T>, OSStatus> queryCFProperty(
    AudioObjectID a_id, AudioObjectPropertySelector a_selector,
    AudioObjectPropertyScope a_scope = kAudioObjectPropertyScopeGlobal)
{
  AudioObjectPropertyAddress prop {a_selector, a_scope,
                                   kAudioObjectPropertyElementMain};
  T raw = nullptr;
  UInt32 size = sizeof(raw);
  if (OSStatus status =
          AudioObjectGetPropertyData(a_id, &prop, 0, nullptr, &size, &raw);
      status != noErr)
    return std::unexpected(status);
  if (!raw)
    return std::unexpected(kAudioHardwareUnspecifiedError);
  return CFRef<T>(raw);
}

std::expected<uint32_t, OSStatus> queryInputChannelCount(AudioObjectID a_id)
{
  AudioObjectPropertyAddress prop {kAudioDevicePropertyStreamConfiguration,
                                   kAudioDevicePropertyScopeInput,
                                   kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (OSStatus status =
          AudioObjectGetPropertyDataSize(a_id, &prop, 0, nullptr, &size);
      status != noErr)
    return std::unexpected(status);
  std::vector<uint8_t> buf(size);
  auto* list = reinterpret_cast<AudioBufferList*>(buf.data());
  if (OSStatus status =
          AudioObjectGetPropertyData(a_id, &prop, 0, nullptr, &size, list);
      status != noErr)
    return std::unexpected(status);
  uint32_t total = 0;
  for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
    total += list->mBuffers[i].mNumberChannels;
  return total;
}

}  // namespace

// Caps display length and replaces control characters and bytes outside the
// printable-ASCII range so a hostile or oddly-named device cannot inject
// terminal escape sequences into stderr output. Multi-byte UTF-8 names lose
// fidelity here; that tradeoff is intentional.
static std::string sanitizeForDisplay(std::string a_str)
{
  constexpr size_t k_max_device_name_len = 256;
  if (a_str.size() > k_max_device_name_len)
    a_str.resize(k_max_device_name_len);
  for (char& c : a_str)
  {
    auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc > 0x7E)
      c = '_';
  }
  return a_str;
}

static AudioDeviceID getDefaultInputDevice()
{
  return queryFixedProperty<AudioDeviceID>(
             kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice)
      .value_or(kAudioObjectUnknown);
}

std::expected<std::vector<AudioDevice>, std::string> getInputDevices()
{
  AudioObjectPropertyAddress prop = {kAudioHardwarePropertyDevices,
                                     kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
  UInt32 size = 0;
  if (OSStatus status = AudioObjectGetPropertyDataSize(
          kAudioObjectSystemObject, &prop, 0, nullptr, &size);
      status != noErr)
    return std::unexpected(
        std::format("Error: failed to query device list size ({}).\n",
                    formatOSStatus(status)));

  std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
  if (OSStatus status = AudioObjectGetPropertyData(
          kAudioObjectSystemObject, &prop, 0, nullptr, &size, ids.data());
      status != noErr)
    return std::unexpected(std::format(
        "Error: failed to fetch device list ({}).\n", formatOSStatus(status)));

  std::vector<AudioDevice> result;
  for (auto dev_id : ids)
  {
    auto channels = queryInputChannelCount(dev_id);
    if (!channels || *channels == 0)
      continue;

    auto rate = queryFixedProperty<Float64>(
        dev_id, kAudioDevicePropertyNominalSampleRate);
    if (!rate)
      continue;

    AudioDevice dev {};
    dev.id = dev_id;
    dev.input_channels = *channels;
    dev.sample_rate = *rate;

    if (auto cf_name = queryCFProperty<CFStringRef>(
            dev_id, kAudioDevicePropertyDeviceNameCFString))
      dev.name = sanitizeForDisplay(cfToString(cf_name->get()));

    // UID is persistent across reboots; usable as a stable CLI selector.
    if (auto cf_uid =
            queryCFProperty<CFStringRef>(dev_id, kAudioDevicePropertyDeviceUID))
      dev.uid = sanitizeForDisplay(cfToString(cf_uid->get()));

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

    if (auto it = std::ranges::find(a_devices, default_id, &AudioDevice::id);
        it != a_devices.end())
      return *it;
    return std::unexpected(
        std::string("Error: default input device not available.\n"));
  }

  const std::string& selector = *a_selector;

  // 1-based index has priority over UID so an all-digit UID never shadows
  // the index form printed by --list-devices.
  size_t idx = 0;
  const char* sel_end = selector.data() + selector.size();
  auto [ptr, status] = std::from_chars(selector.data(), sel_end, idx);
  if (status == std::errc {} && ptr == sel_end && idx >= 1 &&
      idx <= a_devices.size())
    return a_devices[idx - 1];

  if (auto it = std::ranges::find(a_devices, selector, &AudioDevice::uid);
      it != a_devices.end())
    return *it;

  return std::unexpected(
      std::format("Error: no input device matches '{}'.\n"
                  "Use --list-devices to see available devices.\n",
                  selector));
}
