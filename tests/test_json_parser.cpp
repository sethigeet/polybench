#include <gtest/gtest.h>

#include "json_parser.hpp"
#include "types/polymarket.hpp"

class JsonParserTest : public ::testing::Test {
 protected:
  JsonParser parser;

  SmallVector<PolymarketMessage, 2> parse(const std::string& json) {
    SmallVector<PolymarketMessage, 2> messages;
    parser.parse(json, messages);
    return messages;
  }
};

TEST_F(JsonParserTest, ParseBookMessage) {
  std::string json = R"({
    "event_type": "book",
    "asset_id": "12345678901234567890123456789012345678901234567890123456789012345678901234567",
    "market": "0x1234567890123456789012345678901234567890123456789012345678901234",
    "bids": [
      {"price": "0.50", "size": "100"},
      {"price": "0.49", "size": "200"}
    ],
    "asks": [
      {"price": "0.55", "size": "150"}
    ],
    "timestamp": "1704067200000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  ASSERT_TRUE(std::holds_alternative<BookMessage>(messages[0]));

  auto& msg = std::get<BookMessage>(messages[0]);
  EXPECT_EQ(std::string_view(msg.asset_id),
            "12345678901234567890123456789012345678901234567890123456789012345678901234567");
  EXPECT_EQ(std::string_view(msg.market),
            "0x1234567890123456789012345678901234567890123456789012345678901234");
  EXPECT_EQ(msg.timestamp, 1704067200000ULL);

  ASSERT_EQ(msg.bids.size(), 2);
  EXPECT_DOUBLE_EQ(msg.bids[0].price, 0.50);
  EXPECT_DOUBLE_EQ(msg.bids[0].size, 100.0);
  EXPECT_DOUBLE_EQ(msg.bids[1].price, 0.49);
  EXPECT_DOUBLE_EQ(msg.bids[1].size, 200.0);

  ASSERT_EQ(msg.asks.size(), 1);
  EXPECT_DOUBLE_EQ(msg.asks[0].price, 0.55);
  EXPECT_DOUBLE_EQ(msg.asks[0].size, 150.0);
}

TEST_F(JsonParserTest, ParsePriceChangeMessage) {
  std::string json = R"({
    "event_type": "price_change",
    "market": "market-123",
    "price_changes": [
      {
        "asset_id": "asset-1",
        "price": "0.55",
        "size": "250",
        "side": "buy",
        "best_bid": "0.54",
        "best_ask": "0.56"
      }
    ],
    "timestamp": "1704067200000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  ASSERT_TRUE(std::holds_alternative<PriceChangeMessage>(messages[0]));

  auto& msg = std::get<PriceChangeMessage>(messages[0]);
  EXPECT_EQ(std::string_view(msg.market), "market-123");
  EXPECT_EQ(msg.timestamp, 1704067200000ULL);

  ASSERT_EQ(msg.price_changes.size(), 1);
  EXPECT_EQ(std::string_view(msg.price_changes[0].asset_id), "asset-1");
  EXPECT_DOUBLE_EQ(msg.price_changes[0].price, 0.55);
  EXPECT_DOUBLE_EQ(msg.price_changes[0].size, 250.0);
  EXPECT_EQ(msg.price_changes[0].side, Side::Buy);
  EXPECT_DOUBLE_EQ(msg.price_changes[0].best_bid, 0.54);
  EXPECT_DOUBLE_EQ(msg.price_changes[0].best_ask, 0.56);
}

TEST_F(JsonParserTest, ParseLastTradeMessage) {
  std::string json = R"({
    "event_type": "last_trade_price",
    "asset_id": "asset-1",
    "market": "market-123",
    "price": "0.55",
    "side": "sell",
    "size": "100",
    "fee_rate_bps": "50",
    "timestamp": "1704067200000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  ASSERT_TRUE(std::holds_alternative<LastTradeMessage>(messages[0]));

  auto& msg = std::get<LastTradeMessage>(messages[0]);
  EXPECT_EQ(std::string_view(msg.asset_id), "asset-1");
  EXPECT_EQ(std::string_view(msg.market), "market-123");
  EXPECT_DOUBLE_EQ(msg.price, 0.55);
  EXPECT_EQ(msg.side, Side::Sell);
  EXPECT_DOUBLE_EQ(msg.size, 100.0);
  EXPECT_EQ(msg.fee_rate_bps, 50);
  EXPECT_EQ(msg.timestamp, 1704067200000ULL);
}

TEST_F(JsonParserTest, ParseTickSizeChangeMessage) {
  std::string json = R"({
    "event_type": "tick_size_change",
    "asset_id": "asset-1",
    "market": "market-123",
    "old_tick_size": "0.01",
    "new_tick_size": "0.001",
    "timestamp": "1704067200000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  ASSERT_TRUE(std::holds_alternative<TickSizeChangeMessage>(messages[0]));

  auto& msg = std::get<TickSizeChangeMessage>(messages[0]);
  EXPECT_EQ(std::string_view(msg.asset_id), "asset-1");
  EXPECT_EQ(std::string_view(msg.market), "market-123");
  EXPECT_DOUBLE_EQ(msg.old_tick_size, 0.01);
  EXPECT_DOUBLE_EQ(msg.new_tick_size, 0.001);
}

TEST_F(JsonParserTest, ParseMarketResolvedMessage) {
  std::string json = R"({
    "event_type": "market_resolved",
    "market": "market-123",
    "winning_asset_id": "asset-yes",
    "winning_outcome": "Yes",
    "assets_ids": ["asset-yes", "asset-no"],
    "outcomes": ["Yes", "No"],
    "timestamp": "1704067200000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  ASSERT_TRUE(std::holds_alternative<MarketResolvedMessage>(messages[0]));

  auto& msg = std::get<MarketResolvedMessage>(messages[0]);
  EXPECT_EQ(std::string_view(msg.market), "market-123");
  EXPECT_EQ(std::string_view(msg.winning_asset_id), "asset-yes");
  EXPECT_EQ(msg.winning_outcome, Outcome::Yes);
  ASSERT_EQ(msg.asset_ids.size(), 2);
}

TEST_F(JsonParserTest, ParseArrayOfMessages) {
  std::string json = R"([
    {
      "event_type": "last_trade_price",
      "asset_id": "asset-1",
      "market": "market-1",
      "price": "0.50",
      "side": "buy",
      "size": "50",
      "fee_rate_bps": "0",
      "timestamp": "1000"
    },
    {
      "event_type": "last_trade_price",
      "asset_id": "asset-2",
      "market": "market-2",
      "price": "0.60",
      "side": "sell",
      "size": "100",
      "fee_rate_bps": "25",
      "timestamp": "2000"
    }
  ])";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 2);

  ASSERT_TRUE(std::holds_alternative<LastTradeMessage>(messages[0]));
  ASSERT_TRUE(std::holds_alternative<LastTradeMessage>(messages[1]));

  auto& msg1 = std::get<LastTradeMessage>(messages[0]);
  auto& msg2 = std::get<LastTradeMessage>(messages[1]);

  EXPECT_EQ(std::string_view(msg1.market), "market-1");
  EXPECT_EQ(std::string_view(msg2.market), "market-2");
}

TEST_F(JsonParserTest, UnknownEventTypeReturnsEmpty) {
  std::string json = R"({
    "event_type": "unknown_event",
    "data": "something"
  })";

  auto messages = parse(json);
  EXPECT_TRUE(messages.empty());
}

TEST_F(JsonParserTest, BestBidAskEventIgnored) {
  std::string json = R"({
    "event_type": "best_bid_ask",
    "asset_id": "asset-1",
    "market": "market-1",
    "bid": "0.50",
    "ask": "0.55"
  })";

  auto messages = parse(json);
  EXPECT_TRUE(messages.empty());
}

TEST_F(JsonParserTest, NewMarketEventIgnored) {
  std::string json = R"({
    "event_type": "new_market",
    "market": "market-new"
  })";

  auto messages = parse(json);
  EXPECT_TRUE(messages.empty());
}

TEST_F(JsonParserTest, InvalidJsonReturnsEmpty) {
  std::string json = "{ invalid json }}}";
  auto messages = parse(json);
  EXPECT_TRUE(messages.empty());
}

TEST_F(JsonParserTest, ParseSideBuy) {
  std::string json = R"({
    "event_type": "last_trade_price",
    "asset_id": "a",
    "market": "m",
    "price": "0.50",
    "side": "Buy",
    "size": "10",
    "fee_rate_bps": "0",
    "timestamp": "1000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  auto& msg = std::get<LastTradeMessage>(messages[0]);
  EXPECT_EQ(msg.side, Side::Buy);
}

TEST_F(JsonParserTest, ParseSideSell) {
  std::string json = R"({
    "event_type": "last_trade_price",
    "asset_id": "a",
    "market": "m",
    "price": "0.50",
    "side": "SELL",
    "size": "10",
    "fee_rate_bps": "0",
    "timestamp": "1000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  auto& msg = std::get<LastTradeMessage>(messages[0]);
  EXPECT_EQ(msg.side, Side::Sell);
}

TEST_F(JsonParserTest, ParseOutcomeYes) {
  std::string json = R"({
    "event_type": "market_resolved",
    "market": "m",
    "winning_asset_id": "a",
    "winning_outcome": "yes",
    "assets_ids": [],
    "outcomes": [],
    "timestamp": "1000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  auto& msg = std::get<MarketResolvedMessage>(messages[0]);
  EXPECT_EQ(msg.winning_outcome, Outcome::Yes);
}

TEST_F(JsonParserTest, ParseOutcomeNo) {
  std::string json = R"({
    "event_type": "market_resolved",
    "market": "m",
    "winning_asset_id": "a",
    "winning_outcome": "No",
    "assets_ids": [],
    "outcomes": [],
    "timestamp": "1000"
  })";

  auto messages = parse(json);
  ASSERT_EQ(messages.size(), 1);
  auto& msg = std::get<MarketResolvedMessage>(messages[0]);
  EXPECT_EQ(msg.winning_outcome, Outcome::No);
}
