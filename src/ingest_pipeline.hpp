#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

#include "json_parser.hpp"
#include "perf_stats.hpp"
#include "types/ring_buffer.hpp"

class MessagePipeline {
 public:
  MessagePipeline(size_t capacity, PerfStats* perf_stats);

  MessagePipeline(const MessagePipeline&) = delete;
  MessagePipeline& operator=(const MessagePipeline&) = delete;

  size_t ingest_message(std::string_view message);
  size_t poll_messages(SmallVector<PolymarketMessage, 16>& out, size_t max_messages = 0);
  [[nodiscard]] bool wait_for_messages(std::chrono::microseconds timeout);
  void notify_shutdown();

  [[nodiscard]] size_t size() const noexcept;

 private:
  void notify_consumer();

  RingBuffer<PolymarketMessage, 0> queue_;
  PerfStats* perf_stats_ = nullptr;
  JsonParser parser_;
  SmallVector<PolymarketMessage, 16> parsed_batch_;

  mutable std::mutex wait_mutex_;
  std::condition_variable wait_cv_;
  std::atomic<uint64_t> sequence_{0};
  std::atomic<bool> stopping_{false};
};
