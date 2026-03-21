// output_file.cpp -- CAF output file creation.

#include "output_file.h"
#include "util.h"

#include <AudioToolbox/ExtendedAudioFile.h>
#include <CoreServices/CoreServices.h>

std::expected<AudioFileGuard, OSStatus> createOutputFile(
    const std::string& a_path, Float64 a_sample_rate,
    uint32_t a_output_channels)
{
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(a_path.c_str()),
      static_cast<CFIndex>(a_path.length()), false);
  if (!url)
  {
    printErr("Error: invalid output path '{}'.\n", a_path);
    return std::unexpected(static_cast<OSStatus>(fnfErr));
  }

  // File format: 24-bit signed integer, packed
  AudioStreamBasicDescription file_fmt = {};
  file_fmt.mSampleRate = a_sample_rate;
  file_fmt.mFormatID = kAudioFormatLinearPCM;
  file_fmt.mFormatFlags =
      kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  file_fmt.mBitsPerChannel = 24;
  file_fmt.mChannelsPerFrame = a_output_channels;
  file_fmt.mFramesPerPacket = 1;
  file_fmt.mBytesPerFrame = 3 * a_output_channels;
  file_fmt.mBytesPerPacket = file_fmt.mBytesPerFrame;

  ExtAudioFileRef audio_file = nullptr;
  OSStatus status =
      ExtAudioFileCreateWithURL(url, kAudioFileCAFType, &file_fmt, nullptr,
                                kAudioFileFlags_EraseFile, &audio_file);
  CFRelease(url);
  if (status != noErr)
    return std::unexpected(status);

  // Client format: float (ExtAudioFile converts to 24-bit on write)
  AudioStreamBasicDescription client_fmt = {};
  client_fmt.mSampleRate = a_sample_rate;
  client_fmt.mFormatID = kAudioFormatLinearPCM;
  client_fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  client_fmt.mBitsPerChannel = 32;
  client_fmt.mChannelsPerFrame = a_output_channels;
  client_fmt.mFramesPerPacket = 1;
  client_fmt.mBytesPerFrame = sizeof(float) * a_output_channels;
  client_fmt.mBytesPerPacket = client_fmt.mBytesPerFrame;

  status = ExtAudioFileSetProperty(audio_file,
                                   kExtAudioFileProperty_ClientDataFormat,
                                   sizeof(client_fmt), &client_fmt);
  if (status != noErr)
  {
    ExtAudioFileDispose(audio_file);
    return std::unexpected(status);
  }

  return AudioFileGuard(audio_file);
}
