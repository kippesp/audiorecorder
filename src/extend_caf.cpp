// extend_caf.cpp -- Extend mode: pad CAF recording via sparse hole.
//
// The read side uses AudioToolbox's AudioFile API to parse the source
// file; the extension itself is a POSIX pwrite + ftruncate on a copy
// of the input at a fresh fd, keeping the AudioFile handle off the
// write path.

#include "extend_caf.h"
#include "file_util.h"
#include "util.h"

#include <AudioToolbox/AudioFile.h>
#include <CoreFoundation/CoreFoundation.h>
#include <fcntl.h>
#include <libkern/OSByteOrder.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <utility>

namespace {

constexpr int kCafChunkTypeSize = 4;
constexpr int kCafChunkHeaderSize = 12;
constexpr int kCafDataEditCountSize = 4;
constexpr int kSecondsPerMinute = 60;

struct CafFormat {
  double sample_rate;
  uint32_t bytes_per_frame;
  int64_t frame_count;
  int64_t data_header_offset;
};

struct FdCloser {
  int fd;
  ~FdCloser()
  {
    if (fd >= 0)
      ::close(fd);
  }
};

struct AudioFileCloser {
  AudioFileID af;
  ~AudioFileCloser()
  {
    if (af)
      AudioFileClose(af);
  }
};

std::expected<CafFormat, std::string> readCafFormat(const std::string& a_path)
{
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      nullptr, reinterpret_cast<const UInt8*>(a_path.c_str()),
      static_cast<CFIndex>(a_path.size()), false);
  if (!url)
    return std::unexpected(
        std::format("Error: could not form URL for '{}'.\n", a_path));

  AudioFileID af = nullptr;
  OSStatus st = AudioFileOpenURL(url, kAudioFileReadPermission, 0, &af);
  CFRelease(url);
  if (st)
    return std::unexpected(
        std::format("Error: could not open '{}' as an audio file ({}).\n",
                    a_path, formatOSStatus(st)));
  AudioFileCloser af_guard {af};

  UInt32 file_format = 0;
  UInt32 size = sizeof(file_format);
  st = AudioFileGetProperty(af, kAudioFilePropertyFileFormat, &size,
                            &file_format);
  if (st)
    return std::unexpected(
        std::format("Error: could not read file format of '{}' ({}).\n", a_path,
                    formatOSStatus(st)));
  if (file_format != kAudioFileCAFType)
    return std::unexpected(std::format("Error: '{}' is not a CAF file ({}).\n",
                                       a_path, formatFourCC(file_format)));

  AudioStreamBasicDescription asbd {};
  size = sizeof(asbd);
  st = AudioFileGetProperty(af, kAudioFilePropertyDataFormat, &size, &asbd);
  if (st)
    return std::unexpected(
        std::format("Error: could not read data format of '{}' ({}).\n", a_path,
                    formatOSStatus(st)));

  if (asbd.mFormatID != kAudioFormatLinearPCM)
    return std::unexpected(std::format("Error: '{}' is not LPCM.\n", a_path));
  if (asbd.mFramesPerPacket != 1)
    return std::unexpected(
        std::format("Error: '{}' has {} frames per packet (expected 1).\n",
                    a_path, asbd.mFramesPerPacket));
  if (!(asbd.mSampleRate > 0))
    return std::unexpected(
        std::format("Error: '{}' has non-positive sample rate.\n", a_path));
  if (asbd.mBytesPerPacket == 0)
    return std::unexpected(
        std::format("Error: '{}' has zero bytes per packet.\n", a_path));

  UInt64 audio_byte_count = 0;
  size = sizeof(audio_byte_count);
  st = AudioFileGetProperty(af, kAudioFilePropertyAudioDataByteCount, &size,
                            &audio_byte_count);
  if (st)
    return std::unexpected(
        std::format("Error: could not read audio byte count of '{}' ({}).\n",
                    a_path, formatOSStatus(st)));

  SInt64 data_offset = 0;
  size = sizeof(data_offset);
  st = AudioFileGetProperty(af, kAudioFilePropertyDataOffset, &size,
                            &data_offset);
  if (st)
    return std::unexpected(
        std::format("Error: could not read data offset of '{}' ({}).\n", a_path,
                    formatOSStatus(st)));

  int64_t bytes_per_frame = static_cast<int64_t>(asbd.mBytesPerPacket);
  int64_t frame_count =
      static_cast<int64_t>(audio_byte_count) / bytes_per_frame;
  if (frame_count <= 0)
    return std::unexpected(
        std::format("Error: '{}' has no audio frames.\n", a_path));

  // kAudioFilePropertyDataOffset points at the first audio byte, past the
  // 'data' chunk's 4-byte mEditCount prefix. Back up past the chunk header
  // and mEditCount to land on the chunk header start.
  int64_t data_header_offset = static_cast<int64_t>(data_offset) -
                               (kCafChunkHeaderSize + kCafDataEditCountSize);

  // Trailing-data check: the 'data' chunk must be the terminal chunk and its
  // declared audio bytes must reach exactly end-of-file. patchAndExtendTmp's
  // ftruncate would otherwise overwrite any chunk after 'data'.
  std::error_code size_ec;
  std::uintmax_t file_size = std::filesystem::file_size(a_path, size_ec);
  if (size_ec)
    return std::unexpected(std::format("Error: could not stat '{}' ({}).\n",
                                       a_path, size_ec.message()));
  int64_t audio_bytes_end = static_cast<int64_t>(data_offset) +
                            static_cast<int64_t>(audio_byte_count);
  if (audio_bytes_end != static_cast<int64_t>(file_size))
    return std::unexpected(
        std::format("Error: '{}' has data after the audio chunk "
                    "(unsupported layout).\n",
                    a_path));

  return CafFormat {
      .sample_rate = asbd.mSampleRate,
      .bytes_per_frame = static_cast<uint32_t>(bytes_per_frame),
      .frame_count = frame_count,
      .data_header_offset = data_header_offset,
  };
}

std::expected<void, std::string> patchAndExtendTmp(
    const std::string& a_tmp_path, const CafFormat& a_fmt,
    int64_t a_target_frames)
{
  int fd = open(a_tmp_path.c_str(), O_RDWR);
  if (fd < 0)
    return std::unexpected(
        std::format("Error: could not open '{}' for write (errno {}: {}).\n",
                    a_tmp_path, errno, std::strerror(errno)));
  FdCloser guard {fd};

  int64_t new_chunk_size =
      kCafDataEditCountSize +
      a_target_frames * static_cast<int64_t>(a_fmt.bytes_per_frame);

  // CAF chunk header: [type:4][size:8][payload]; size is big-endian.
  off_t size_field_offset =
      static_cast<off_t>(a_fmt.data_header_offset + kCafChunkTypeSize);
  uint64_t size_be =
      OSSwapHostToBigInt64(static_cast<uint64_t>(new_chunk_size));
  ssize_t written = pwrite(fd, &size_be, sizeof(size_be), size_field_offset);
  if (written != static_cast<ssize_t>(sizeof(size_be)))
    return std::unexpected(std::format(
        "Error: could not patch data chunk size in '{}' (errno {}: {}).\n",
        a_tmp_path, errno, std::strerror(errno)));

  off_t new_file_len = static_cast<off_t>(a_fmt.data_header_offset +
                                          kCafChunkHeaderSize + new_chunk_size);
  if (ftruncate(fd, new_file_len) != 0)
    return std::unexpected(std::format(
        "Error: could not extend file length of '{}' (errno {}: {}).\n",
        a_tmp_path, errno, std::strerror(errno)));

  // Durability before rename: a crash between here and rename otherwise
  // surfaces a file with stale header contents under the final name.
  return fullFsync(fd, a_tmp_path);
}

}  // namespace

std::expected<ExtendResult, std::string> extendCafFile(const ExtendArgs& a_args)
{
  auto resolved_input_r = expandHome(a_args.extend_caf_file);
  if (!resolved_input_r)
    return std::unexpected(std::move(resolved_input_r.error()));
  std::string resolved_input = std::move(*resolved_input_r);

  auto fmt_r = readCafFormat(resolved_input);
  if (!fmt_r)
    return std::unexpected(std::move(fmt_r.error()));
  const CafFormat& fmt = *fmt_r;

  double target_frames_exact = static_cast<double>(a_args.pad_to_min) *
                               kSecondsPerMinute * fmt.sample_rate;
  int64_t target_frames = static_cast<int64_t>(std::ceil(target_frames_exact));

  if (target_frames <= fmt.frame_count)
    return std::unexpected(std::format(
        "Error: --pad-to {} minutes does not exceed the existing duration "
        "of '{}'.\n",
        a_args.pad_to_min, resolved_input));

  auto resolved_output_r = resolveOutputPath(a_args.output_path);
  if (!resolved_output_r)
    return std::unexpected(std::move(resolved_output_r.error()));
  std::string resolved_output = std::move(*resolved_output_r);

  std::string tmp_path = resolved_output + ".tmp";

  std::error_code ec;
  std::filesystem::copy_file(resolved_input, tmp_path,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec)
    return std::unexpected(
        std::format("Error: could not copy '{}' to '{}' ({}).\n",
                    resolved_input, tmp_path, ec.message()));

  auto cleanupAndFail =
      [&](std::string a_msg) -> std::expected<ExtendResult, std::string> {
    std::error_code cleanup_ec;
    std::filesystem::remove(tmp_path, cleanup_ec);
    if (cleanup_ec)
      printErr("Warning: could not remove staging file '{}' ({}).\n", tmp_path,
               cleanup_ec.message());
    return std::unexpected(std::move(a_msg));
  };

  if (auto extend_result = patchAndExtendTmp(tmp_path, fmt, target_frames);
      !extend_result)
    return cleanupAndFail(std::move(extend_result.error()));

  std::filesystem::rename(tmp_path, resolved_output, ec);
  if (ec)
    return cleanupAndFail(
        std::format("Error: rename of '{}' onto '{}' failed ({}).\n", tmp_path,
                    resolved_output, ec.message()));

  // Directory durability: ensures the rename's new entry is committed after
  // the tmp file's contents. Failures here leave the file under its final
  // name but with an unflushed directory entry; tmp_path no longer exists.
  std::string parent_dir = directoryOf(resolved_output);
  int dir_fd = open(parent_dir.c_str(), O_RDONLY);
  if (dir_fd < 0)
    return std::unexpected(
        std::format("Error: could not open parent directory '{}' for fsync "
                    "(errno {}: {}).\n",
                    parent_dir, errno, std::strerror(errno)));
  FdCloser dir_guard {dir_fd};
  if (auto fsync_result = fullFsync(dir_fd, parent_dir); !fsync_result)
    return std::unexpected(std::move(fsync_result.error()));

  return ExtendResult {
      .resolved_input = std::move(resolved_input),
      .resolved_output = std::move(resolved_output),
      .pad_to_minutes = a_args.pad_to_min,
  };
}
