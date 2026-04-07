#pragma once

#include <functional>
#include <string>

#include "utils/perf_stats.hpp"
#include "types/common.hpp"

inline constexpr size_t kMessageBatchSize = 16;

enum class TransportMode {
  IxWebSocket,
#if defined(__linux__) && defined(HAS_IO_URING)
  IoUring,
#endif
};

struct TransportConfig {
  TransportMode mode = TransportMode::IxWebSocket;
  std::string url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
  std::vector<AssetId> asset_ids;

  int ping_interval_secs = 10;
  int reconnect_wait_secs = 1;
  int reconnect_wait_max_secs = 30;

  size_t message_queue_capacity = 4096;
  int consumer_spin_count = 256;
  int consumer_wait_timeout_us = 500;
  int consumer_sleep_initial_us = 50;
  int consumer_sleep_max_us = 1000;

  int ingest_cpu_affinity = -1;
  int engine_cpu_affinity = -1;
  int socket_rcvbuf_bytes = 0;
  int busy_poll_us = 0;
  size_t recv_batch_size = 32;

  // io_uring-specific tuning
  int io_uring_queue_depth = 256;
  int io_uring_buf_count = 256;
  int io_uring_buf_size = 16384;

  PerfStatsConfig perf_stats;
};

using ErrorCallback = std::function<void(const std::string&)>;
using ConnectCallback = std::function<void()>;
using DisconnectCallback = std::function<void()>;
