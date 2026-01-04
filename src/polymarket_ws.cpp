#include "polymarket_ws.hpp"

#include <simdjson.h>

#define LOGGER_NAME "WebSocket"
#include "logger.hpp"

PolymarketWS::PolymarketWS(const WsConfig& config) : config_(config) {
  current_subscriptions_ = {config.asset_ids.begin(), config.asset_ids.end()};

  ws_.setUrl(config_.url);
  ws_.setPingInterval(config_.ping_interval_secs);
  ws_.setExtraHeaders({{"User-Agent", "polybench/1.0"}});

  ws_.enableAutomaticReconnection();
  ws_.setMinWaitBetweenReconnectionRetries(config_.reconnect_wait_secs * 1000);
  ws_.setMaxWaitBetweenReconnectionRetries(config_.reconnect_wait_max_secs * 1000);

  ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
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

std::vector<PolymarketMessage> PolymarketWS::poll_messages(size_t max_messages) {
  std::vector<PolymarketMessage> messages;
  std::lock_guard<std::mutex> lock(queue_mutex_);

  size_t count = 0;
  while (!message_queue_.empty() && (max_messages == 0 || count < max_messages)) {
    messages.push_back(std::move(message_queue_.front()));
    message_queue_.pop();
    ++count;
  }

  return messages;
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
  ws_.stop();
  connected_ = false;
}

bool PolymarketWS::is_connected() const { return connected_; }

template <std::ranges::range R>
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

void PolymarketWS::subscribe(const std::vector<std::string>& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      current_subscriptions_.insert(id);
    }
  }

  if (connected_) {
    send_subscription(asset_ids, ws_);
  }
}

template <std::ranges::range R>
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

void PolymarketWS::unsubscribe(const std::vector<std::string>& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      if (current_subscriptions_.erase(id) == 0) {
        LOG_WARN("Asset {} not found in current subscriptions", id);
      }
    }
  }

  if (connected_) {
    send_unsubscription(asset_ids, ws_);
  }
}

void PolymarketWS::handle_message(const std::string& message) {
  LOG_DEBUG("Received message ({} bytes)", message.length());

  auto parsed = json_parser_.parse(message);
  if (!parsed.empty()) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (auto& msg : parsed) {
      message_queue_.push(std::move(msg));
    }
    LOG_DEBUG("Queued {} messages (queue size: {})", parsed.size(), message_queue_.size());
  }
}
