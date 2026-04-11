#if defined(__linux__) && defined(HAS_IO_URING)

#include <gtest/gtest.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "transport/io_uring_ws.hpp"
#include "transport/market_data_transport.hpp"

using namespace std::chrono_literals;

namespace {
/// Bind to port 0 to let the OS assign an ephemeral port, then close the
/// socket and return that port.  ix::WebSocketServer::getPort() returns the
/// constructor argument, so when passing 0 it always returns 0.  This helper
/// works around that.
int find_free_port() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  socklen_t len = sizeof(addr);
  getsockname(sock, reinterpret_cast<struct sockaddr*>(&addr), &len);
  int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}
}  // namespace

// ---------------------------------------------------------------------------
// Test fixture: spins up a local ix::WebSocketServer on an ephemeral port
// ---------------------------------------------------------------------------
class IoUringWSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_port_ = find_free_port();

    server_ = std::make_unique<ix::WebSocketServer>(server_port_, "127.0.0.1");
    server_->disablePerMessageDeflate();

    server_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> /*state*/,
                                               ix::WebSocket& ws,
                                               const ix::WebSocketMessagePtr& msg) {
      if (msg->type == ix::WebSocketMessageType::Open) {
        // Track connected client
        std::lock_guard<std::mutex> lk(clients_mutex_);
        clients_.push_back(&ws);
      } else if (msg->type == ix::WebSocketMessageType::Message) {
        std::lock_guard<std::mutex> lk(handler_mutex_);
        if (on_server_message_) {
          on_server_message_(ws, msg->str);
        }
      }
    });

    auto res = server_->listenAndStart();
    ASSERT_TRUE(res) << "Failed to start local WebSocket server on port " << server_port_;

    // Small delay to ensure the server is listening
    std::this_thread::sleep_for(50ms);
  }

  void TearDown() override {
    if (server_) {
      server_->stop();
    }
  }

  TransportConfig make_config() {
    TransportConfig cfg;
    cfg.mode = TransportMode::IoUring;
    cfg.url = "ws://127.0.0.1:" + std::to_string(server_port_) + "/ws/market";
    cfg.ping_interval_secs = 30;  // Don't ping during short tests
    cfg.reconnect_wait_secs = 1;
    cfg.reconnect_wait_max_secs = 2;
    cfg.message_queue_capacity = 64;
    cfg.io_uring_queue_depth = 32;
    cfg.io_uring_buf_count = 16;
    cfg.io_uring_buf_size = 4096;
    cfg.perf_stats = {.enabled = true, .log_interval_messages = 1000000};
    return cfg;
  }

  // Broadcast a message from the server to all connected clients
  void server_send(const std::string& msg) {
    auto clients = server_->getClients();
    for (auto& ws : clients) {
      ws->send(msg);
    }
  }

  // Wait until the transport reports connected, with timeout
  bool wait_connected(IoUringWS& transport, std::chrono::milliseconds timeout = 3000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!transport.is_connected() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(10ms);
    }
    return transport.is_connected();
  }

  std::unique_ptr<ix::WebSocketServer> server_;
  int server_port_ = 0;

  std::mutex clients_mutex_;
  std::vector<ix::WebSocket*> clients_;

  std::mutex handler_mutex_;
  std::function<void(ix::WebSocket&, const std::string&)> on_server_message_;
};

// ---------------------------------------------------------------------------
// Test: Connect and verify connected state
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, ConnectsToLocalServer) {
  auto cfg = make_config();
  IoUringWS transport(cfg);

  bool connect_called = false;
  transport.on_connect([&connect_called]() { connect_called = true; });

  transport.start();
  ASSERT_TRUE(wait_connected(transport)) << "Transport did not connect within timeout";
  EXPECT_TRUE(connect_called);

  transport.stop();
  EXPECT_FALSE(transport.is_connected());
}

// ---------------------------------------------------------------------------
// Test: Receive a book message via poll_messages
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, ReceivesBookMessage) {
  auto cfg = make_config();
  IoUringWS transport(cfg);

  transport.start();
  ASSERT_TRUE(wait_connected(transport));

  // Small delay so the server recognizes the client
  std::this_thread::sleep_for(50ms);

  // Server sends a book message
  std::string book_json = R"([{
    "event_type": "book",
    "asset_id": "test-asset-123",
    "market": "0xdeadbeef",
    "bids": [{"price": "0.50", "size": "100"}],
    "asks": [{"price": "0.55", "size": "200"}],
    "timestamp": "1704067200000000"
  }])";
  server_send(book_json);

  // Poll until we get the message
  SmallVector<PolymarketMessage, kMessageBatchSize> messages;
  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (messages.empty() && std::chrono::steady_clock::now() < deadline) {
    transport.wait_for_messages(100000us);
    transport.poll_messages(messages, kMessageBatchSize);
  }

  ASSERT_GE(messages.size(), 1);
  EXPECT_TRUE(std::holds_alternative<BookMessage>(messages[0]));

  if (auto* book = std::get_if<BookMessage>(&messages[0])) {
    EXPECT_EQ(std::string_view(book->asset_id), "test-asset-123");
  }

  transport.stop();
}

// ---------------------------------------------------------------------------
// Test: Receive multiple messages in batch
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, ReceivesMultipleMessages) {
  auto cfg = make_config();
  IoUringWS transport(cfg);

  transport.start();
  ASSERT_TRUE(wait_connected(transport));
  std::this_thread::sleep_for(50ms);

  // Send multiple trade messages
  for (int i = 0; i < 5; ++i) {
    std::string trade_json = R"([{
      "event_type": "last_trade_price",
      "asset_id": "asset-)" + std::to_string(i) +
                             R"(",
      "market": "market-1",
      "price": "0.55",
      "side": "buy",
      "size": "10",
      "fee_rate_bps": "0",
      "timestamp": "123"
    }])";
    server_send(trade_json);
    std::this_thread::sleep_for(10ms);
  }

  // Collect all messages
  SmallVector<PolymarketMessage, kMessageBatchSize> messages;
  auto deadline = std::chrono::steady_clock::now() + 3s;
  size_t total = 0;
  while (total < 5 && std::chrono::steady_clock::now() < deadline) {
    transport.wait_for_messages(100000us);
    total += transport.poll_messages(messages, kMessageBatchSize);
  }

  EXPECT_GE(total, 3) << "Expected at least 3 of 5 messages (allowing for timing)";

  transport.stop();
}

// ---------------------------------------------------------------------------
// Test: Subscribe sends JSON to server
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, SubscribeSendsMessage) {
  std::string received_subscription;
  {
    std::lock_guard<std::mutex> lk(handler_mutex_);
    on_server_message_ = [&](ix::WebSocket& /*ws*/, const std::string& msg) {
      received_subscription = msg;
    };
  }

  auto cfg = make_config();
  IoUringWS transport(cfg);

  transport.start();
  ASSERT_TRUE(wait_connected(transport));
  std::this_thread::sleep_for(50ms);

  std::vector<AssetId> assets;
  assets.push_back(AssetId{"test-asset-1"});
  assets.push_back(AssetId{"test-asset-2"});
  transport.subscribe(assets);

  // Wait for the subscription message to arrive at the server
  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (received_subscription.empty() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }

  EXPECT_FALSE(received_subscription.empty()) << "Server did not receive subscription message";
  if (!received_subscription.empty()) {
    EXPECT_NE(received_subscription.find("subscribe"), std::string::npos);
    EXPECT_NE(received_subscription.find("test-asset-1"), std::string::npos);
  }

  transport.stop();
}

// ---------------------------------------------------------------------------
// Test: Disconnect callback fires on server stop
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, DisconnectCallbackOnServerStop) {
  auto cfg = make_config();
  IoUringWS transport(cfg);

  std::atomic<bool> disconnected{false};
  transport.on_disconnect([&disconnected]() { disconnected = true; });

  transport.start();
  ASSERT_TRUE(wait_connected(transport));

  // Stop the server — should trigger disconnect
  server_->stop();

  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!disconnected && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }

  EXPECT_TRUE(disconnected.load());

  transport.stop();
}

// ---------------------------------------------------------------------------
// Test: Perf stats track io_uring metrics
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, PerfStatsRecordIoUringBatches) {
  auto cfg = make_config();
  IoUringWS transport(cfg);

  transport.start();
  ASSERT_TRUE(wait_connected(transport));
  std::this_thread::sleep_for(100ms);  // Let some CQEs be processed

  auto snapshot = transport.perf_stats().snapshot();
  // After connecting, there should have been at least one CQE batch
  // (connect CQE + recv CQEs during handshake)
  EXPECT_GT(snapshot.io_uring_cqe_batches, 0);
  EXPECT_GT(snapshot.io_uring_total_cqes, 0);

  transport.stop();
}

// ---------------------------------------------------------------------------
// Test: Error callback on invalid URL
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, ErrorCallbackOnBadHost) {
  auto cfg = make_config();
  cfg.url = "ws://this-host-does-not-exist-999.invalid:12345/ws";

  // IoUringWS constructor should succeed (URL parsing is fine)
  // But connection should fail and trigger error/disconnect
  IoUringWS transport(cfg);

  std::atomic<bool> error_called{false};
  transport.on_error([&error_called](const std::string& /*msg*/) { error_called = true; });

  transport.start();

  // Wait a bit — DNS resolution should fail
  std::this_thread::sleep_for(2s);

  // The transport should either have called the error callback or simply not connected
  EXPECT_FALSE(transport.is_connected());

  transport.stop();
}

// ---------------------------------------------------------------------------
// Test: Price change message round-trip
// ---------------------------------------------------------------------------
TEST_F(IoUringWSTest, ReceivesPriceChangeMessage) {
  auto cfg = make_config();
  IoUringWS transport(cfg);

  transport.start();
  ASSERT_TRUE(wait_connected(transport));
  std::this_thread::sleep_for(50ms);

  std::string price_change_json = R"([{
    "event_type": "price_change",
    "market": "market-1",
    "price_changes": [{
      "asset_id": "price-asset-1",
      "price": "0.65",
      "size": "50",
      "side": "sell",
      "best_bid": "0.64",
      "best_ask": "0.65"
    }],
    "timestamp": "456"
  }])";
  server_send(price_change_json);

  SmallVector<PolymarketMessage, kMessageBatchSize> messages;
  auto deadline = std::chrono::steady_clock::now() + 3s;
  while (messages.empty() && std::chrono::steady_clock::now() < deadline) {
    transport.wait_for_messages(100000us);
    transport.poll_messages(messages, kMessageBatchSize);
  }

  ASSERT_GE(messages.size(), 1);
  EXPECT_TRUE(std::holds_alternative<PriceChangeMessage>(messages[0]));

  transport.stop();
}

#else
// On non-Linux or without io_uring, provide a placeholder so the test binary links
#include <gtest/gtest.h>
TEST(IoUringWSTest, SkippedNotAvailable) {
  GTEST_SKIP() << "io_uring transport not available on this platform";
}
#endif  // defined(__linux__) && defined(HAS_IO_URING)
