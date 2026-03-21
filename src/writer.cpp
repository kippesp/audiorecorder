// writer.cpp -- Writer thread (ring drain, mixdown, file write).

#include "writer.h"
#include "util.h"

#include <AudioToolbox/ExtendedAudioFile.h>

#include <cstdio>
#include <format>

void writerFn(std::stop_token a_stoken, RecordingContext* a_ctx,
              ExtAudioFileRef a_audio_file, uint32_t a_output_channels)
{
  bool do_mono = (a_output_channels == 1 && a_ctx->channels_ == 2);
  size_t cap = a_ctx->ring_.capacity();
  std::vector<float> drain_buf(cap);
  std::vector<float> mono_buf;
  if (do_mono)
    mono_buf.resize(cap / 2);

  auto drainRing = [&]() {
    size_t available =
        a_ctx->ring_.popAll({drain_buf.data(), drain_buf.size()});
    if (available == 0)
      return;

    // Prepare output
    float* out_data = drain_buf.data();
    uint32_t out_ch = a_ctx->channels_;
    size_t frame_count = available / a_ctx->channels_;

    // Drain-only mode when no output file (test mode)
    if (!a_audio_file)
    {
      a_ctx->frames_written_.fetch_add(frame_count, std::memory_order_relaxed);
      return;
    }

    if (do_mono)
    {
      constexpr float k_mono_mix = 0.70710678118f;  // -3dB constant power sum
      for (size_t i = 0; i < frame_count; i++)
        mono_buf[i] = (drain_buf[i * 2] + drain_buf[i * 2 + 1]) * k_mono_mix;
      out_data = mono_buf.data();
      out_ch = 1;
    }

    // Write to file
    AudioBufferList abl;
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = out_ch;
    abl.mBuffers[0].mDataByteSize =
        static_cast<UInt32>(frame_count * out_ch * sizeof(float));
    abl.mBuffers[0].mData = out_data;

    OSStatus write_status =
        ExtAudioFileWrite(a_audio_file, static_cast<UInt32>(frame_count), &abl);
    if (write_status != noErr)
    {
      printErr("\nError: disk write failed (OSStatus {}).\n",
               static_cast<int>(write_status));
      a_ctx->write_error_.store(true, std::memory_order_relaxed);
      return;
    }
    a_ctx->frames_written_.fetch_add(frame_count, std::memory_order_relaxed);
  };

  while (!a_stoken.stop_requested() &&
         !a_ctx->write_error_.load(std::memory_order_relaxed))
  {
    a_ctx->writer_sem_.try_acquire_for(std::chrono::milliseconds(100));
    drainRing();
  }

  // Final drain after stop
  drainRing();
}
