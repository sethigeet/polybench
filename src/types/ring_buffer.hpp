#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>

inline constexpr size_t kCacheLineSize = 64;

namespace ring_buffer_detail {
inline size_t next_power_of_two(size_t value) {
  if (value < 2) return 2;
  return std::bit_ceil(value);
}
}  // namespace ring_buffer_detail

template <typename T, size_t N = 0>
class RingBuffer;

template <typename T, size_t N>
class RingBuffer {
  static_assert(N != 0, "Use RingBuffer<T, 0> for runtime-sized buffers");
  static_assert((N & (N - 1)) == 0, "N must be a power of 2");

 public:
  RingBuffer() = default;

  // Non-copyable, non-movable (due to atomic members)
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;
  RingBuffer(RingBuffer&&) = delete;
  RingBuffer& operator=(RingBuffer&&) = delete;

  /**
   * Try to push an element. Returns false if buffer is full.
   */
  bool push(const T& value) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & kMask;

    // Check if buffer is full
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[head] = value;
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /**
   * Try to push an element (move version). Returns false if buffer is full.
   */
  bool push(T&& value) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & kMask;

    // Check if buffer is full
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[head] = std::move(value);
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /**
   * Push an element, blocking (spinning) until space is available.
   * Returns true on success, false if was_full callback returns true (for backpressure logging).
   */
  template <typename WasFullCallback = std::nullptr_t>
  void push_wait(T&& value, WasFullCallback was_full = nullptr) {
    bool logged = false;
    while (!push(std::move(value))) {
      if constexpr (!std::is_null_pointer_v<WasFullCallback>) {
        if (!logged) {
          was_full();
          logged = true;
        }
      }
      std::this_thread::yield();
    }
  }

  /**
   * Try to pop an element. Returns std::nullopt if buffer is empty.
   */
  std::optional<T> pop() {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    auto value = std::move(buffer_[tail]);
    buffer_[tail].reset();
    tail_.store((tail + 1) & kMask, std::memory_order_release);
    return value;
  }

  /**
   * Pop an element, blocking (spinning) until data is available.
   */
  T pop_wait() {
    std::optional<T> value;
    while (!(value = pop())) {
      std::this_thread::yield();
    }
    return std::move(*value);
  }

  /**
   * Check if buffer is empty. Can be called from any thread.
   */
  [[nodiscard]] bool empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  /**
   * Get approximate size. Can be called from any thread but may be slightly stale.
   */
  [[nodiscard]] size_t size() const {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & kMask;
  }

  /**
   * Maximum capacity.
   */
  [[nodiscard]] static constexpr size_t capacity() { return N - 1; }

 private:
  static constexpr size_t kMask = N - 1;

  // Align to cache line to avoid false sharing
  alignas(kCacheLineSize) std::atomic<size_t> head_{0};
  alignas(kCacheLineSize) std::atomic<size_t> tail_{0};
  alignas(kCacheLineSize) std::array<std::optional<T>, N> buffer_{};
};

template <typename T>
class RingBuffer<T, 0> {
 public:
  explicit RingBuffer(size_t capacity)
      : capacity_(ring_buffer_detail::next_power_of_two(capacity + 1)),
        mask_(capacity_ - 1),
        buffer_(std::make_unique<std::optional<T>[]>(capacity_)) {}

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;
  RingBuffer(RingBuffer&&) = delete;
  RingBuffer& operator=(RingBuffer&&) = delete;

  bool push(const T& value) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & mask_;

    // Check if buffer is full
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[head] = value;
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /**
   * Try to push an element (move version). Returns false if buffer is full.
   */
  bool push(T&& value) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (head + 1) & mask_;
    if (next_head == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[head] = std::move(value);
    head_.store(next_head, std::memory_order_release);
    return true;
  }

  /**
   * Push an element, blocking (spinning) until space is available.
   * Returns true on success, false if was_full callback returns true (for backpressure logging).
   */
  template <typename WasFullCallback = std::nullptr_t>
  void push_wait(T&& value, WasFullCallback was_full = nullptr) {
    bool logged = false;
    while (!push(std::move(value))) {
      if constexpr (!std::is_null_pointer_v<WasFullCallback>) {
        if (!logged) {
          was_full();
          logged = true;
        }
      }
      std::this_thread::yield();
    }
  }

  /**
   * Try to pop an element. Returns std::nullopt if buffer is empty.
   */
  std::optional<T> pop() {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    auto value = std::move(buffer_[tail]);
    buffer_[tail].reset();
    tail_.store((tail + 1) & mask_, std::memory_order_release);
    return value;
  }

  /**
   * Pop an element, blocking (spinning) until data is available.
   */
  T pop_wait() {
    std::optional<T> value;
    while (!(value = pop())) {
      std::this_thread::yield();
    }
    return std::move(*value);
  }

  /**
   * Check if buffer is empty. Can be called from any thread.
   */
  [[nodiscard]] bool empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  /**
   * Get approximate size. Can be called from any thread but may be slightly stale.
   */
  [[nodiscard]] size_t size() const {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return (head - tail) & mask_;
  }

  /**
   * Maximum capacity.
   */
  [[nodiscard]] size_t capacity() const { return capacity_ - 1; }

 private:
  const size_t capacity_;
  const size_t mask_;

  // Align to cache line to avoid false sharing
  alignas(kCacheLineSize) std::atomic<size_t> head_{0};
  alignas(kCacheLineSize) std::atomic<size_t> tail_{0};
  alignas(kCacheLineSize) std::unique_ptr<std::optional<T>[]> buffer_;
};
