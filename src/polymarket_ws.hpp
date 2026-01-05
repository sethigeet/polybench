#pragma once

#include <ixwebsocket/IXWebSocket.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "json_parser.hpp"

struct WsConfig {
  std::string url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
  std::vector<std::string> asset_ids;
  // Polymarket requires a ping interval of 10 seconds to keep the connection alive
  int ping_interval_secs = 10;
  int reconnect_wait_secs = 1;
  int reconnect_wait_max_secs = 30;
};

using ErrorCallback = std::function<void(const std::string&)>;
using ConnectCallback = std::function<void()>;
using DisconnectCallback = std::function<void()>;

template <std::ranges::range R>
void send_subscription(const R& asset_ids, ix::WebSocket& ws);
template <std::ranges::range R>
void send_unsubscription(const R& asset_ids, ix::WebSocket& ws);

class PolymarketWS {
 public:
  explicit PolymarketWS(const WsConfig& config);
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

  // Poll messages from queue (call from main thread to avoid blocking WS thread)
  // Returns up to max_messages, or all available if max_messages is 0
  std::vector<PolymarketMessage> poll_messages(size_t max_messages = 0);

  template <std::ranges::range R>
  void subscribe(const R& asset_ids);
  template <std::ranges::range R>
  void unsubscribe(const R& asset_ids);

 private:
  void handle_message(const std::string& message);

  WsConfig config_;
  ix::WebSocket ws_;
  std::atomic<bool> connected_{false};

  ErrorCallback error_callback_;
  ConnectCallback connect_callback_;
  DisconnectCallback disconnect_callback_;

  std::mutex callback_mutex_;
  std::mutex subscription_mutex_;
  std::unordered_set<std::string> current_subscriptions_;

  // Message queue for decoupling WS thread from processing thread
  std::queue<PolymarketMessage> message_queue_;
  std::mutex queue_mutex_;

  // NOTE: simdjson parser is not thread-safe
  JsonParser json_parser_;
};
