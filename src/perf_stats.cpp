#include "perf_stats.hpp"

#include <string_view>

#define LOGGER_NAME "Perf"
#include "logger.hpp"

namespace {
uint64_t load_relaxed(const std::atomic<uint64_t>& value) noexcept {
  return value.load(std::memory_order_relaxed);
}
}  // namespace

PerfStats::PerfStats(PerfStatsConfig config) : config_(config) {}

bool PerfStats::enabled() const noexcept { return config_.enabled; }

void PerfStats::record_frame(size_t bytes) noexcept {
  if (!enabled()) return;

  frames_received_.fetch_add(1, std::memory_order_relaxed);
  bytes_received_.fetch_add(bytes, std::memory_order_relaxed);
}

void PerfStats::record_parse(size_t parsed_messages, uint64_t parse_ns, uint64_t parse_errors) noexcept {
  if (!enabled()) return;

  messages_parsed_.fetch_add(parsed_messages, std::memory_order_relaxed);
  total_parse_ns_.fetch_add(parse_ns, std::memory_order_relaxed);
  this->parse_errors_.fetch_add(parse_errors, std::memory_order_relaxed);
}

void PerfStats::record_queue_backpressure(uint64_t wait_ns, uint64_t dropped_messages) noexcept {
  if (!enabled()) return;

  queue_backpressure_events_.fetch_add(1, std::memory_order_relaxed);
  total_queue_wait_ns_.fetch_add(wait_ns, std::memory_order_relaxed);
  queue_push_failures_.fetch_add(dropped_messages, std::memory_order_relaxed);
  queue_dropped_messages_.fetch_add(dropped_messages, std::memory_order_relaxed);
}

void PerfStats::record_engine_dispatch(size_t processed_messages, uint64_t dispatch_ns) noexcept {
  if (!enabled()) return;

  total_engine_dispatch_ns_.fetch_add(dispatch_ns, std::memory_order_relaxed);
  total_polls_.fetch_add(processed_messages > 0 ? 1 : 0, std::memory_order_relaxed);
}

void PerfStats::record_poll() noexcept {
  if (!enabled()) return;
  total_polls_.fetch_add(1, std::memory_order_relaxed);
}

void PerfStats::record_message_type(std::string_view event_type) noexcept {
  if (!enabled()) return;

  if (event_type == "book") {
    book_messages_.fetch_add(1, std::memory_order_relaxed);
  } else if (event_type == "price_change") {
    price_change_messages_.fetch_add(1, std::memory_order_relaxed);
  } else if (event_type == "last_trade_price") {
    trade_messages_.fetch_add(1, std::memory_order_relaxed);
  } else if (event_type == "tick_size_change") {
    tick_size_messages_.fetch_add(1, std::memory_order_relaxed);
  } else if (event_type == "market_resolved") {
    resolved_messages_.fetch_add(1, std::memory_order_relaxed);
  }
}

PerfStatsSnapshot PerfStats::snapshot() const noexcept {
  PerfStatsSnapshot snapshot;
  snapshot.frames_received = load_relaxed(frames_received_);
  snapshot.bytes_received = load_relaxed(bytes_received_);
  snapshot.messages_parsed = load_relaxed(messages_parsed_);
  snapshot.parse_errors = load_relaxed(parse_errors_);
  snapshot.queue_push_failures = load_relaxed(queue_push_failures_);
  snapshot.queue_backpressure_events = load_relaxed(queue_backpressure_events_);
  snapshot.queue_dropped_messages = load_relaxed(queue_dropped_messages_);
  snapshot.book_messages = load_relaxed(book_messages_);
  snapshot.price_change_messages = load_relaxed(price_change_messages_);
  snapshot.trade_messages = load_relaxed(trade_messages_);
  snapshot.tick_size_messages = load_relaxed(tick_size_messages_);
  snapshot.resolved_messages = load_relaxed(resolved_messages_);
  snapshot.total_parse_ns = load_relaxed(total_parse_ns_);
  snapshot.total_queue_wait_ns = load_relaxed(total_queue_wait_ns_);
  snapshot.total_engine_dispatch_ns = load_relaxed(total_engine_dispatch_ns_);
  snapshot.total_polls = load_relaxed(total_polls_);
  return snapshot;
}

void PerfStats::maybe_log(std::string_view logger_name) const {
  if (!enabled()) return;

  const uint64_t current_messages = messages_parsed_.load(std::memory_order_relaxed);
  uint64_t last_logged = last_logged_message_count_.load(std::memory_order_relaxed);
  if (current_messages < last_logged + config_.log_interval_messages) {
    return;
  }

  if (!last_logged_message_count_.compare_exchange_strong(last_logged, current_messages,
                                                          std::memory_order_relaxed)) {
    return;
  }

  const auto snap = snapshot();
  const double avg_parse_ns =
      snap.messages_parsed == 0 ? 0.0 : static_cast<double>(snap.total_parse_ns) / snap.messages_parsed;
  const double avg_dispatch_ns = snap.messages_parsed == 0
                                     ? 0.0
                                     : static_cast<double>(snap.total_engine_dispatch_ns) / snap.messages_parsed;

  logger::get(std::string(logger_name))
      .info("Perf frames={} bytes={} parsed={} parse_errors={} dropped={} avg_parse_ns={:.1f} "
            "avg_dispatch_ns={:.1f} backpressure={}",
            snap.frames_received, snap.bytes_received, snap.messages_parsed, snap.parse_errors,
            snap.queue_dropped_messages, avg_parse_ns, avg_dispatch_ns, snap.queue_backpressure_events);
}
