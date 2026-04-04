// writer.h -- Writer thread (ring drain, file write).

#pragma once

#include "recording_context.h"

#include <stop_token>

// Writer thread entry point (for std::jthread).
// On write failure, sets ctx.write_error_ and returns.
void writerFn(std::stop_token a_stoken, RecordingContext* a_ctx,
              ExtAudioFileRef a_audio_file);
