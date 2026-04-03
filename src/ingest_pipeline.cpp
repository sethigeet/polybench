#include "ingest_pipeline.hpp"

MessagePipeline::MessagePipeline(size_t capacity, PerfStats* perf_stats)
    : queue_(capacity), perf_stats_(perf_stats) {}

size_t MessagePipeline::ingest_message(std::string_view message) {
  const bool stats_active = perf_stats_ && perf_stats_->enabled();

  if (stats_active) {
    perf_stats_->record_frame(message.size());
  }

  parsed_batch_.clear();

  // Only call steady_clock::now() when stats are active (~20-30ns each avoided otherwise)
  const auto parse_start = stats_active ? std::chrono::steady_clock::now()
                                        : std::chrono::steady_clock::time_point{};
  const size_t parsed_count = parser_.parse(message, parsed_batch_, stats_active ? perf_stats_ : nullptr);

  if (stats_active) {
    const uint64_t parse_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - parse_start)
            .count();
    perf_stats_->record_parse(parsed_count, parse_ns, parsed_count == 0 ? 1 : 0);
  }

  bool had_backpressure = false;
  size_t dropped = 0;
  std::chrono::steady_clock::time_point queue_wait_start;
  if (stats_active) {
    queue_wait_start = std::chrono::steady_clock::now();
  }
  for (auto& parsed : parsed_batch_) {
    if (!queue_.push(std::move(parsed))) {
      had_backpressure = true;
      ++dropped;
    }
  }

  if (had_backpressure && stats_active) {
    const uint64_t wait_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - queue_wait_start)
                                 .count();
    perf_stats_->record_queue_backpressure(wait_ns, dropped);
  }

  if (parsed_count > dropped) {
    notify_consumer();
  }

  return parsed_count - dropped;
}

size_t MessagePipeline::poll_messages(SmallVector<PolymarketMessage, 16>& out,
                                      size_t max_messages) {
  size_t count = 0;
  while (max_messages == 0 || count < max_messages) {
    auto message = queue_.pop();
    if (!message.has_value()) break;
    out.push_back(std::move(*message));
    ++count;
  }
  return count;
}

bool MessagePipeline::wait_for_messages(std::chrono::microseconds timeout) {
  if (!queue_.empty()) return true;

  const uint64_t observed_sequence = sequence_.load(std::memory_order_acquire);
  std::unique_lock<std::mutex> lock(wait_mutex_);
  return wait_cv_.wait_for(lock, timeout, [this, observed_sequence]() {
    return stopping_.load(std::memory_order_acquire) || !queue_.empty() ||
           sequence_.load(std::memory_order_acquire) != observed_sequence;
  });
}

void MessagePipeline::notify_shutdown() {
  stopping_.store(true, std::memory_order_release);
  notify_consumer();
}

size_t MessagePipeline::size() const noexcept { return queue_.size(); }

void MessagePipeline::notify_consumer() {
  sequence_.fetch_add(1, std::memory_order_release);
  wait_cv_.notify_one();
}
