#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "ingest_pipeline.hpp"
#include "market_data_transport.hpp"

class FakeTransport {
 public:
  FakeTransport()
      : perf_stats_({.enabled = true, .log_interval_messages = 1000000}),
        pipeline_(64, &perf_stats_) {}

  void on_error(ErrorCallback callback) { error_callback_ = std::move(callback); }
  void on_connect(ConnectCallback callback) { connect_callback_ = std::move(callback); }
  void on_disconnect(DisconnectCallback callback) { disconnect_callback_ = std::move(callback); }

  void start() {
    connected_ = true;
    if (connect_callback_) connect_callback_();
  }

  void stop() {
    connected_ = false;
    pipeline_.notify_shutdown();
    if (disconnect_callback_) disconnect_callback_();
  }

  [[nodiscard]] bool is_connected() const { return connected_; }

  size_t poll_messages(SmallVector<PolymarketMessage, kMessageBatchSize>& out,
                       size_t max_messages = 0) {
    return pipeline_.poll_messages(out, max_messages);
  }

  [[nodiscard]] bool wait_for_messages(std::chrono::microseconds timeout) {
    return pipeline_.wait_for_messages(timeout);
  }

  [[nodiscard]] const PerfStats& perf_stats() const { return perf_stats_; }

  template <std::ranges::range R>
  void subscribe(const R& asset_ids) {
    subscribed_.insert(subscribed_.end(), asset_ids.begin(), asset_ids.end());
  }

  template <std::ranges::range R>
  void unsubscribe(const R& asset_ids) {
    for (const auto& asset_id : asset_ids) {
      subscribed_.erase(std::remove(subscribed_.begin(), subscribed_.end(), asset_id),
                        subscribed_.end());
    }
  }

 public:
  void ingest(std::string_view payload) { pipeline_.ingest_message(payload); }
  [[nodiscard]] const std::vector<AssetId>& subscriptions() const { return subscribed_; }

  bool connected_ = false;
  PerfStats perf_stats_;
  MessagePipeline pipeline_;
  std::vector<AssetId> subscribed_;
  ErrorCallback error_callback_;
  ConnectCallback connect_callback_;
  DisconnectCallback disconnect_callback_;
};

TEST(MarketDataTransportTest, FakeTransportPollsParsedMessages) {
  FakeTransport transport;
  transport.start();

  transport.ingest(R"({
    "event_type": "last_trade_price",
    "asset_id": "asset-1",
    "market": "market-1",
    "price": "0.55",
    "side": "buy",
    "size": "10",
    "fee_rate_bps": "0",
    "timestamp": "123"
  })");

  SmallVector<PolymarketMessage, kMessageBatchSize> messages;
  ASSERT_EQ(transport.poll_messages(messages, kMessageBatchSize), 1);
  ASSERT_EQ(messages.size(), 1);
  ASSERT_TRUE(std::holds_alternative<LastTradeMessage>(messages[0]));
}

TEST(MarketDataTransportTest, WaitForMessagesWakesConsumer) {
  FakeTransport transport;
  transport.start();

  std::thread producer([&transport]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    transport.ingest(R"({
      "event_type": "price_change",
      "market": "market-1",
      "price_changes": [{
        "asset_id": "asset-1",
        "price": "0.50",
        "size": "10",
        "side": "buy",
        "best_bid": "0.49",
        "best_ask": "0.50"
      }],
      "timestamp": "123"
    })");
  });

  EXPECT_TRUE(transport.wait_for_messages(std::chrono::milliseconds(50)));

  SmallVector<PolymarketMessage, kMessageBatchSize> messages;
  EXPECT_EQ(transport.poll_messages(messages, kMessageBatchSize), 1);
  producer.join();
}

TEST(MarketDataTransportTest, SubscribeAndUnsubscribeTrackAssets) {
  FakeTransport transport;
  SmallVector<AssetId, 2> subscribed_assets;
  subscribed_assets.push_back(AssetId{"asset-a"});
  subscribed_assets.push_back(AssetId{"asset-b"});
  transport.subscribe(subscribed_assets);
  ASSERT_EQ(transport.subscriptions().size(), 2);

  SmallVector<AssetId, 1> unsubscribed_assets;
  unsubscribed_assets.push_back(AssetId{"asset-a"});
  transport.unsubscribe(unsubscribed_assets);
  ASSERT_EQ(transport.subscriptions().size(), 1);
  EXPECT_EQ(std::string_view(transport.subscriptions().front()), "asset-b");
}

TEST(MarketDataTransportTest, PerfStatsTrackParsedMessages) {
  FakeTransport transport;
  transport.start();
  transport.ingest(R"({
    "event_type": "book",
    "asset_id": "asset-1",
    "market": "market-1",
    "bids": [{"price": "0.40", "size": "1"}],
    "asks": [{"price": "0.60", "size": "1"}],
    "timestamp": "123"
  })");

  auto snapshot = transport.perf_stats().snapshot();
  EXPECT_EQ(snapshot.frames_received, 1);
  EXPECT_EQ(snapshot.messages_parsed, 1);
  EXPECT_EQ(snapshot.book_messages, 1);
}

TEST(MarketDataTransportTest, PipelineDropsWhenQueueIsFullInsteadOfResizing) {
  PerfStats perf_stats({.enabled = true, .log_interval_messages = 1000000});
  MessagePipeline pipeline(1, &perf_stats);

  const size_t delivered = pipeline.ingest_message(R"([
    {
      "event_type": "last_trade_price",
      "asset_id": "asset-1",
      "market": "market-1",
      "price": "0.55",
      "side": "buy",
      "size": "10",
      "fee_rate_bps": "0",
      "timestamp": "123"
    },
    {
      "event_type": "last_trade_price",
      "asset_id": "asset-2",
      "market": "market-2",
      "price": "0.65",
      "side": "buy",
      "size": "11",
      "fee_rate_bps": "0",
      "timestamp": "124"
    }
  ])");

  EXPECT_EQ(delivered, 1);
  EXPECT_EQ(pipeline.size(), 1);

  SmallVector<PolymarketMessage, kMessageBatchSize> messages;
  EXPECT_EQ(pipeline.poll_messages(messages, kMessageBatchSize), 1);
  EXPECT_EQ(messages.size(), 1);

  const auto snapshot = perf_stats.snapshot();
  EXPECT_EQ(snapshot.messages_parsed, 2);
  EXPECT_EQ(snapshot.queue_backpressure_events, 1);
  EXPECT_EQ(snapshot.queue_dropped_messages, 1);
}
