// writer.cpp -- Writer thread (ring drain, file write).

#include "writer.h"
#include "util.h"

#include <AudioToolbox/ExtendedAudioFile.h>

#include <cstdio>
#include <format>

void writerFn(std::stop_token a_stoken, RecordingContext* a_ctx,
              ExtAudioFileRef a_audio_file)
{
  size_t cap = a_ctx->ring_.capacity();
  std::vector<float> drain_buf(cap);

  auto drainRing = [&]() {
    size_t available =
        a_ctx->ring_.popAll({drain_buf.data(), drain_buf.size()});
    if (available == 0)
      return;

    size_t frame_count = available / a_ctx->channels_;

    // Drain-only mode when no output file (test mode)
    if (!a_audio_file)
    {
      a_ctx->frames_written_.fetch_add(frame_count, std::memory_order_relaxed);
      return;
    }

    // Write to file
    AudioBufferList abl;
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = a_ctx->channels_;
    abl.mBuffers[0].mDataByteSize =
        static_cast<UInt32>(frame_count * a_ctx->channels_ * sizeof(float));
    abl.mBuffers[0].mData = drain_buf.data();

    OSStatus write_status =
        ExtAudioFileWrite(a_audio_file, static_cast<UInt32>(frame_count), &abl);
    if (write_status != noErr)
    {
      printErr("\nError: disk write failed ({}).\n",
               formatOSStatus(write_status));
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
