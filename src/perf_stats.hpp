#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

struct PerfStatsConfig {
  bool enabled = false;
  uint64_t log_interval_messages = 10000;
};

struct PerfStatsSnapshot {
  uint64_t frames_received = 0;
  uint64_t bytes_received = 0;
  uint64_t messages_parsed = 0;
  uint64_t parse_errors = 0;
  uint64_t queue_push_failures = 0;
  uint64_t queue_backpressure_events = 0;
  uint64_t queue_dropped_messages = 0;
  uint64_t book_messages = 0;
  uint64_t price_change_messages = 0;
  uint64_t trade_messages = 0;
  uint64_t tick_size_messages = 0;
  uint64_t resolved_messages = 0;
  uint64_t total_parse_ns = 0;
  uint64_t total_queue_wait_ns = 0;
  uint64_t total_engine_dispatch_ns = 0;
  uint64_t total_polls = 0;
};

class PerfStats {
 public:
  explicit PerfStats(PerfStatsConfig config = {});

  [[nodiscard]] bool enabled() const noexcept;

  void record_frame(size_t bytes) noexcept;
  void record_parse(size_t parsed_messages, uint64_t parse_ns, uint64_t parse_errors) noexcept;
  void record_queue_backpressure(uint64_t wait_ns, uint64_t dropped_messages) noexcept;
  void record_engine_dispatch(size_t processed_messages, uint64_t dispatch_ns) noexcept;
  void record_poll() noexcept;
  void record_message_type(std::string_view event_type) noexcept;

  [[nodiscard]] PerfStatsSnapshot snapshot() const noexcept;
  void maybe_log(std::string_view logger_name) const;

 private:
  PerfStatsConfig config_;
  mutable std::atomic<uint64_t> last_logged_message_count_{0};

  std::atomic<uint64_t> frames_received_{0};
  std::atomic<uint64_t> bytes_received_{0};
  std::atomic<uint64_t> messages_parsed_{0};
  std::atomic<uint64_t> parse_errors_{0};
  std::atomic<uint64_t> queue_push_failures_{0};
  std::atomic<uint64_t> queue_backpressure_events_{0};
  std::atomic<uint64_t> queue_dropped_messages_{0};
  std::atomic<uint64_t> book_messages_{0};
  std::atomic<uint64_t> price_change_messages_{0};
  std::atomic<uint64_t> trade_messages_{0};
  std::atomic<uint64_t> tick_size_messages_{0};
  std::atomic<uint64_t> resolved_messages_{0};
  std::atomic<uint64_t> total_parse_ns_{0};
  std::atomic<uint64_t> total_queue_wait_ns_{0};
  std::atomic<uint64_t> total_engine_dispatch_ns_{0};
  std::atomic<uint64_t> total_polls_{0};
};
