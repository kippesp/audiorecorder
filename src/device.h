// device.h -- Audio input device enumeration and resolution.

#pragma once

#include <CoreAudio/CoreAudio.h>

#include <expected>
#include <string>
#include <vector>

struct AudioDevice {
  AudioDeviceID id_ = kAudioObjectUnknown;
  std::string name_;
  std::string uid_;
  Float64 sample_rate_ = 0;
  uint32_t input_channels_ = 0;
};

std::vector<AudioDevice> getInputDevices();
void printDeviceList(const std::vector<AudioDevice>& a_devices);
std::expected<AudioDevice, std::string> resolveSelectedDevice(
    const std::string& a_selector, const std::vector<AudioDevice>& a_devices);
