// monitor.cpp -- Monitor playback setup and callback.

#include "monitor.h"

#include <CoreServices/CoreServices.h>

#include <cstring>

// Called by CoreAudio's output unit on its real-time thread when it needs
// samples to play.  Pops from monitor_ring (fed by inputCallback) and
// zero-fills any shortfall to avoid outputting garbage on underrun.
static OSStatus monitorCallback(void* a_ref_con,
                                AudioUnitRenderActionFlags* /*a_action_flags*/,
                                const AudioTimeStamp* /*a_time_stamp*/,
                                UInt32 /*a_bus_number*/, UInt32 a_number_frames,
                                AudioBufferList* a_buf_list)
{
  auto* ctx = static_cast<RecordingContext*>(a_ref_con);
  uint32_t ch = ctx->monitor_channels;
  size_t requested = static_cast<size_t>(a_number_frames) * ch;
  auto* out = static_cast<float*>(a_buf_list->mBuffers[0].mData);

  size_t got = ctx->monitor_ring.pop({out, requested});

  // Fill remainder with silence on underrun
  if (got < requested)
    std::memset(out + got, 0, (requested - got) * sizeof(float));

  return noErr;
}

// Configures a CoreAudio default-output unit to play captured audio through
// the speakers/headphones (--monitor flag). Only called when monitoring is
// requested; otherwise no output unit exists and monitor_ring stays unused.
// The setup is boilerplate required by CoreAudio: find the output component,
// set the stream format to match the capture device, register monitorCallback
// as the sample source, and initialize.
std::expected<AudioComponentInstance, OSStatus> setupMonitorUnit(
    RecordingContext& a_ctx, Float64 a_sample_rate, uint32_t a_channels)
{
  a_ctx.monitor_channels = a_channels;

  // ~0.5 seconds of buffer -- enough to bridge input/output callback timing
  size_t ring_samples = static_cast<size_t>(a_sample_rate * 0.5) * a_channels;
  a_ctx.monitor_ring.init(ring_samples);

  AudioComponentDescription desc = {};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;

  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (!comp)
    return std::unexpected(static_cast<OSStatus>(fnfErr));

  AudioComponentInstance unit = nullptr;
  OSStatus status = AudioComponentInstanceNew(comp, &unit);
  if (status != noErr)
    return std::unexpected(status);

  auto fail = [&](OSStatus a_status) -> std::unexpected<OSStatus> {
    AudioComponentInstanceDispose(unit);
    return std::unexpected(a_status);
  };

  // Client format: interleaved float matching the input device
  AudioStreamBasicDescription fmt = {};
  fmt.mSampleRate = a_sample_rate;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  fmt.mBitsPerChannel = 32;
  fmt.mChannelsPerFrame = a_channels;
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerFrame = sizeof(float) * a_channels;
  fmt.mBytesPerPacket = fmt.mBytesPerFrame;

  status = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
  if (status != noErr)
    return fail(status);

  AURenderCallbackStruct cb = {monitorCallback, &a_ctx};
  status = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &cb, sizeof(cb));
  if (status != noErr)
    return fail(status);

  status = AudioUnitInitialize(unit);
  if (status != noErr)
    return fail(status);

  return unit;
}
