// session.cpp -- Session lifecycle: setup, run, teardown.

#include "session.h"
#include "capture.h"
#include "display.h"
#include "file_util.h"
#include "monitor.h"
#include "output_file.h"
#include "recording_context.h"
#include "util.h"
#include "writer.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

#include <cmath>
#include <csignal>
#include <cstdio>
#include <format>
#include <functional>
#include <sys/stat.h>
#include <thread>

static std::atomic<bool> s_stop_requested {false};

static void signalHandler(int)
{
  s_stop_requested.store(true, std::memory_order_relaxed);
}

struct SleepGuard {
  IOPMAssertionID id_ = 0;

  SleepGuard()
  {
    IOReturn ret = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
        CFSTR("Audio recording in progress"), &id_);
    if (ret != kIOReturnSuccess)
      printErr("Warning: could not prevent idle sleep.\n");
  }
  ~SleepGuard()
  {
    if (id_)
      IOPMAssertionRelease(id_);
  }

  SleepGuard(const SleepGuard&) = delete;
  SleepGuard& operator=(const SleepGuard&) = delete;
};

struct Session {
  AudioUnitGuard unit_guard_;  // RAII owner of the capture AudioUnit
  AudioFileGuard file_guard_;  // RAII owner of the output ExtAudioFile
  AudioDevice device_;
  std::string output_path_;
  std::string output_dir_;
  uint32_t record_channels_ = 0;
  const char* ch_label_ = "";
  AudioUnitGuard monitor_guard_;

  // Disk space polling state (throttled to ~5s at 100ms poll)
  int disk_check_counter_ = 0;
  std::optional<uint64_t> cached_free_bytes_;
};

struct SessionState {
  Session session;
  std::unique_ptr<RecordingContext> ctx;
};

static std::optional<SessionState> setupSession(
    const Args& a_args, const std::vector<AudioDevice>& a_devices)
{
  auto device_result =
      resolveSelectedDevice(a_args.device_selector_, a_devices);
  if (!device_result)
  {
    printErr(device_result.error());
    return std::nullopt;
  }
  auto device = std::move(*device_result);

  uint32_t record_channels = std::min(device.input_channels_, 2u);
  auto ch_label = record_channels == 1 ? "mono" : "stereo";

  std::string output_path;
  std::string output_dir;
  AudioFileGuard file_guard;

  if (!a_args.test_)
  {
    auto path_result = resolveOutputPath(a_args.output_path_);
    if (!path_result)
    {
      printErr(path_result.error());
      return std::nullopt;
    }
    output_path = std::move(*path_result);

    output_dir = directoryOf(output_path);
    uint64_t bytes_per_sec =
        static_cast<uint64_t>(device.sample_rate_) * record_channels * 3;

    auto disk_result = checkDiskSpace(output_dir, bytes_per_sec,
                                      a_args.max_duration_min_, ch_label);
    if (!disk_result)
    {
      printErr(disk_result.error());
      return std::nullopt;
    }

    auto file_result =
        createOutputFile(output_path, device.sample_rate_, record_channels);
    if (!file_result)
    {
      printErr("Error: failed to create output file ({}).\n",
               file_result.error());
      return std::nullopt;
    }
    file_guard = std::move(*file_result);
  }

  auto ctx = std::make_unique<RecordingContext>();

  auto unit_result =
      setupCaptureUnit(*ctx, device.id_, device.sample_rate_, record_channels);
  if (!unit_result)
  {
    printErr("Error: failed to set up audio unit ({}).\n", unit_result.error());
    return std::nullopt;
  }

  AudioUnitGuard monitor_guard;
  if (a_args.monitor_)
  {
    auto monitor_result =
        setupMonitorUnit(*ctx, device.sample_rate_, record_channels);
    if (!monitor_result)
    {
      printErr("Error: failed to set up monitor output ({}).\n",
               monitor_result.error());
      return std::nullopt;
    }
    monitor_guard = AudioUnitGuard(*monitor_result);
  }

  Session session {
      AudioUnitGuard(ctx->audio_unit_),
      std::move(file_guard),
      std::move(device),
      std::move(output_path),
      std::move(output_dir),
      record_channels,
      ch_label,
      std::move(monitor_guard),
      0,
      std::nullopt,
  };

  // Initial disk space query
  if (!session.output_dir_.empty())
  {
    uint64_t bytes = getFreeBytes(session.output_dir_);
    if (bytes != UINT64_MAX)
      session.cached_free_bytes_ = bytes;
  }

  return SessionState {
      .session = std::move(session),
      .ctx = std::move(ctx),
  };
}

static int runRecordingLoop(Session& a_session, RecordingContext& a_ctx,
                            const Args& a_args)
{
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  SleepGuard sleep_guard;

  if (a_args.test_)
  {
    printErr(
        "\n"
        " ##### ##### ##### #####   #   # ##### ####  #####\n"
        "   #   #     #       #     ## ## #   # #   # #\n"
        "   #   ####   ###    #     # # # #   # #   # ####\n"
        "   #   #         #   #     # # # #   # #   # #\n"
        "   #   ##### #####   #     #   # ##### ####  #####\n"
        "\n"
        "  No file output.\n");
  }
  else
    printErr("Recording to {}\n", a_session.output_path_);
  printErr("  Device: {} ({:.0f} Hz, {}, 24-bit)\n", a_session.device_.name_,
           a_session.device_.sample_rate_, a_session.ch_label_);
  if (a_args.max_duration_min_ > 0)
    printErr("  Max duration: {:02d}:{:02d}:00\n",
             a_args.max_duration_min_ / 60, a_args.max_duration_min_ % 60);
  if (a_session.monitor_guard_)
    printErr("  Monitor: on\n");
  printErr("Press Ctrl-C to stop.\n\n");

  std::jthread writer_thread(writerFn, &a_ctx, a_session.file_guard_.get());

  OSStatus start_status = AudioOutputUnitStart(a_ctx.audio_unit_);
  if (start_status != noErr)
  {
    printErr("Error: failed to start audio unit ({}).\n",
             static_cast<int>(start_status));
    return 1;
  }

  if (a_session.monitor_guard_)
  {
    OSStatus mon_status = AudioOutputUnitStart(a_session.monitor_guard_.get());
    if (mon_status != noErr)
      printErr("Warning: could not start monitor output.\n");
  }

  constexpr int k_disk_check_count = 50;  // ~5s at 100ms poll

  // Assembles a DisplaySample from RecordingContext atomics for the meter
  // loop.  Throttles disk space queries to one every ~5s.
  auto pollDisplay = [&a_ctx, &a_session,
                      verbose = a_args.verbose_]() -> DisplaySample {
    DisplaySample s;
    s.peak_l_ = a_ctx.peak_l_.exchange(0.0f, std::memory_order_relaxed);
    s.peak_r_ = a_ctx.peak_r_.exchange(0.0f, std::memory_order_relaxed);
    s.error_ = a_ctx.write_error_.load(std::memory_order_relaxed);
    if (verbose)
    {
      size_t used = a_ctx.ring_.used();
      s.buffer_pct_ = 100.0 * static_cast<double>(used) /
                      static_cast<double>(a_ctx.ring_.capacity());
      s.overruns_ = static_cast<int64_t>(
          a_ctx.overrun_frames_.load(std::memory_order_relaxed));

      size_t prev = a_ctx.ring_peak_used_.load(std::memory_order_relaxed);
      while (prev < used && !a_ctx.ring_peak_used_.compare_exchange_weak(
                                prev, used, std::memory_order_relaxed))
      {
      }
    }
    if (!a_session.output_dir_.empty())
    {
      if (++a_session.disk_check_counter_ >= k_disk_check_count)
      {
        uint64_t bytes = getFreeBytes(a_session.output_dir_);
        if (bytes != UINT64_MAX)
          a_session.cached_free_bytes_ = bytes;
        a_session.disk_check_counter_ = 0;
      }
      s.free_bytes_ = a_session.cached_free_bytes_;
    }
    return s;
  };

  DisplayLoopConfig loop_config {
      .quiet = a_args.quiet_,
      .max_duration_min = a_args.max_duration_min_,
      .record_channels = a_session.record_channels_,
  };

  // Blocks until Ctrl-C, max duration, or write error.
  runMeterLoop(pollDisplay, loop_config, s_stop_requested);

  if (a_session.monitor_guard_)
    AudioOutputUnitStop(a_session.monitor_guard_.get());
  AudioOutputUnitStop(a_ctx.audio_unit_);
  writer_thread.request_stop();
  a_ctx.writer_sem_.release();

  return 0;
}

static void printSessionSummary(const Session& a_session,
                                const RecordingContext& a_ctx,
                                const Args& a_args)
{
  if (!a_args.quiet_)
    printErr("\r\033[K");

  uint64_t frames = a_ctx.frames_written_.load(std::memory_order_relaxed);
  uint64_t overruns = a_ctx.overrun_frames_.load(std::memory_order_relaxed);

  if (a_args.test_)
  {
    double duration =
        static_cast<double>(frames) / a_session.device_.sample_rate_;
    int total_sec = static_cast<int>(duration);
    printErr("Test complete: {:02d}:{:02d}:{:02d}\n", total_sec / 3600,
             (total_sec % 3600) / 60, total_sec % 60);
    if (overruns > 0)
      printErr("Warning: {} frames dropped due to buffer overruns.\n",
               overruns);
  }
  else
  {
    double duration =
        static_cast<double>(frames) / a_session.device_.sample_rate_;
    int duration_hr = static_cast<int>(duration / 3600);
    int duration_min = static_cast<int>(std::fmod(duration, 3600) / 60);
    int duration_sec = static_cast<int>(std::fmod(duration, 60));

    struct stat file_stat {};
    double file_size_gb = 0;
    if (stat(a_session.output_path_.c_str(), &file_stat) == 0)
      file_size_gb = static_cast<double>(file_stat.st_size) / 1e9;

    printErr("Recorded {:02d}:{:02d}:{:02d} -> {} ({:.1f} GB)\n", duration_hr,
             duration_min, duration_sec, a_session.output_path_, file_size_gb);

    if (overruns > 0)
      printErr("Warning: {} frames dropped due to buffer overruns.\n",
               overruns);
  }

  float sp_l = a_ctx.session_peak_l_.load(std::memory_order_relaxed);
  float sp_r = a_ctx.session_peak_r_.load(std::memory_order_relaxed);
  float sp_max =
      (a_session.record_channels_ == 2) ? std::max(sp_l, sp_r) : sp_l;
  float peak_db = 20.0f * std::log10(std::max(sp_max, 1e-10f));
  if (sp_max >= 1.0f)
    printErr("Peak level: {:+.1f} dB (CLIPPED)\n", peak_db);
  else
    printErr("Peak level: {:+.1f} dB\n", peak_db);

  if (a_args.verbose_)
  {
    double peak_pct = 100.0 *
                      static_cast<double>(a_ctx.ring_peak_used_.load(
                          std::memory_order_relaxed)) /
                      static_cast<double>(a_ctx.ring_.capacity());
    printErr("Buffer: peak {:.1f}% of 60s ring ({} overruns)\n", peak_pct,
             overruns);
  }
}

int runSession(const Args& a_args, const std::vector<AudioDevice>& a_devices)
{
  auto state = setupSession(a_args, a_devices);
  if (!state)
    return 1;

  int result = runRecordingLoop(state->session, *state->ctx, a_args);
  if (result != 0)
    return result;

  printSessionSummary(state->session, *state->ctx, a_args);

  return state->ctx->write_error_.load(std::memory_order_relaxed) ? 1 : 0;
}
