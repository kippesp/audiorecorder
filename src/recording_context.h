// recording_context.h -- Shared recording state and RAII resource guards.

#pragma once

#include "ring_buffer.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <atomic>
#include <memory>
#include <semaphore>
#include <vector>

struct RecordingContext {
  AudioComponentInstance audio_unit = nullptr;
  uint32_t channels = 0;  // channels captured from device (1 or 2)

  // Ring buffer (float samples, interleaved)
  RingBuffer ring;

  // Metering (updated by callback, read/reset by display)
  std::atomic<float> peak_l {0.0f};
  std::atomic<float> peak_r {0.0f};

  // Session-wide peak levels (monotonically increasing)
  std::atomic<float> session_peak_l {0.0f};
  std::atomic<float> session_peak_r {0.0f};

  // Overrun tracking
  std::atomic<uint64_t> overrun_frames {0};

  // High-water mark for ring buffer occupancy (samples)
  std::atomic<size_t> ring_peak_used {0};

  // Frame counter (updated by writer thread)
  std::atomic<uint64_t> frames_written {0};

  // Set by writer thread on unrecoverable disk write failure
  std::atomic<bool> write_error {false};

  // Pre-allocated buffer for AudioUnitRender
  std::vector<float> render_buf;

  // Monitor playback (optional; ring left uninitialized when disabled)
  RingBuffer monitor_ring;
  uint32_t monitor_channels = 0;

  // Signals the writer thread that audio data is available to drain.
  std::counting_semaphore<8192> writer_sem {0};
};

struct AudioUnitDeleter {
  void operator()(AudioComponentInstance a_unit) const
  {
    AudioUnitUninitialize(a_unit);
    AudioComponentInstanceDispose(a_unit);
  }
};
using AudioUnitGuard =
    std::unique_ptr<std::remove_pointer_t<AudioComponentInstance>,
                    AudioUnitDeleter>;

struct AudioFileDeleter {
  void operator()(ExtAudioFileRef a_file) const { ExtAudioFileDispose(a_file); }
};
using AudioFileGuard =
    std::unique_ptr<std::remove_pointer_t<ExtAudioFileRef>, AudioFileDeleter>;
