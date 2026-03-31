#pragma once
#include <atomic>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "exchange.hpp"
#include "market_book.hpp"
#include "market_data_transport.hpp"
#include "portfolio_tracker.hpp"
#include "strategy.hpp"

struct EngineConfig {
  std::string ws_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
  std::vector<AssetId> asset_ids;

  struct RuntimeTuning {
    TransportMode mode = TransportMode::IxWebSocket;
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
    PerfStatsConfig perf_stats;
  } runtime;

  struct AssetMapping {
    MarketId market_id;
    Outcome outcome;
  };
  std::unordered_map<AssetId, AssetMapping> asset_mappings;
};

int run_engine(std::shared_ptr<Strategy> strategy, const EngineConfig& config);
void stop_active_engine();
