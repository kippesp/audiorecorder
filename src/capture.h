// capture.h -- CoreAudio capture unit setup.

#pragma once

#include "recording_context.h"

#include <CoreAudio/CoreAudio.h>

#include <expected>

// Setup AUHAL capture unit -- returns void on success, OSStatus on failure.
// Allocates ring buffer (~60s) and render buffer.
// On success, ctx.audio_unit is set (caller should wrap in AudioUnitGuard).
std::expected<void, OSStatus> setupCaptureUnit(RecordingContext& a_ctx,
                                               AudioDeviceID a_device_id,
                                               Float64 a_sample_rate,
                                               uint32_t a_record_channels);
