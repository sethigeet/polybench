#include "polymarket_ws.hpp"

#include <nlohmann/json.hpp>

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

        send_subscription({current_subscriptions_.begin(), current_subscriptions_.end()});

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

void PolymarketWS::subscribe(const std::vector<std::string>& asset_ids) {
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    for (const auto& id : asset_ids) {
      current_subscriptions_.insert(id);
    }
  }

  if (connected_) {
    send_subscription(asset_ids);
  }
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
    send_unsubscription(asset_ids);
  }
}

void PolymarketWS::send_subscription(const std::vector<std::string>& asset_ids) {
  nlohmann::json sub_msg;
  {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    sub_msg["assets_ids"] = asset_ids;
  }
  sub_msg["type"] = "market";
  sub_msg["operation"] = "subscribe";
  sub_msg["custom_feature_enabled"] = true;

  std::string msg = sub_msg.dump();
  LOG_INFO("Sending subscription for {} assets", asset_ids.size());
  LOG_DEBUG("Subscription message: {}", msg);

  ws_.send(msg);
}

void PolymarketWS::send_unsubscription(const std::vector<std::string>& asset_ids) {
  nlohmann::json unsub_msg;
  unsub_msg["assets_ids"] = asset_ids;
  unsub_msg["type"] = "market";
  unsub_msg["operation"] = "unsubscribe";

  std::string msg = unsub_msg.dump();
  LOG_INFO("Sending unsubscription for {} assets", asset_ids.size());
  LOG_DEBUG("Unsubscription message: {}", msg);

  ws_.send(msg);
}

void PolymarketWS::handle_message(const std::string& message) {
  LOG_DEBUG("Received message ({} bytes)", message.length());

  auto parsed = JsonParser::parse(message);
  if (!parsed.empty()) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (auto& msg : parsed) {
      message_queue_.push(std::move(msg));
    }
    LOG_DEBUG("Queued {} messages (queue size: {})", parsed.size(), message_queue_.size());
  }
}
