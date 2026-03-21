// monitor.h -- Monitor playback setup.

#pragma once

#include "recording_context.h"

#include <CoreAudio/CoreAudio.h>

#include <expected>

// Setup monitor output unit for input playback through default output device.
// On success, caller should wrap the returned instance in AudioUnitGuard.
std::expected<AudioComponentInstance, OSStatus> setupMonitorUnit(
    RecordingContext& a_ctx, Float64 a_sample_rate, uint32_t a_channels);
