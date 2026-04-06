#include "transport/polymarket_ws.hpp"

#include <simdjson.h>

#include "utils/thread.hpp"

#define LOGGER_NAME "WebSocket"
#include "utils/logger.hpp"

PolymarketWS::PolymarketWS(const TransportConfig& config)
    : config_(config),
      perf_stats_(config_.perf_stats),
      pipeline_(config_.message_queue_capacity, &perf_stats_) {
  current_subscriptions_ = {config.asset_ids.begin(), config.asset_ids.end()};

  ws_.setUrl(config_.url);
  ws_.setPingInterval(config_.ping_interval_secs);
  ws_.setExtraHeaders({{"User-Agent", "polybench/1.0"}});

  ws_.enableAutomaticReconnection();
  ws_.setMinWaitBetweenReconnectionRetries(config_.reconnect_wait_secs * 1000);
  ws_.setMaxWaitBetweenReconnectionRetries(config_.reconnect_wait_max_secs * 1000);

  ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    maybe_pin_ingest_thread();
    switch (msg->type) {
      case ix::WebSocketMessageType::Open:
        LOG_INFO("Connected to {}", config_.url);
        connected_ = true;

        send_subscription(current_subscriptions_, ws_);

        {
          std::lock_guard<std::mutex> lock(callback_mutex_);
          if (connect_callback_) {
            connect_callback_();
          }
        }
        break;

      case ix::WebSocketMessageType::Close:
        LOG_INFO("Disconnected: {} ({})", msg->closeInfo.reason, msg->closeInfo.code);
        connected_ = false;
        {
          std::lock_guard<std::mutex> lock(callback_mutex_);
          if (disconnect_callback_) {
            disconnect_callback_();
          }
        }
        break;

      case ix::WebSocketMessageType::Error:
        LOG_ERROR("WebSocket error: {}", msg->errorInfo.reason);
        {
          std::lock_guard<std::mutex> lock(callback_mutex_);
          if (error_callback_) {
            error_callback_(msg->errorInfo.reason);
          }
        }
        break;

      case ix::WebSocketMessageType::Message:
        handle_message(msg->str);
        break;

      // Handled internally by IXWebSocket
      case ix::WebSocketMessageType::Ping:
      case ix::WebSocketMessageType::Pong:
      case ix::WebSocketMessageType::Fragment:
        break;
    }
  });
}

PolymarketWS::~PolymarketWS() { stop(); }

void PolymarketWS::on_error(ErrorCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  error_callback_ = std::move(callback);
}

size_t PolymarketWS::poll_messages(SmallVector<PolymarketMessage, kMessageBatchSize>& out,
                                   size_t max_messages) {
  size_t count = pipeline_.poll_messages(out, max_messages);
  // Only count productive polls — avoids up to 256 atomic increments per spin-wait cycle
  if (count > 0) {
    perf_stats_.record_poll();
  }
  return count;
}

void PolymarketWS::on_connect(ConnectCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  connect_callback_ = std::move(callback);
}

void PolymarketWS::on_disconnect(DisconnectCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  disconnect_callback_ = std::move(callback);
}

void PolymarketWS::start() {
  LOG_INFO("Starting WebSocket connection to {}", config_.url);
  ws_.start();
}

void PolymarketWS::stop() {
  LOG_INFO("Stopping WebSocket connection");
  pipeline_.notify_shutdown();
  ws_.stop();
  connected_ = false;
}

bool PolymarketWS::is_connected() const { return connected_; }

bool PolymarketWS::wait_for_messages(std::chrono::microseconds timeout) {
  return pipeline_.wait_for_messages(timeout);
}

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void send_subscription(const R& asset_ids, ix::WebSocket& ws) {
  simdjson::builder::string_builder sb;
  sb.start_object();
  sb.append_key_value<"assets_ids">(asset_ids);
  sb.append_comma();
  sb.append_key_value<"type">("market");
  sb.append_comma();
  sb.append_key_value<"operation">("subscribe");
  sb.append_comma();
  sb.append_key_value<"custom_feature_enabled">(true);
  sb.end_object();

  std::string_view msg = sb;
  LOG_INFO("Sending subscription for {} assets", asset_ids.size());
  LOG_DEBUG("Subscription message: {}", msg);

  ws.send(std::string(msg));
}

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void PolymarketWS::subscribe(const R& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      current_subscriptions_.insert(AssetId{id});
    }
  }

  if (connected_) {
    send_subscription(asset_ids, ws_);
  }
}

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void send_unsubscription(const R& asset_ids, ix::WebSocket& ws) {
  simdjson::builder::string_builder sb;
  sb.start_object();
  sb.append_key_value<"assets_ids">(asset_ids);
  sb.append_comma();
  sb.append_key_value<"type">("market");
  sb.append_comma();
  sb.append_key_value<"operation">("unsubscribe");
  sb.end_object();

  std::string_view msg = sb;
  LOG_INFO("Sending unsubscription for {} assets", asset_ids.size());
  LOG_DEBUG("Unsubscription message: {}", msg);

  ws.send(std::string(msg));
}

template <std::ranges::range R>
requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<R>>, AssetId>
void PolymarketWS::unsubscribe(const R& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      AssetId asset_id{id};
      if (current_subscriptions_.erase(asset_id) == 0) {
        LOG_WARN("Asset {} not found in current subscriptions", id);
      }
    }
  }

  if (connected_) {
    send_unsubscription(asset_ids, ws_);
  }
}

void PolymarketWS::handle_message(std::string_view message) {
  pipeline_.ingest_message(message);
}

const PerfStats& PolymarketWS::perf_stats() const { return perf_stats_; }
PerfStats& PolymarketWS::perf_stats() { return perf_stats_; }

void PolymarketWS::maybe_pin_ingest_thread() {
  if (ingest_thread_pinned_.load(std::memory_order_acquire)) return;
  if (config_.ingest_cpu_affinity < 0) return;

  if (utils::thread::pin_current_thread_to_cpu(config_.ingest_cpu_affinity, "ingest")) {
    ingest_thread_pinned_.store(true, std::memory_order_release);
  }
}

// Explicit template instantiations for types used in the codebase
template void PolymarketWS::subscribe<std::vector<AssetId>>(const std::vector<AssetId>&);
template void PolymarketWS::unsubscribe<SmallVector<AssetId, 2>>(const SmallVector<AssetId, 2>&);
