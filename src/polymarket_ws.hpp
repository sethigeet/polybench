#pragma once

#include <ixwebsocket/IXWebSocket.h>

#include <atomic>
#include <mutex>
#include <ranges>
#include <unordered_set>

#include "ingest_pipeline.hpp"
#include "market_data_transport.hpp"

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void send_subscription(const R& asset_ids, ix::WebSocket& ws);
template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void send_unsubscription(const R& asset_ids, ix::WebSocket& ws);

class PolymarketWS {
 public:
  explicit PolymarketWS(const TransportConfig& config);
  ~PolymarketWS();

  // Non-copyable
  PolymarketWS(const PolymarketWS&) = delete;
  PolymarketWS& operator=(const PolymarketWS&) = delete;

  void on_error(ErrorCallback callback);
  void on_connect(ConnectCallback callback);
  void on_disconnect(DisconnectCallback callback);

  void start();
  void stop();
  bool is_connected() const;

  size_t poll_messages(SmallVector<PolymarketMessage, kMessageBatchSize>& out,
                       size_t max_messages = 0);
  bool wait_for_messages(std::chrono::microseconds timeout);

  template <std::ranges::range R>
  requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
  void subscribe(const R& asset_ids);
  template <std::ranges::range R>
  requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
  void unsubscribe(const R& asset_ids);

  [[nodiscard]] const PerfStats& perf_stats() const;

 private:
  void handle_message(std::string_view message);
  void maybe_pin_ingest_thread();

  TransportConfig config_;
  ix::WebSocket ws_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> ingest_thread_pinned_{false};

  ErrorCallback error_callback_;
  ConnectCallback connect_callback_;
  DisconnectCallback disconnect_callback_;

  std::mutex callback_mutex_;
  std::mutex subscription_mutex_;
  std::unordered_set<AssetId> current_subscriptions_;

  PerfStats perf_stats_;
  MessagePipeline pipeline_;
};
