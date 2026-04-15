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
    hold_time_l_ = a_now;
  }
  if (a_peak_r >= hold_r_)
  {
    hold_r_ = a_peak_r;
    hold_time_r_ = a_now;
  }
  else if (std::chrono::duration<double>(a_now - hold_time_r_).count() > 1.5)
  {
    hold_r_ = a_peak_r;
    hold_time_r_ = a_now;
  }

  // Convert to dB
  db_l_ = 20.0f * std::log10(std::max(a_peak_l, 1e-10f));
  db_r_ = 20.0f * std::log10(std::max(a_peak_r, 1e-10f));
  hold_db_l_ = 20.0f * std::log10(std::max(hold_l_, 1e-10f));
  hold_db_r_ = 20.0f * std::log10(std::max(hold_r_, 1e-10f));
}

static std::string renderMeter(int a_width, float a_db, float a_hold_db)
{
  // Left block elements for fractional fill (1/8 to 7/8).
  // Index 0 unused; indices 1..7 map to U+258F..U+2589.
  static constexpr const char* k_frac_blocks[] = {
      "",              // 0/8 -- no fractional cell
      "\xe2\x96\x8f",  // 1/8  U+258F left one-eighth block
      "\xe2\x96\x8e",  // 2/8  U+258E left one-quarter block
      "\xe2\x96\x8d",  // 3/8  U+258D left three-eighths block
      "\xe2\x96\x8c",  // 4/8  U+258C left half block
      "\xe2\x96\x8b",  // 5/8  U+258B left five-eighths block
      "\xe2\x96\x8a",  // 6/8  U+258A left three-quarters block
      "\xe2\x96\x89",  // 7/8  U+2589 left seven-eighths block
  };

  float fill_pos =
      std::clamp((a_db + 60.0f) / 60.0f * static_cast<float>(a_width), 0.0f,
                 static_cast<float>(a_width));
  int full_blocks = static_cast<int>(fill_pos);
  int frac_index =
      static_cast<int>((fill_pos - static_cast<float>(full_blocks)) * 8.0f);

  int hold_indicator_pos =
      std::clamp(static_cast<int>((a_hold_db + 60.0f) / 60.0f *
                                  static_cast<float>(a_width)),
                 0, a_width - 1);

  std::string meter_str;
  meter_str.reserve(static_cast<size_t>(a_width) * 3);
  for (int i = 0; i < a_width; i++)
  {
    if (i < full_blocks)
      meter_str += "\xe2\x96\x88";  // full block
    else if (i == full_blocks && frac_index > 0)
      meter_str += k_frac_blocks[frac_index];
    else if (i == hold_indicator_pos && hold_indicator_pos >= full_blocks)
      meter_str += "\xe2\x94\x82";  // vertical line
    else
      meter_str += "\xe2\x96\x91";  // light shade
  }
  return meter_str;
}

std::string formatMeterLine(const MeterState& a_state)
{
  std::string line = "\r\033[K";

  if (a_state.channels == 1)
  {
    line += std::format(" [{}] {:+6.1f}dB",
                        renderMeter(27, a_state.peak_db_l, a_state.hold_db_l),
                        a_state.hold_db_l);
  }
  else
  {
    float hold_max = std::max(a_state.hold_db_l, a_state.hold_db_r);
    line += std::format(" L[{}] R[{}] {:+6.1f}dB",
                        renderMeter(25, a_state.peak_db_l, a_state.hold_db_l),
                        renderMeter(25, a_state.peak_db_r, a_state.hold_db_r),
                        hold_max);
  }

  int elapsed_hr = a_state.elapsed_sec / 3600;
  int elapsed_min = (a_state.elapsed_sec % 3600) / 60;
  int elapsed_sec = a_state.elapsed_sec % 60;

  if (a_state.total_sec > 0)
  {
    int total_hr = a_state.total_sec / 3600;
    int total_min = (a_state.total_sec % 3600) / 60;
    line += std::format("  {:02d}:{:02d}:{:02d}/{:02d}:{:02d}:00", elapsed_hr,
                        elapsed_min, elapsed_sec, total_hr, total_min);
  }
  else
  {
    line += std::format("  {:02d}:{:02d}:{:02d} (no limit)", elapsed_hr,
                        elapsed_min, elapsed_sec);
  }

  if (a_state.free_bytes)
  {
    line += std::format("  {:.0f}GB",
                        static_cast<double>(*a_state.free_bytes) / 1e9);
  }

  if (a_state.buffer_pct)
  {
    line += std::format("  buf:{:.1f}%", *a_state.buffer_pct);
    if (a_state.overruns && *a_state.overruns > 0)
      line += std::format(" ovr:{}", *a_state.overruns);
  }

  if (a_state.clipping)
    line += " CLIP";

  return line;
}

void runMeterLoop(std::function<DisplaySample()> a_poll,
                  const DisplayLoopConfig& a_config,
                  const std::atomic<bool>& a_stop_flag)
{
  auto start_time = std::chrono::steady_clock::now();
  PeakProcessor peaks;
  int clip_hold = 0;

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
    if (sample.error)
      break;

    if (!a_config.quiet)
    {
      peaks.updateLevels(sample.peak_l, sample.peak_r, now);

      MeterState state {};
      state.peak_db_l = peaks.dbL();
      state.peak_db_r = peaks.dbR();
      state.hold_db_l = peaks.holdDbL();
      state.hold_db_r = peaks.holdDbR();
      bool clipped_now =
          sample.peak_l >= 1.0f ||
          (a_config.record_channels == 2 && sample.peak_r >= 1.0f);
      if (clipped_now)
        clip_hold = 5;
      state.clipping = clip_hold > 0;
      if (clip_hold > 0)
        --clip_hold;
      state.elapsed_sec = static_cast<int>(elapsed);
      state.total_sec =
          a_config.max_duration_min > 0 ? a_config.max_duration_min * 60 : 0;
      state.free_bytes = sample.free_bytes;
      state.channels = a_config.record_channels;
      state.buffer_pct = sample.buffer_pct;
      state.overruns = sample.overruns;

      printErr(formatMeterLine(state));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(k_poll_ms));
  }
}
