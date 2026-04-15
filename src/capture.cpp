// capture.cpp -- CoreAudio capture unit setup and input callback.

#include "capture.h"
#include "util.h"

#include <CoreServices/CoreServices.h>

#include <cmath>

// Called by CoreAudio's AUHAL input unit on a dedicated real-time thread
// each time the microphone hardware delivers a buffer of samples. Renders
// audio into render_buf_, pushes to the recording ring (for the writer
// thread) and the monitor ring (for speaker playback if enabled), computes
// per-buffer peak levels, and signals the writer via the semaphore.
static OSStatus inputCallback(void* a_ref_con,
                              AudioUnitRenderActionFlags* a_action_flags,
                              const AudioTimeStamp* a_time_stamp,
                              UInt32 a_bus_number, UInt32 a_number_frames,
                              [[maybe_unused]] AudioBufferList* a_data)
{
  auto* ctx = static_cast<RecordingContext*>(a_ref_con);
  uint32_t ch = ctx->channels;

  // Set up buffer for AudioUnitRender.
  // render_buf_ is safe without synchronization because AUHAL invokes the
  // input callback on a single dedicated real-time thread per audio unit.
  AudioBufferList buf_list;
  buf_list.mNumberBuffers = 1;
  buf_list.mBuffers[0].mNumberChannels = ch;
  buf_list.mBuffers[0].mDataByteSize =
      a_number_frames * ch * static_cast<UInt32>(sizeof(float));
  buf_list.mBuffers[0].mData = ctx->render_buf.data();

  OSStatus status =
      AudioUnitRender(ctx->audio_unit, a_action_flags, a_time_stamp,
                      a_bus_number, a_number_frames, &buf_list);
  if (status != noErr)
    return status;

  size_t sample_count = static_cast<size_t>(a_number_frames) * ch;

  // Push to monitor ring (drop silently on overrun)
  if (ctx->monitor_ring.capacity() > 0)
    ctx->monitor_ring.push({ctx->render_buf.data(), sample_count});

  // Push into recording ring buffer
  if (!ctx->ring.push({ctx->render_buf.data(), sample_count}))
  {
    ctx->overrun_frames.fetch_add(a_number_frames, std::memory_order_relaxed);
    return noErr;
  }

  // Peak metering
  const float* src = ctx->render_buf.data();
  float peak_l = 0.0f, peak_r = 0.0f;
  if (ch == 2)
  {
    for (size_t i = 0; i < a_number_frames; i++)
    {
      peak_l = std::max(peak_l, std::abs(src[i * 2]));
      peak_r = std::max(peak_r, std::abs(src[i * 2 + 1]));
    }
  }
  else
  {
    for (size_t i = 0; i < a_number_frames; i++)
      peak_l = std::max(peak_l, std::abs(src[i]));
  }

  // Atomic max update via compare-exchange loop
  auto updatePeak = [](std::atomic<float>& a_atom, float a_val) {
    float prev = a_atom.load(std::memory_order_relaxed);
    while (prev < a_val && !a_atom.compare_exchange_weak(
                               prev, a_val, std::memory_order_relaxed))
    {
    }
  };
  updatePeak(ctx->peak_l, peak_l);
  updatePeak(ctx->peak_r, peak_r);
  updatePeak(ctx->session_peak_l, peak_l);
  updatePeak(ctx->session_peak_r, peak_r);

  ctx->writer_sem.release();
  return noErr;
}

std::expected<void, OSStatus> setupCaptureUnit(RecordingContext& a_ctx,
                                               AudioDeviceID a_device_id,
                                               Float64 a_sample_rate,
                                               uint32_t a_record_channels)
{
  a_ctx.channels = a_record_channels;

  // Ring buffer sized at ~60 seconds to absorb macOS disk I/O stalls
  // (Spotlight indexing, Time Machine snapshots, APFS CoW bursts) that can
  // block writes for tens of seconds. At 96 kHz stereo this is ~46 MB
  // (rounded to power-of-2 by RingBuffer::init).
  size_t ring_frames = static_cast<size_t>(a_sample_rate * 60.0);
  size_t ring_samples = ring_frames * a_record_channels;
  a_ctx.ring.init(ring_samples);

  // Find AUHAL component
  AudioComponentDescription desc = {};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_HALOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;

  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (!comp)
    return std::unexpected(static_cast<OSStatus>(fnfErr));

  OSStatus status = AudioComponentInstanceNew(comp, &a_ctx.audio_unit);
  if (status != noErr)
    return std::unexpected(status);

  auto fail = [&](OSStatus a_status) -> std::unexpected<OSStatus> {
    AudioComponentInstanceDispose(a_ctx.audio_unit);
    a_ctx.audio_unit = nullptr;
    return std::unexpected(a_status);
  };

  // Enable input on bus 1
  UInt32 enable_io = 1;
  status = AudioUnitSetProperty(
      a_ctx.audio_unit, kAudioOutputUnitProperty_EnableIO,
      kAudioUnitScope_Input, 1, &enable_io, sizeof(enable_io));
  if (status != noErr)
    return fail(status);

  // Disable output on bus 0
  enable_io = 0;
  status = AudioUnitSetProperty(
      a_ctx.audio_unit, kAudioOutputUnitProperty_EnableIO,
      kAudioUnitScope_Output, 0, &enable_io, sizeof(enable_io));
  if (status != noErr)
    return fail(status);

  // Set input device
  status = AudioUnitSetProperty(
      a_ctx.audio_unit, kAudioOutputUnitProperty_CurrentDevice,
      kAudioUnitScope_Global, 0, &a_device_id, sizeof(a_device_id));
  if (status != noErr)
    return fail(status);

  // Set desired format: interleaved float
  AudioStreamBasicDescription fmt = {};
  fmt.mSampleRate = a_sample_rate;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  fmt.mBitsPerChannel = 32;
  fmt.mChannelsPerFrame = a_record_channels;
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerFrame = sizeof(float) * a_record_channels;
  fmt.mBytesPerPacket = fmt.mBytesPerFrame;

  status =
      AudioUnitSetProperty(a_ctx.audio_unit, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Output, 1, &fmt, sizeof(fmt));
  if (status != noErr)
    return fail(status);

  // Set input callback
  AURenderCallbackStruct cb = {inputCallback, &a_ctx};
  status = AudioUnitSetProperty(a_ctx.audio_unit,
                                kAudioOutputUnitProperty_SetInputCallback,
                                kAudioUnitScope_Global, 0, &cb, sizeof(cb));
  if (status != noErr)
    return fail(status);

  // Pre-allocate render buffer
  UInt32 max_frames = 4096;
  UInt32 prop_size = sizeof(max_frames);
  status = AudioUnitGetProperty(
      a_ctx.audio_unit, kAudioUnitProperty_MaximumFramesPerSlice,
      kAudioUnitScope_Global, 0, &max_frames, &prop_size);
  if (status != noErr)
    return fail(status);
  a_ctx.render_buf.resize(static_cast<size_t>(max_frames) * a_record_channels);

  // Initialize audio unit
  status = AudioUnitInitialize(a_ctx.audio_unit);
  if (status != noErr)
    return fail(status);

  return {};
}
