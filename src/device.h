// device.h -- Audio input device enumeration and resolution.

#pragma once

#include <CoreAudio/CoreAudio.h>

#include <expected>
#include <optional>
#include <string>
#include <vector>

struct AudioDevice {
  AudioDeviceID id = kAudioObjectUnknown;
  std::string name;
  std::string uid;
  Float64 sample_rate = 0;
  uint32_t input_channels = 0;
};

std::expected<std::vector<AudioDevice>, std::string> getInputDevices();
void printDeviceList(const std::vector<AudioDevice>& a_devices);
std::expected<AudioDevice, std::string> resolveSelectedDevice(
    const std::optional<std::string>& a_selector,
    const std::vector<AudioDevice>& a_devices);
