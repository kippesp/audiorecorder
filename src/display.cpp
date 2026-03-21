// display.cpp -- Meter bar rendering and display line formatting.

#include "display.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <thread>

void PeakProcessor::updateLevels(float a_peak_l, float a_peak_r,
                                 std::chrono::steady_clock::time_point a_now)
{
  // Peak hold (1.5 second hold time)
  if (a_peak_l >= hold_l_)
  {
    hold_l_ = a_peak_l;
    hold_time_l_ = a_now;
  }
  else if (std::chrono::duration<double>(a_now - hold_time_l_).count() > 1.5)
  {
    hold_l_ = a_peak_l;
  }
  if (a_peak_r >= hold_r_)
  {
    hold_r_ = a_peak_r;
    hold_time_r_ = a_now;
  }
  else if (std::chrono::duration<double>(a_now - hold_time_r_).count() > 1.5)
  {
    hold_r_ = a_peak_r;
  }

  // Convert to dB
  db_l_ = 20.0f * std::log10(std::max(a_peak_l, 1e-10f));
  db_r_ = 20.0f * std::log10(std::max(a_peak_r, 1e-10f));
  hold_db_l_ = 20.0f * std::log10(std::max(hold_l_, 1e-10f));
  hold_db_r_ = 20.0f * std::log10(std::max(hold_r_, 1e-10f));
}

static std::string renderMeter(int a_width, float a_db, float a_hold_db)
{
  int fill_width = std::clamp(
      static_cast<int>((a_db + 60.0f) / 60.0f * static_cast<float>(a_width)), 0,
      a_width);
  int hold_indicator_pos =
      std::clamp(static_cast<int>((a_hold_db + 60.0f) / 60.0f *
                                  static_cast<float>(a_width)),
                 0, a_width - 1);

  std::string meter_str;
  meter_str.reserve(static_cast<size_t>(a_width) * 3);
  for (int i = 0; i < a_width; i++)
  {
    if (i < fill_width)
      meter_str += "\xe2\x96\x88";  // full block
    else if (i == hold_indicator_pos && hold_indicator_pos >= fill_width)
      meter_str += "\xe2\x94\x82";  // vertical line
    else
      meter_str += "\xe2\x96\x91";  // light shade
  }
  return meter_str;
}

std::string formatMeterLine(const MeterState& a_state)
{
  std::string line = "\r\033[K";

  if (a_state.channels_ == 1)
  {
    line += std::format(" [{}] {:+6.1f}dB",
                        renderMeter(27, a_state.peak_db_l_, a_state.hold_db_l_),
                        a_state.peak_db_l_);
  }
  else
  {
    float db_max = std::max(a_state.peak_db_l_, a_state.peak_db_r_);
    line += std::format(" L[{}] R[{}] {:+6.1f}dB",
                        renderMeter(25, a_state.peak_db_l_, a_state.hold_db_l_),
                        renderMeter(25, a_state.peak_db_r_, a_state.hold_db_r_),
                        db_max);
  }

  if (a_state.clipping_)
    line += " CLIP";

  int elapsed_hr = a_state.elapsed_sec_ / 3600;
  int elapsed_min = (a_state.elapsed_sec_ % 3600) / 60;
  int elapsed_sec = a_state.elapsed_sec_ % 60;

  if (a_state.total_sec_ > 0)
  {
    int total_hr = a_state.total_sec_ / 3600;
    int total_min = (a_state.total_sec_ % 3600) / 60;
    line += std::format("  {:02d}:{:02d}:{:02d}/{:02d}:{:02d}:00", elapsed_hr,
                        elapsed_min, elapsed_sec, total_hr, total_min);
  }
  else
  {
    line += std::format("  {:02d}:{:02d}:{:02d} (no limit)", elapsed_hr,
                        elapsed_min, elapsed_sec);
  }

  if (a_state.free_bytes_)
  {
    line += std::format("  {:.0f}GB",
                        static_cast<double>(*a_state.free_bytes_) / 1e9);
  }

  if (a_state.buffer_pct_)
  {
    line += std::format("  buf:{:.1f}%", *a_state.buffer_pct_);
    if (a_state.overruns_ && *a_state.overruns_ > 0)
      line += std::format(" ovr:{}", *a_state.overruns_);
  }

  return line;
}

void runMeterLoop(std::function<DisplaySample()> a_poll,
                  const DisplayLoopConfig& a_config,
                  const std::atomic<bool>& a_stop_flag)
{
  auto start_time = std::chrono::steady_clock::now();
  PeakProcessor peaks;

  constexpr int k_poll_ms = 100;

  while (!a_stop_flag.load(std::memory_order_relaxed))
  {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start_time).count();

    if (a_config.max_duration_min > 0 &&
        elapsed >= static_cast<double>(a_config.max_duration_min) * 60.0)
    {
      break;
    }

    auto sample = a_poll();
    if (sample.error_)
      break;

    if (!a_config.quiet)
    {
      peaks.updateLevels(sample.peak_l_, sample.peak_r_, now);

      MeterState state {};
      state.peak_db_l_ = peaks.db_l_;
      state.peak_db_r_ = peaks.db_r_;
      state.hold_db_l_ = peaks.hold_db_l_;
      state.hold_db_r_ = peaks.hold_db_r_;
      state.clipping_ =
          sample.peak_l_ >= 1.0f ||
          (a_config.record_channels == 2 && sample.peak_r_ >= 1.0f);
      state.elapsed_sec_ = static_cast<int>(elapsed);
      state.total_sec_ =
          a_config.max_duration_min > 0 ? a_config.max_duration_min * 60 : 0;
      state.free_bytes_ = sample.free_bytes_;
      state.channels_ = a_config.record_channels;
      state.buffer_pct_ = sample.buffer_pct_;
      state.overruns_ = sample.overruns_;

      printErr(formatMeterLine(state));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(k_poll_ms));
  }
}
