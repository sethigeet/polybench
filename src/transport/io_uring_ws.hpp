#pragma once

#if defined(__linux__) && defined(HAS_IO_URING)

#include <atomic>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_set>

#include "transport/ingest_pipeline.hpp"
#include "transport/market_data_transport.hpp"

class IoUringWS {
 public:
  explicit IoUringWS(const TransportConfig& config);
  ~IoUringWS();

  // Non-copyable
  IoUringWS(const IoUringWS&) = delete;
  IoUringWS& operator=(const IoUringWS&) = delete;

  void on_error(ErrorCallback callback);
  void on_connect(ConnectCallback callback);
  void on_disconnect(DisconnectCallback callback);

  void start();
  void stop();
  [[nodiscard]] bool is_connected() const;

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
  [[nodiscard]] PerfStats& perf_stats();

  // Forward-declared PIMPL to keep liburing/OpenSSL headers out of this header
  struct IoContext;

 private:
  void io_loop();
  void send_ws_text(std::string_view payload);

  TransportConfig config_;
  PerfStats perf_stats_;
  MessagePipeline pipeline_;

  std::atomic<bool> connected_{false};
  std::atomic<bool> running_{false};
  std::thread io_thread_;

  // Callbacks
  ErrorCallback error_callback_;
  ConnectCallback connect_callback_;
  DisconnectCallback disconnect_callback_;
  std::mutex callback_mutex_;

  // Subscription tracking
  std::mutex subscription_mutex_;
  std::unordered_set<AssetId> current_subscriptions_;

  // Lock-free send queue: engine thread enqueues, io_thread drains
  struct PendingSend {
    std::string data;
  };
  std::mutex send_mutex_;
  std::vector<PendingSend> send_queue_;

  std::unique_ptr<IoContext> io_ctx_;
};

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void IoUringWS::subscribe(const R& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      current_subscriptions_.insert(AssetId{id});
    }
  }

  if (connected_) {
    // Build subscription JSON using simdjson builder
    simdjson::builder::string_builder sb;
    sb.start_object();
    sb.template append_key_value<"assets_ids">(asset_ids);
    sb.append_comma();
    sb.template append_key_value<"type">("market");
    sb.append_comma();
    sb.template append_key_value<"operation">("subscribe");
    sb.append_comma();
    sb.template append_key_value<"custom_feature_enabled">(true);
    sb.end_object();

    send_ws_text(std::string_view(sb));
  }
}

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void IoUringWS::unsubscribe(const R& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      current_subscriptions_.erase(AssetId{id});
    }
  }

  if (connected_) {
    simdjson::builder::string_builder sb;
    sb.start_object();
    sb.template append_key_value<"assets_ids">(asset_ids);
    sb.append_comma();
    sb.template append_key_value<"type">("market");
    sb.append_comma();
    sb.template append_key_value<"operation">("unsubscribe");
    sb.end_object();

    send_ws_text(std::string_view(sb));
  }
}

#endif  // defined(__linux__) && defined(HAS_IO_URING)
