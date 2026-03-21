// ring_buffer.h -- Lock-free SPSC ring buffer for float samples.

#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <span>
#include <vector>

// Lock-free single-producer, single-consumer (SPSC) ring buffer.
//
// No standard library equivalent exists; this is a custom implementation of
// the classic SPSC queue pattern using monotonically increasing position
// counters and a power-of-two capacity for branchless index wrapping via
// bitmask. Thread safety relies on acquire/release ordering between the
// producer's write_pos_ store and the consumer's write_pos_ load (and
// symmetrically for read_pos_).
//
// Based on Lamport's concurrent read/write algorithm:
//   https://lamport.azurewebsites.net/pubs/spec.pdf
// Structurally equivalent to boost::lockfree::spsc_queue.
// For background on the acquire/release ordering used here:
//   https://preshing.com/20120612/an-introduction-to-lock-free-programming/
//
// Producer: inputCallback (CoreAudio real-time thread) via push().
// Consumer: writerFn (writer thread) via popAll(),
//           monitorCallback (CoreAudio output thread) via pop().
class RingBuffer {
public:
  RingBuffer() = default;

  // (Re)initialize with at least a_min_capacity elements.
  // Must not be called while push/popAll are active.
  void init(size_t a_min_capacity)
  {
    buf_.assign(std::bit_ceil(a_min_capacity), 0.0f);
    mask_ = buf_.size() - 1;
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
  }

  // Push samples (producer thread). Returns true on success, false on overrun.
  bool push(std::span<const float> a_data)
  {
    size_t count = a_data.size();
    size_t write = write_pos_.load(std::memory_order_relaxed);
    size_t read = read_pos_.load(std::memory_order_acquire);
    size_t capacity = buf_.size();

    if (capacity - (write - read) < count)
      return false;

    size_t offset = write & mask_;
    size_t head = std::min(count, capacity - offset);
    std::memcpy(&buf_[offset], a_data.data(), head * sizeof(float));
    if (head < count)
    {
      std::memcpy(&buf_[0], a_data.data() + head,
                  (count - head) * sizeof(float));
    }
    write_pos_.store(write + count, std::memory_order_release);
    return true;
  }

  // Pop up to a_dest.size() samples (consumer thread). Returns count written.
  size_t pop(std::span<float> a_dest)
  {
    size_t write = write_pos_.load(std::memory_order_acquire);
    size_t read = read_pos_.load(std::memory_order_relaxed);
    size_t available = write - read;
    size_t count = std::min(available, a_dest.size());
    if (count == 0)
      return 0;

    size_t capacity = buf_.size();
    size_t offset = read & mask_;
    size_t head = std::min(count, capacity - offset);
    std::memcpy(a_dest.data(), &buf_[offset], head * sizeof(float));
    if (head < count)
    {
      std::memcpy(a_dest.data() + head, &buf_[0],
                  (count - head) * sizeof(float));
    }
    read_pos_.store(read + count, std::memory_order_release);
    return count;
  }

  // Pop all available samples (consumer thread). Returns count written.
  // a_dest must have at least capacity() elements.
  size_t popAll(std::span<float> a_dest)
  {
    size_t write = write_pos_.load(std::memory_order_acquire);
    size_t read = read_pos_.load(std::memory_order_relaxed);
    size_t available = write - read;
    if (available == 0)
      return 0;

    size_t capacity = buf_.size();
    size_t offset = read & mask_;
    size_t head = std::min(available, capacity - offset);
    std::memcpy(a_dest.data(), &buf_[offset], head * sizeof(float));
    if (head < available)
    {
      std::memcpy(a_dest.data() + head, &buf_[0],
                  (available - head) * sizeof(float));
    }
    read_pos_.store(read + available, std::memory_order_release);
    return available;
  }

  size_t capacity() const { return buf_.size(); }

  // Approximate; the two relaxed loads are not mutually ordered.
  // Suitable for diagnostics, not for flow control.
  size_t used() const
  {
    size_t write = write_pos_.load(std::memory_order_relaxed);
    size_t read = read_pos_.load(std::memory_order_relaxed);
    return write - read;
  }

private:
  std::vector<float> buf_;
  size_t mask_ = 0;
  std::atomic<size_t> write_pos_ {0};
  std::atomic<size_t> read_pos_ {0};
};
