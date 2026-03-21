// display.h -- Meter state and display formatting.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

struct PeakProcessor {
  float hold_l_ = 0.0f;
  float hold_r_ = 0.0f;
  std::chrono::steady_clock::time_point hold_time_l_ {};
  std::chrono::steady_clock::time_point hold_time_r_ {};

  // Process raw linear peak values. Updates hold state, computes dB.
  void updateLevels(float a_peak_l, float a_peak_r,
                    std::chrono::steady_clock::time_point a_now);

  float db_l_ = -200.0f, db_r_ = -200.0f;
  float hold_db_l_ = -200.0f, hold_db_r_ = -200.0f;
};

struct MeterState {
  float peak_db_l_ = 0.0f, peak_db_r_ = 0.0f;
  float hold_db_l_ = 0.0f, hold_db_r_ = 0.0f;
  bool clipping_ = false;
  int elapsed_sec_ = 0;
  int total_sec_ = 0;  // 0 if unlimited
  std::optional<uint64_t> free_bytes_;
  uint32_t channels_ = 0;  // 1 or 2
  std::optional<double> buffer_pct_;
  std::optional<int64_t> overruns_;
};

std::string formatMeterLine(const MeterState& a_state);

// Snapshot of recorder state for display polling.
struct DisplaySample {
  float peak_l_ = 0.0f;
  float peak_r_ = 0.0f;
  std::optional<double> buffer_pct_;
  std::optional<int64_t> overruns_;
  std::optional<uint64_t> free_bytes_;
  bool error_ = false;
};

// Configuration for the display loop.
struct DisplayLoopConfig {
  bool quiet = false;
  int max_duration_min = 0;
  uint32_t record_channels = 0;
};

// Display loop -- polls via callback, renders meter, returns when stop_flag
// is set or max_duration_min is reached.
void runMeterLoop(std::function<DisplaySample()> a_poll,
                  const DisplayLoopConfig& a_config,
                  const std::atomic<bool>& a_stop_flag);
