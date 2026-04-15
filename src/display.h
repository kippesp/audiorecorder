// display.h -- Meter state and display formatting.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

class PeakProcessor {
public:
  // Process raw linear peak values. Updates hold state, computes dB.
  void updateLevels(float a_peak_l, float a_peak_r,
                    std::chrono::steady_clock::time_point a_now);

  float dbL() const { return db_l_; }
  float dbR() const { return db_r_; }
  float holdDbL() const { return hold_db_l_; }
  float holdDbR() const { return hold_db_r_; }

private:
  float hold_l_ = 0.0f;
  float hold_r_ = 0.0f;
  std::chrono::steady_clock::time_point hold_time_l_ {};
  std::chrono::steady_clock::time_point hold_time_r_ {};
  float db_l_ = -200.0f;
  float db_r_ = -200.0f;
  float hold_db_l_ = -200.0f;
  float hold_db_r_ = -200.0f;
};

struct MeterState {
  float peak_db_l = 0.0f;
  float peak_db_r = 0.0f;
  float hold_db_l = 0.0f;
  float hold_db_r = 0.0f;
  bool clipping = false;
  int elapsed_sec = 0;
  int total_sec = 0;  // 0 if unlimited
  std::optional<uint64_t> free_bytes;
  uint32_t channels = 0;  // 1 or 2
  std::optional<double> buffer_pct;
  std::optional<int64_t> overruns;
};

std::string formatMeterLine(const MeterState& a_state);

// Snapshot of recorder state for display polling.
struct DisplaySample {
  float peak_l = 0.0f;
  float peak_r = 0.0f;
  std::optional<double> buffer_pct;
  std::optional<int64_t> overruns;
  std::optional<uint64_t> free_bytes;
  bool error = false;
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
